#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"

#define STACK_MAX 256

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

typedef struct {
  Chunk* chunk;
  // 指向instruction
  uint8_t* ip;
  // 运行时参数内存栈
  Value stack[STACK_MAX];
  // 栈顶，默认指向下一个需要存储的位置
  Value* stackTop;
} VM;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);

// 入栈
void push(Value value);
// 出栈
Value pop();
// 取距离栈顶distance的数据
Value peek(int distance);

# endif