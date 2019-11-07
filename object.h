#ifndef clox_object_h
#define clox_object_h

#include "common.h"
#include "value.h"

// 获取对应的类型
#define OBJ_TYPE(value) (AS_OBJ(value)->type)

// 判断类型
#define IS_STRING(value)  isObjType(value, OBJ_STRING)

// 对Obj进行断言得到ObjString
#define AS_STRING(value)        ((ObjString*)AS_OBJ(value))
// 获取chars字符串
#define AS_CSTRING(value)       (((ObjString*)AS_OBJ(value))->chars)

// 对象的类型
typedef enum {
  OBJ_STRING,
} ObjType;

// 相当于对象的base class，每个obj都有一个类型
struct sObj {
  ObjType type;
  // 单链表，指向下一个sObj
  struct sObj* next;
};

// 字类：字符串对象



/*
  NOTE: 
  1. struct嵌套实现类似继承的功能
  C的机制中由于指向sObjString的指针也指向Obj的内存地址
  这样 sObjString* 和 Obj*之间就能互相断言

  2. Flexible array member:
  @refer: https://riptutorial.com/c/example/10850/flexible-array-members
  为了避免重复的给每个字符串对象分配两个分开的独立的内存(一块给ObjString, 一块给chars),
  这里使用c99的flexible array member方法来将ObjString和它的char字符串存储在一块联系的内存空间
*/
struct sObjString {
  Obj obj; // 基本类型
  int length; // 字符串长度
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

void printObject(Value value);


#endif
