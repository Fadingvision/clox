#include <stdio.h>

#include "common.h"
#include "debug.h"
#include "compiler.h"
#include "vm.h"

// 基于栈的虚拟机
VM vm;

static void resetStack() {
  // 将栈顶指向数组初始的第一个位置为清空栈
  vm.stackTop = vm.stack;
}

static InterpretResult run() {
  // follow DRY rule
  #define READ_BYTE() (*vm.ip++)
  #define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
  #define BINARY_OP(op) \
      do { \
        double b = pop(); \
        double a = pop(); \
        push(a op b); \
      } while (false) // do while用于加一个块级作用域包裹代码块

  // 按序执行每个指令
  for(;;) {
    #ifdef DEBUG_TRACE_EXECUTION
      // DEBUG: 打印此时栈中所有参数
      printf("          ");
      for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
        printf("[ ");
        printValue(*slot);
        printf(" ]");
      }
      printf("\n");
      // DEBUG: 打印每个指令
      disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
    #endif

    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_NEGATE: push(-pop()); break;
      case OP_ADD:      BINARY_OP(+); break;
      case OP_SUBTRACT: BINARY_OP(-); break;
      case OP_MULTIPLY: BINARY_OP(*); break;
      case OP_DIVIDE:   BINARY_OP(/); break;
      case OP_RETURN: {
        printValue(pop());
        printf("\n");
        return INTERPRET_OK;
      }

      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);
        break;
      }
    }
  }

  #undef READ_BYTE
  #undef READ_CONSTANT
  #undef BINARY_OP
}

void initVM() {
  resetStack();
}

void freeVM() {}

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