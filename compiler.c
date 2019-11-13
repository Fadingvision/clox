#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"
#include "chunk.h"
#include "object.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

// lox优先级顺序，值越大优先级越高
typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,  // =
  PREC_OR,          // or
  PREC_AND,         // and
  PREC_EQUALITY,    // == !=
  PREC_COMPARISON,  // < > <= >=
  PREC_TERM,        // + -
  PREC_FACTOR,      // * /
  PREC_UNARY,       // ! -
  PREC_CALL,        // . () []
  PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(bool canAssign);

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

// 局部变量
typedef struct {
  // 变量名
  Token name;
  // 变量所在的块级作用域深度
  int depth;
} Local;

typedef struct {
  Local locals[UINT8_COUNT];
  // 变量个数
  int localCount;
  // 当前正在编译的块级作用域的深度，默认为0即全局作用域
  int scopeDepth;
} Compiler;

// Parser 执行one-pass策略，一次循环中编译
typedef struct {
  Token current;  // 下一个token
  Token previous; // 当前token
  bool panicMode; // 是否已经进入了错误模式
  bool hadError;
} Parser;

// 创建一个全局变量，以免传来传去
Parser parser;

// 当前正在写入的chunk
Chunk* compilingChunk;

// compiler
Compiler* current = NULL;

static void initCompiler(Compiler* compiler) {
  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  current = compiler;
}

// -------------------- token方法 -------------------------

// 打印错误信息
static void errorAt(Token* token, const char* message) {
  if (parser.panicMode) return;
  parser.panicMode = true;

  // 错误行数
  fprintf(stderr, "[line %d] Error", token->line);

  // 错误的token字符串
  if (token->type == TOKEN_EOF) {
    fprintf(stderr, " at end");
  } else if (token->type == TOKEN_ERROR) {
    // Nothing.
  } else {
    fprintf(stderr, " at '%.*s'", token->length, token->start);
  }

  // 错误信息
  fprintf(stderr, ": %s\n", message);
  parser.hadError = true;
}

// 语法分析错误
static void error(const char* message) {
  errorAt(&parser.previous, message);   
}

// 词法分析错误
static void errorAtCurrent(const char* message) {
  errorAt(&parser.current, message);
}

// 消费任意的一个不为error的token
static void advance() {
  // 保存之前的一个token
  parser.previous = parser.current;

  for (;;) {
    parser.current = scanToken();
    if (parser.current.type != TOKEN_ERROR) break;

    // 如果是error token，则报错, start 则是message
    errorAtCurrent(parser.current.start);
  }
}

// 消费指定类型的token一个，用于前瞻
static void consume(TokenType type, const char* message) {
  if (parser.current.type == type) {
    advance();
    return;
  }

  errorAtCurrent(message);
}

static bool check(TokenType type) {
  return parser.current.type == type;
}

static bool match(TokenType type) {
  if (!check(type)) return false;
  advance();
  return true;
}

// --------------------  字节码写入方法  -------------------------

static Chunk* currentChunk() {
  return compilingChunk;
}

// 写入一个字节指令到chunk中
static void emitByte(uint8_t byte) {
  writeChunk(currentChunk(), byte, parser.previous.line);
}

// 写入两个字节指令到chunk中，例如constant的操作数
static void emitBytes(uint8_t byte1, uint8_t byte2) {
  emitByte(byte1);
  emitByte(byte2);
}

// return 指令
static void emitReturn() {
  emitByte(OP_RETURN);
}

// 写一个Value struct到chunk的constants数组中，返回index
static uint8_t makeConstant(Value value) {
  // 如果已经存在相同的string Constant，则直接复用
  Chunk* ck = currentChunk();
  if (IS_STRING(value)) {
    for (int i = 0; i < ck->count; i++) {
      if (isEuqal(value, ck->constants.values[i])) {
        return i;
      }
    }
  }
  int constant = addConstant(currentChunk(), value);
  if (constant > UINT8_MAX) {
    error("Too many constants in one chunk.");
    return 0;
  }

  return (uint8_t)constant;
}

// 写入一个double类型的常量字节
static void emitConstant(Value value) {
  emitBytes(OP_CONSTANT, makeConstant(value));
}

// 暂时用return指令来结束编译
static void endCompiler() {
  emitReturn();

  // 打印当前指令集，验证编译正确性
  #ifdef DEBUG_PRINT_CODE
  if (!parser.hadError) {
    disassembleChunk(currentChunk(), "code");
  }
  #endif
}


// 存在循环引用，因此需要先声明，否则编译报错
static void expression();
static void declaration();
static void statement();
static bool isIdentifierEqual(Token* a, Token* b);
static uint8_t identifierConstant(Token*);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

// -------------------- 将对应的表达式转为字节码 -------------------------

// group表达式，去掉左右括号直接执行中间的表达式
static void grouping(bool canAssign) {
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

// 数字表达式：将字符串转为double, 类似parseFloat自动取前面的数字
static void number(bool canAssign) {
  double value = strtod(parser.previous.start, NULL);
  emitConstant(NUMBER_VAL(value));
}

// 从块级作用域去找该变量
static int resolveLocal(Compiler* compiler, Token* name) {
  // 从locals一层一层的往上找，直到找到名字相同的变量，返回其在locals中的位置index
  for (int i = compiler->localCount - 1; i >= 0; i--) {
    Local* local = &compiler->locals[i];
    if (isIdentifierEqual(name, &local->name)) {
      return i;
    }
  }

  return -1;
}

// assignment → ( call "." )? IDENTIFIER "=" assignment
static void namedVariable(Token name, bool canAssign) {
  uint8_t getOp, setOp;
  // 首先尝试从块级作用域去找该变量
  // 局部: arg为在locals中的位置
  // 全局: arg为在constants中的位置
  int arg = resolveLocal(current, &name);

  // 变量找到
  if (arg != -1) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
  } else {
    arg = identifierConstant(&name);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }

  /* 
    为了避免将a * b = c + d解析为 a * (b = c + d): 这样违背了运算符优先级。
    因此需要加一个限制条件: canAssign.
    成立的条件：变量前面的运算符的优先级 <= PREC_ASSIGNMENT
   */
  if (canAssign && match(TOKEN_EQUAL)) {
    expression();
    emitBytes(setOp, arg);
  } else {
    emitBytes(getOp, arg);
  }
}

// 变量
static void variable(bool canAssign) {
  namedVariable(parser.previous, canAssign);
}

// 文本表达式：nil, false, true;
static void literal(bool canAssign) {
  // 直接写入对应的操作指令
  switch (parser.previous.type) {
    case TOKEN_FALSE: emitByte(OP_FALSE); break;
    case TOKEN_TRUE: emitByte(OP_TRUE); break;
    case TOKEN_NIL: emitByte(OP_NIL); break;
    default:
      break;
  }
}

// 字符串
static void string(bool canAssign) {
  // 将去掉引号的字符串copy并组成ObjString，然后组成Value类型写入内存
  emitConstant(OBJ_VAL(copyString(parser.previous.start + 1,
    parser.previous.length - 2)));
}

// 一元表达式
static void unary(bool canAssign) {
  TokenType operatorType = parser.previous.type;
  // 因为是右结合的，优先写入同级或更高优先级的表达式
  parsePrecedence(PREC_UNARY);

  // 然后写入一元表达式操作符
  switch (operatorType) {
    case TOKEN_BANG: emitByte(OP_NOT); break;
    case TOKEN_MINUS: emitByte(OP_NEGATE); break;
    default:
      return;
  }
}

// 二元表达式
static void binary(bool canAssign) {
  TokenType operatorType = parser.previous.type;

  ParseRule* rule = getRule(operatorType);
  // 写入右边表达式
  // 因为是左结合的，所以只能优先写入优先级更高的，不然 a - b - c 就会被解析成 a - (b - c)
  parsePrecedence((Precedence)(rule->precedence + 1));

  // 写入操作符，我们的字节码是基于栈的，因此先写操作数，再写入操作符
  switch (operatorType) {
    case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT); break;
    case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
    case TOKEN_GREATER:       emitByte(OP_GREATER); break;
    // 这里并没有GREATER_EQUAL指令，而是使用 !(a < b) 来代替 a >= b
    case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
    case TOKEN_LESS:          emitByte(OP_LESS); break;
    case TOKEN_LESS_EQUAL:    emitBytes(OP_GREATER, OP_NOT); break;
    case TOKEN_PLUS:          emitByte(OP_ADD); break;
    case TOKEN_MINUS:         emitByte(OP_SUBTRACT); break;
    case TOKEN_STAR:          emitByte(OP_MULTIPLY); break;
    case TOKEN_SLASH:         emitByte(OP_DIVIDE); break;
    default:
      return;
  }
}

// -------------------- Pratt Parser -------------------------

// 每种类型对应的解析规则表：
// 一些pratt parser实现中，倾向于把这些方法内置到token的对象中，但是这种查找表的方式看起来更直观易懂
ParseRule rules[] = {
/* Compiling Expressions rules < Calls and Functions infix-left-paren
  { grouping, NULL,    PREC_NONE },       // TOKEN_LEFT_PAREN
*/
//> Calls and Functions infix-left-paren
  { grouping, NULL,    PREC_CALL },       // TOKEN_LEFT_PAREN
//< Calls and Functions infix-left-paren
  { NULL,     NULL,    PREC_NONE },       // TOKEN_RIGHT_PAREN
  { NULL,     NULL,    PREC_NONE },       // TOKEN_LEFT_BRACE [big]
  { NULL,     NULL,    PREC_NONE },       // TOKEN_RIGHT_BRACE
  { NULL,     NULL,    PREC_NONE },       // TOKEN_COMMA
/* Compiling Expressions rules < Classes and Instances not-yet
  { NULL,     NULL,    PREC_NONE },       // TOKEN_DOT
*/
//> Classes and Instances not-yet
  { NULL,     NULL,     PREC_CALL },       // TOKEN_DOT
//< Classes and Instances not-yet
  { unary,    binary,  PREC_TERM },       // TOKEN_MINUS
  { NULL,     binary,  PREC_TERM },       // TOKEN_PLUS
  { NULL,     NULL,    PREC_NONE },       // TOKEN_SEMICOLON
  { NULL,     binary,  PREC_FACTOR },     // TOKEN_SLASH
  { NULL,     binary,  PREC_FACTOR },     // TOKEN_STAR
/* Compiling Expressions rules < Types of Values table-not
  { NULL,     NULL,    PREC_NONE },       // TOKEN_BANG
*/
//> Types of Values table-not
  { unary,    NULL,    PREC_NONE },       // TOKEN_BANG
//< Types of Values table-not
/* Compiling Expressions rules < Types of Values table-equal
  { NULL,     NULL,    PREC_NONE },       // TOKEN_BANG_EQUAL
*/
//> Types of Values table-equal
  { NULL,     binary,  PREC_EQUALITY },   // TOKEN_BANG_EQUAL
//< Types of Values table-equal
  { NULL,     NULL,    PREC_NONE },       // TOKEN_EQUAL
/* Compiling Expressions rules < Types of Values table-comparisons
  { NULL,     NULL,    PREC_NONE },       // TOKEN_EQUAL_EQUAL
  { NULL,     NULL,    PREC_NONE },       // TOKEN_GREATER
  { NULL,     NULL,    PREC_NONE },       // TOKEN_GREATER_EQUAL
  { NULL,     NULL,    PREC_NONE },       // TOKEN_LESS
  { NULL,     NULL,    PREC_NONE },       // TOKEN_LESS_EQUAL
*/
//> Types of Values table-comparisons
  { NULL,     binary,  PREC_EQUALITY },   // TOKEN_EQUAL_EQUAL
  { NULL,     binary,  PREC_COMPARISON }, // TOKEN_GREATER
  { NULL,     binary,  PREC_COMPARISON }, // TOKEN_GREATER_EQUAL
  { NULL,     binary,  PREC_COMPARISON }, // TOKEN_LESS
  { NULL,     binary,  PREC_COMPARISON }, // TOKEN_LESS_EQUAL
//< Types of Values table-comparisons
/* Compiling Expressions rules < Global Variables table-identifier
  { NULL,     NULL,    PREC_NONE },       // TOKEN_IDENTIFIER
*/
//> Global Variables table-identifier
  { variable, NULL,    PREC_NONE },       // TOKEN_IDENTIFIER
//< Global Variables table-identifier
/* Compiling Expressions rules < Strings table-string
  { NULL,     NULL,    PREC_NONE },       // TOKEN_STRING
*/
//> Strings table-string
  { string,   NULL,    PREC_NONE },       // TOKEN_STRING
//< Strings table-string
  { number,   NULL,    PREC_NONE },       // TOKEN_NUMBER
/* Compiling Expressions rules < Jumping Back and Forth table-and
  { NULL,     NULL,    PREC_NONE },       // TOKEN_AND
*/
//> Jumping Back and Forth table-and
  { NULL,     NULL,    PREC_AND },        // TOKEN_AND
//< Jumping Back and Forth table-and
  { NULL,     NULL,    PREC_NONE },       // TOKEN_CLASS
  { NULL,     NULL,    PREC_NONE },       // TOKEN_ELSE
//> Types of Values table-false
  { literal,  NULL,    PREC_NONE },       // TOKEN_FALSE
//< Types of Values table-false
  { NULL,     NULL,    PREC_NONE },       // TOKEN_FOR
  { NULL,     NULL,    PREC_NONE },       // TOKEN_FUN
  { NULL,     NULL,    PREC_NONE },       // TOKEN_IF
//> Types of Values table-nil
  { literal,  NULL,    PREC_NONE },       // TOKEN_NIL
//< Types of Values table-nil
/* Compiling Expressions rules < Jumping Back and Forth table-or
  { NULL,     NULL,    PREC_NONE },       // TOKEN_OR
*/
//> Jumping Back and Forth table-or
  { NULL,     NULL,     PREC_OR },         // TOKEN_OR
//< Jumping Back and Forth table-or
  { NULL,     NULL,    PREC_NONE },       // TOKEN_PRINT
  { NULL,     NULL,    PREC_NONE },       // TOKEN_RETURN
/* Compiling Expressions rules < Superclasses not-yet
  { NULL,     NULL,    PREC_NONE },       // TOKEN_SUPER
*/
//> Superclasses not-yet
  { NULL,   NULL,    PREC_NONE },       // TOKEN_SUPER
//< Superclasses not-yet
/* Compiling Expressions rules < Methods and Initializers not-yet
  { NULL,     NULL,    PREC_NONE },       // TOKEN_THIS
*/
//> Methods and Initializers not-yet
  { NULL,    NULL,    PREC_NONE },       // TOKEN_THIS
//< Methods and Initializers not-yet
/* Compiling Expressions rules < Types of Values table-true
  { NULL,     NULL,    PREC_NONE },       // TOKEN_TRUE
*/
//> Types of Values table-true
  { literal,  NULL,    PREC_NONE },       // TOKEN_TRUE
//< Types of Values table-true
  { NULL,     NULL,    PREC_NONE },       // TOKEN_VAR
  { NULL,     NULL,    PREC_NONE },       // TOKEN_WHILE
  { NULL,     NULL,    PREC_NONE },       // TOKEN_ERROR
  { NULL,     NULL,    PREC_NONE },       // TOKEN_EOF
};

static void synchronize() {
  // 重置panic位
  parser.panicMode = false;

  // 当遇见下述token的时候，我们认为即将开始一个新的语句，在这之前，丢弃所遇见的token
  while (parser.current.type != TOKEN_EOF) {
    if (parser.previous.type == TOKEN_SEMICOLON) return;

    switch (parser.current.type) {
      case TOKEN_CLASS:
      case TOKEN_FUN:
      case TOKEN_VAR:
      case TOKEN_FOR:
      case TOKEN_IF:
      case TOKEN_WHILE:
      case TOKEN_PRINT:
      case TOKEN_RETURN:
        return;
      default: break;
    }

    advance();
  }
}

static uint8_t identifierConstant(Token* name) {
  // 将变量名字符串对象写入constants中
  return makeConstant(OBJ_VAL(copyString(name->start, name->length)));
}

// 将变量名加入到locals数组
static void addLocal(Token name) {
  if (current->localCount == UINT8_COUNT) {
    error("Too many local variables in function.");
    return;
  }

  Local* local = &current->locals[current->localCount++];
  local->name = name;
  local->depth = current->scopeDepth;
}

static bool isIdentifierEqual(Token* a, Token* b) {
  if (a->length != b->length) return false;
  return memcmp(a->start, b->start, a->length) == 0;
}

// 声明一个局部变量
static void declareVariable() {
  Token* name = &parser.previous;

  // 检测当前作用域内是否存在同名变量
  for (int i = current->localCount - 1; i >= 0; i--) {
    Local* local = &current->locals[i];
    if (local->depth != -1 && local->depth < current->scopeDepth) {
      break;
    }

    if (isIdentifierEqual(name, &local->name)) {
      error("Variable with this name already declared in this scope.");
    }
  }

  addLocal(*name);
}

static uint8_t parseVariable(const char* errorMessage) {
  consume(TOKEN_IDENTIFIER, errorMessage);

  // 局部变量
  if (current->scopeDepth > 0) {
    declareVariable();
    return 0;
  }

  // 全局变量
  return identifierConstant(&parser.previous);
}

static void defineVariable(uint8_t global) {
  // 局部变量
  // NOTE: 在声明局部变量的时候，并不需要像全局变量一样
  // 反之，我们并不产生任何指令，而是让expression产生的值暂时就放置在stack中
  // 这样变量值在stack中的位置 = 变量名在locals中的位置
  if (current->scopeDepth > 0) return;

  // 全局变量：runtime的时候用一个OP_DEFINE_GLOBAL指令来将expression产生的值
  // 保存在table中，然后pop掉stack中的值
  emitBytes(OP_DEFINE_GLOBAL, global);
}

// varDecl → "var" IDENTIFIER ( "=" expression )? ";" ;
static void varDeclaration() {
  // 解析变量名，并返回其在instants中存储的index位置
  uint8_t global = parseVariable("Expect variable name");

  if (match(TOKEN_EQUAL)) {
    expression();
  } else {
    // 默认初始化为nil值
    emitByte(OP_NIL);
  }

  consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
  // 写入定义全局变量指令和该指令的操作数：global
  defineVariable(global);
}

/* 
  declaration  → classDecl | funDecl | varDecl | statement ;
*/
static void declaration() {
  if (match(TOKEN_VAR)) {
    varDeclaration();
  } else {
    statement();
  }
  // 解析某个语句发生错误的时候，进行同步操作：即丢弃当前语句的解析工作，直到下个语句
  // 这样做可以让我们的编译器同时的发现更多的错误，而不是在第一个错误的时候就退出
  if (parser.panicMode) synchronize();
}

// printStmt → "print" expression ";" ;
static void printStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  // 写入print指令
  emitByte(OP_PRINT);
}

// exprStmt  → expression ";" ;
static void expressionStatement() {
  expression();
  consume(TOKEN_SEMICOLON, "Expect ';' after expression.");
  // 语义上来说，表达式语句会产生一个值，并且直接被丢弃
  emitByte(OP_POP);
}

// blockStmt → "{" declaration* "}" ;
static void block() {
  while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF)) {
    declaration();
  }

  consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void beginScope() {
  current->scopeDepth++;
}

static void endScope() {
  current->scopeDepth--;

  // 从当前作用域退出时，删除该作用域的中的变量，同时也就是去除stack中的临时变量
  while (current->localCount > 0 &&
         current->locals[current->localCount - 1].depth >
            current->scopeDepth) {

    // 由于在退出作用域时，stack中还持有声明变量时的变量值，因此需要依次删除这些值
    emitByte(OP_POP);
    current->localCount--;
  }
}

// statement → exprStmt | forStmt | ifStmt | printStmt | returnStmt | whileStmt | block ;
static void statement() {
  if (match(TOKEN_PRINT)) {
    printStatement();
  } else if (match(TOKEN_LEFT_BRACE)) {
    beginScope();
    block();
    endScope();
  } else {
    expressionStatement();
  }
}

// 表达式
static void expression() {
  // 从优先级最低的开始
  parsePrecedence(PREC_ASSIGNMENT);
}

// 根据类型获取token的解析规则
static ParseRule* getRule(TokenType type) {
  return &rules[type];
}

/* 
  how `(-1 + 2) * 3 - -4` works:

  parsePrecedence(PREC_ASSIGNMENT)
  ( => prefix => grouping
    parsePrecedence(PREC_ASSIGNMENT)
    - => unary
      parsePrecedence(PREC_UNARY);
      1 => number; // 写入CONSTANT: 1
      +号的优先级PREC_TERM低于PREC_UNARY: 退出
      => // 写入NEGATE指令
    +的优先级大于PREC_ASSIGNMENT => infixRule => binary
      parsePrecedence(PREC_FACTOR);
      2 => number; // 写入CONSTANT: 2
      )号的优先级PREC_NONE低于PREC_FACTOR: 退出
      => // 写入ADD
    => )号的优先级PREC_NONE低于PREC_ASSIGNMENT: 退出
  * => infix => binary
    parsePrecedence(PREC_UNARY);
    3 => number; // 写入CONSTANT: 3
    PREC_TERM低于PREC_UNARY: 退出
    => // 写入OP_MULTIPLY
  - => infix => binary
    parsePrecedence(PREC_UNARY);
      - => unary
      parsePrecedence(PREC_UNARY);
        4 => number; // 写入CONSTANT: 4
        PREC_NONE低于PREC_UNARY: 退出
      => // 写入NEGATE指令
    4 => number; // 写入CONSTANT: 4
    EOF号的优先级PREC_NONE低于PREC_UNARY: 退出
  => // 写入OP_SUBTRACT
  EOF号的优先级PREC_NONE低于PREC_UNARY: 退出
*/
static void parsePrecedence(Precedence precedence) {
  // lox中一个表达式的开头必须为前缀表达式.
  // 也就是：(, -, !, indentifier, string, number, false, true, nil, super, this
  advance();
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == NULL) {
    error("Expect expression.");
    return;
  }
  // 解析对应的前缀表达式
  bool canAssign = precedence <= PREC_ASSIGNMENT;
  prefixRule(canAssign);

  // 开始查看是否有中序表达式的优先级token
  // 当后续的token优先级比当前解析的优先级高或者同级的时候，继续解析后续的表达式
  // 这里是<=, 因此默认为右结合
  while (precedence <= getRule(parser.current.type)->precedence) {
    // 消费操作符
    advance();
    // 开始解析右边表达式，目前只有binary
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule(canAssign);
  }

  // (a * b) = c + d
  // (a * b)不是一个有效的可赋值对象
  if (canAssign && match(TOKEN_EQUAL)) {
    error("Invalid assignment target.");
    expression();
  }
}

// -------------------- entry -------------------------

bool compile(const char* source, Chunk* chunk) {
  // init scanner
  initScanner(source);

  // init compiler
  Compiler compiler;
  initCompiler(&compiler);

  // init chunk
  compilingChunk = chunk;

  // init parser
  parser.hadError = false;
  parser.panicMode = false;

  advance();

  while (!match(TOKEN_EOF)) {
    declaration();
  }
  
  consume(TOKEN_EOF, "Unexpected end of expression");

  endCompiler();
  return !parser.hadError;

  // 不需要像glox一样一次性把所有的token分析出来，只需要按需分析，节省内存
  // int line = -1;
  // for (;;) {
  //   Token token = scanToken();
  //   if (token.line != line) {
  //     printf("%4d ", token.line);
  //     line = token.line;
  //   } else {
  //     printf("   | ");
  //   }
  //   printf("%2d '%.*s'\n", token.type, token.length, token.start); // [format]
  //   if (token.type == TOKEN_EOF) break;
  // }
}