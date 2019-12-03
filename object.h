#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "value.h"
#include "chunk.h"

// 获取对应的类型
#define OBJ_TYPE(value) (AS_OBJ(value)->type)

// 判断类型
#define IS_STRING(value)    isObjType(value, OBJ_STRING)
#define IS_FUNCTION(value)  isObjType(value, OBJ_FUNCTION)
#define IS_CLOSURE(value)   isObjType(value, OBJ_CLOSURE)
#define IS_NATIVE(value)    isObjType(value, OBJ_NATIVE)

// 对Obj进行断言得到ObjString
#define AS_STRING(value)        ((ObjString*)AS_OBJ(value))
// 获取chars字符串
#define AS_CSTRING(value)       (((ObjString*)AS_OBJ(value))->chars)
// 函数断言
#define AS_FUNCTION(value)       (((ObjFunction*)AS_OBJ(value)))
// 闭包断言
#define AS_CLOSURE(value)       ((ObjClosure*)AS_OBJ(value))
// 内置函数断言
#define AS_NATIVE(value)       (((ObjNative*)AS_OBJ(value))->function)

// 对象的类型
typedef enum {
  OBJ_STRING,
  OBJ_FUNCTION,
  OBJ_CLOSURE,
  OBJ_NATIVE,
} ObjType;

// 相当于对象的base class，每个obj都有一个类型
struct sObj {
  ObjType type;
  // 单链表，指向下一个sObj
  struct sObj* next;
};

// 子类：函数对象
typedef struct {
  Obj obj;
  int arity;        // 函数参数数量
  Chunk chunk;      // 函数体对应的指令集
  ObjString* name;  // 函数名
} ObjFunction;


// 闭包函数
typedef struct {
  Obj obj;
  ObjFunction* function;
} objClosure;

// 定义一个NativeFn类型
// 这个类型为Value func(int argCount, Value* args)这种函数的指针类型
typedef Value (*NativeFn)(int argCount, Value* args);

// 内置函数对象
typedef struct {
  Obj obj;
  NativeFn function;
} ObjNative;


// 子类：字符串对象
/*
  NOTE: 
  1. struct嵌套实现类似继承的功能
  C的机制中由于指向sObjString的指针也指向Obj的内存地址
  这样 sObjString* 和 Obj*之间就能互相断言

  2. Flexible array member:
  @refer: https://riptutorial.com/c/example/10850/flexible-array-members
  为了避免重复的给每个字符串对象分配两个分开的独立的内存(一块给ObjString, 一块给chars),
  这里使用c99的flexible array member方法来将ObjString和它的char字符串存储在一块连续的内存空间
*/
struct sObjString {
  Obj obj; // 基本类型
  int length; // 字符串长度
  uint32_t hash; // 用于缓存字符串的hash值
  char chars[]; // 字符串
};

// 内联函数：函数调用时会被直接替换为函数体，而不是新开一个函数栈
// 从而避免频繁调用函数对栈内存的消耗
// 关键字inline 必须与函数定义体放在一起才能使函数成为内联
static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

// 从chars位置开始复制length长度的字符，并生成ObjString
ObjString* copyString(const char* chars, int length);
// 将给定的chars生成ObjString
ObjString* takeString(char* chars, int length);
// 连接两个字符串对象，生成一个新的字符串对象
ObjString* concatenateString(ObjString*, ObjString*);


// 初始化新的函数对象
ObjFunction* newFunction();
ObjClosure* newClosure(ObjFunction* function);
ObjNative* newNative(NativeFn function);

void printObject(Value value);


#endif
