#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "scanner.h"
#include "chunk.h"

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

typedef void (*ParseFn)();

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

// Parser 执行one-pass策略，一次循环中编译
typedef struct {
  Token current;  // 下一个token
  Token previous; // 当前token
  bool panicMode; // 是否已经进入了错误模式
  bool hadError;
} Parser;

// 创建一个全局变量，以免传来传去
Parser parser;

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

// --------------------  字节码写入方法  -------------------------

// 当前正在写入的chunk
Chunk* compilingChunk;

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

// 写一个constant的double值到chunk的constants数组中，返回index
static uint8_t makeConstant(Value value) {
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
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

// -------------------- 将对应的表达式转为字节码 -------------------------

// group表达式，去掉左右括号直接执行中间的表达式
static void grouping() {
  expression();
  consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}

// 数字表达式：将字符串转为double, 类似parseFloat自动取前面的数字
static void number() {
  double value = strtod(parser.previous.start, NULL);
  emitConstant(value);
}

// 一元表达式
static void unary() {
  TokenType operatorType = parser.previous.type;
  // 优先写入比一元表达式更高优先级的表达式或同级的表达式，一元表达式可以嵌套：(--4)
  parsePrecedence(PREC_UNARY);

  // 然后写入一元表达式
  switch (operatorType) {
    case TOKEN_BANG: emitByte(OP_NOT); break;
    case TOKEN_MINUS: emitByte(OP_NEGATE); break;
    default:
      return;
  }
}

// 二元表达式
static void binary() {
  TokenType operatorType = parser.previous.type;

  ParseRule* rule = getRule(operatorType);
  // 优先写入比当前二元表达式更高的优先级的表达式
  parsePrecedence((Precedence)(rule->precedence + 1));

  switch (operatorType) {
    case TOKEN_BANG_EQUAL:    emitBytes(OP_EQUAL, OP_NOT); break;
    case TOKEN_EQUAL_EQUAL:   emitByte(OP_EQUAL); break;
    case TOKEN_GREATER:       emitByte(OP_GREATER); break;
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
  { NULL, NULL,    PREC_NONE },       // TOKEN_IDENTIFIER
//< Global Variables table-identifier
/* Compiling Expressions rules < Strings table-string
  { NULL,     NULL,    PREC_NONE },       // TOKEN_STRING
*/
//> Strings table-string
  { NULL,   NULL,    PREC_NONE },       // TOKEN_STRING
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
/* Compiling Expressions rules < Types of Values table-false
  { NULL,     NULL,    PREC_NONE },       // TOKEN_FALSE
*/
//> Types of Values table-false
  { NULL,  NULL,    PREC_NONE },       // TOKEN_FALSE
//< Types of Values table-false
  { NULL,     NULL,    PREC_NONE },       // TOKEN_FOR
  { NULL,     NULL,    PREC_NONE },       // TOKEN_FUN
  { NULL,     NULL,    PREC_NONE },       // TOKEN_IF
/* Compiling Expressions rules < Types of Values table-nil
  { NULL,     NULL,    PREC_NONE },       // TOKEN_NIL
*/
//> Types of Values table-nil
  { NULL,  NULL,    PREC_NONE },       // TOKEN_NIL
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
  { NULL,  NULL,    PREC_NONE },       // TOKEN_TRUE
//< Types of Values table-true
  { NULL,     NULL,    PREC_NONE },       // TOKEN_VAR
  { NULL,     NULL,    PREC_NONE },       // TOKEN_WHILE
  { NULL,     NULL,    PREC_NONE },       // TOKEN_ERROR
  { NULL,     NULL,    PREC_NONE },       // TOKEN_EOF
};

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
  printf("%4d\n", precedence);
  advance();
  // 前缀表达式规则: number, grouping, unary
  ParseFn prefixRule = getRule(parser.previous.type)->prefix;
  if (prefixRule == NULL) {
    error("Expect expression.");
    return;
  }

  // 解析对应的表达式
  prefixRule();

  // 依次解析后面的优先级高于当前优先级的表达式
  while (precedence <= getRule(parser.current.type)->precedence) {
    advance();
    ParseFn infixRule = getRule(parser.previous.type)->infix;
    infixRule();
  }
}

// -------------------- entry -------------------------

bool compile(const char* source, Chunk* chunk) {
  // init scanner
  initScanner(source);

  // init chunk
  compilingChunk = chunk;

  // init parser
  parser.hadError = false;
  parser.panicMode = false;

  advance();
  // 递归解析表达式
  expression();
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