#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "object.h"
#include "table.h"

#define FRAMES_MAX 64                       
#define STACK_MAX (FRAMES_MAX * UINT8_COUNT)

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

// CallFrame代表一个正在进行的函数调用
typedef struct {
  // 正在执行的函数
  ObjClosure* closure;
  // 函数体指令集
  uint8_t* ip;
  /* 
    call Window: 在函数出现之前，我们获取变量的值是根据它在stack中的偏移量而来的，这个值不会变化
    在函数之后，由于函数的调用位置和次数的不确定性，这个偏移位置同样具有了不确定性
    因此引入了slots概念，slots指向vm的stack中的一个位置，我们在每次调用前更新这个值，这个位置恰好是函数调用开始的位置，
    此后函数内部的变量都根据slots这个位置来计算偏移量，这样才能保证取到正确的对应变量
   */
  Value* slots;
} CallFrame;

typedef struct {
  // return address: 利用frames数组的形式记录函数调用的层级关系，当某个层级的函数帧结束之后，就可以立马退出到上一个层级
  CallFrame frames[FRAMES_MAX];
  // 此时的函数调用栈深度
  int frameCount;
  
  // 运行时参数内存栈
  Value stack[STACK_MAX];
  // 栈顶，默认指向下一个需要存储的位置
  Value* stackTop;
  // 堆内存，用于内存回收
  Obj* objects; 
  // openUpvalues链表的head, 用于追踪upValues，来确保唯一性
  ObjUpvalue* openUpvalues;
  // 用于存储用户所定义的字符串
  Table strings;
  // 全局变量
  Table globals;

  // 垃圾回收的灰色对象栈，见memory.c
  int grayCount;
  int grayCapacity;
  Obj** grayStack;

  // vm总共分配的堆内存
  size_t bytesAllocated;
  //
  size_t nextGC;
} VM;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);

extern VM vm;

// 入栈
void push(Value value);
// 出栈
Value pop();
// 取距离栈顶distance的数据
Value peek(int distance);

# endif