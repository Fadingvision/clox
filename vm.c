#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "common.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "compiler.h"
#include "value.h"
#include "vm.h"

// 基于栈的虚拟机
VM vm;

static void resetStack() {
  // 将栈顶指向数组初始的第一个位置为清空栈
  vm.stackTop = vm.stack;
}

// c的可变长参数函数
static void runtimeError(const char* format, ...) {
  // 定义一个va_list类型的变量，变量是指向参数的指针。
  va_list args;
  // va_start初始化刚定义的变量，第二个参数是最后一个显式声明的参数。
  va_start(args, format);
  // 将args已format的形式写入到stderr中
  vfprintf(stderr, format, args);
  // va_end将变量args重置为NULL。
  va_end(args);
  fputs("\n", stderr);

  // 写入出错的行数
  size_t instruction = vm.ip - vm.chunk->code;
  int line = vm.chunk->lines[instruction];
  fprintf(stderr, "[line %d] in script\n", line);

  resetStack();
}

// 除了nil和false本身，其余全为true
static bool toBool(Value value) {
  if (IS_NIL(value)) return false;
  if (IS_BOOL(value) && !AS_BOOL(value)) {
    return false;
  }
  return true;
}

static void concatenate() {
  ObjString* b = AS_STRING(pop());
  ObjString* a = AS_STRING(pop());

  // 把a和b的字符串拷贝到一个新的字符串中
  // int length = a->length + b->length;
  // char* chars = ALLOCATE(char, length + 1);
  // memcpy(chars, a->chars, a->length);
  // memcpy(chars + a->length, b->chars, b->length);
  // chars[length] = '\0';

  ObjString* result = concatenateString(a, b);
  push(OBJ_VAL(result));
}

static InterpretResult run() {
  // do not repeat ourself
  #define READ_BYTE() (*vm.ip++)
  // 位运算
  #define READ_SHORT() (vm.ip += 2, (uint16_t)((vm.ip[-2] << 8) | vm.ip[-1]))
  #define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
  #define READ_STRING() AS_STRING(READ_CONSTANT())
  #define BINARY_OP(valueType, op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        runtimeError("Operands must be numbers."); \
        return INTERPRET_RUNTIME_ERROR; \
      } \
      \
      double b = AS_NUMBER(pop()); \
      double a = AS_NUMBER(pop()); \
      push(valueType(a op b)); \
    } while (false) // do while用于加一个块级作用域包裹代码块

  // 按序执行每个指令
  for(;;) {
    #ifdef DEBUG_TRACE_EXECUTION
      // DEBUG: 打印此时内存中所有参数
      printf("          ");
      for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
        printf("[ ");
        printValue(*slot);
        printf(" ]");
      }
      printf("\n");
      // DEBUG: 打印待执行的指令
      disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
    #endif

    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_NEGATE: {
        // 类型检测
        if (!IS_NUMBER(peek(0))) {
          runtimeError("Operand must be a number");
          return INTERPRET_RUNTIME_ERROR;
        }
        // 取负数写入内存
        push(NUMBER_VAL(-AS_NUMBER(pop())));
        break;
      }
      case OP_NOT: {
        // 对栈顶的数取反，然后写入栈中
        push(BOOL_VAL(!toBool(pop())));
        break;
      }
      case OP_ADD: {
        // 支持字符串相加
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          concatenate();
        } else {
          BINARY_OP(NUMBER_VAL, +);
        }
        break;
      }
      case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
      case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
      case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;

      case OP_GREATER:  BINARY_OP(BOOL_VAL, >); break;
      case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        // 将比较后的结果转为Value写入内存
        push(BOOL_VAL(isEuqal(a, b)));
        break;
      }
      case OP_PRINT: {
        printValue(pop());
        printf("\n");
        break;
      }
      case OP_RETURN: {
        return INTERPRET_OK;
      }
      case OP_POP: {
        pop();
        break;
      }
      case OP_GET_LOCAL: {
        // 在locals中的位置 = 在stack中的位置
        uint8_t slot = READ_BYTE();
        // 直接将该值push在stack中供后续表达式使用
        push(vm.stack[slot]);
        break;
      }
      case OP_SET_LOCAL: {
        // 在locals中的位置 = 在stack中的位置
        uint8_t slot = READ_BYTE();
        // 直接将stack的值进行替换，也就完成了赋值
        vm.stack[slot] = peek(0);

        // 在赋值表达式中，并不需要pop(), 因为在compile赋值表达式的时候，默认插入了一个OP_POP指令
        break;
      }
      case OP_DEFINE_GLOBAL: {
        // 从栈中去除放入table中
        ObjString* name = READ_STRING();
        tableSet(&vm.globals, name, peek(0));
        pop();
        break;
      }
      case OP_GET_GLOBAL: {
        // 类似于constant, 从table中取到之后推入栈中，待其他的表达式使用
        ObjString* name = READ_STRING();
        Value value;
        if (!tableGet(&vm.globals, name, &value)) {
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        push(value);
        break;
      }
      case OP_SET_GLOBAL: {
        ObjString* name = READ_STRING();
        Value value;
        if (!tableGet(&vm.globals, name, &value)) {
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        } else {
          tableSet(&vm.globals, name, peek(0));
        }
        break;
      }
      // 读出来写入内存
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);
        break;
      }
      case OP_NIL: push(NIL_VAL); break;
      case OP_FALSE: push(BOOL_VAL(false)); break;
      case OP_TRUE: push(BOOL_VAL(true)); break;

      // logic control flow
      case OP_JUMP_IF_FALSE: {
        // 读出跳过的字节大小（两个字节存储在OP_JUMP_IF_FALSE后）
        uint16_t offset = READ_SHORT();
        // 此时的条件表达式产生的值应该在栈顶，
        // 如果该条件为假，则跳过offset字节的指令
        // 条件为真，则这offset个字节的指令会正常执行
        if (!toBool(peek(0))) vm.ip += offset;
        break;
      }
      case OP_JUMP: {
        // 读出跳过的字节大小
        uint16_t offset = READ_SHORT();
        // 无条件跳过offset字节的指令
        vm.ip += offset;
        break;
      }
    }
  }

  #undef READ_BYTE
  #undef READ_SHORT
  #undef READ_CONSTANT
  #undef READ_STRING
  #undef BINARY_OP
}

void initVM() {
  resetStack();
  vm.objects = NULL;
  initTable(&vm.strings);
  initTable(&vm.globals);
}

void freeVM() {
  freeTable(&vm.strings);
  freeTable(&vm.globals);
  freeObjects();
}

InterpretResult interpret(const char* source) {
  Chunk chunk;
  initChunk(&chunk);
  if (!compile(source, &chunk)) {
    freeChunk(&chunk);
    return INTERPRET_COMPILE_ERROR;
  }
  vm.chunk = &chunk;
  vm.ip = vm.chunk->code;
  InterpretResult result = run();
  freeChunk(&chunk);
  return result;
}

void push(Value value) {
  *vm.stackTop = value;
  vm.stackTop++;
}

Value pop() {
  vm.stackTop--;
  return *vm.stackTop; 
}

// 返回距离栈顶distance距离的元素
Value peek(int distance) {
  return vm.stackTop[-1 - distance];
}