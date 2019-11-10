#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"
#include "table.h"

#define ALLOCATE_OBJ(type, objectType) \
    (type*)allocateObject(sizeof(type), objectType)

static Obj* allocateObject(size_t size, ObjType type) {
  Obj* object = (Obj*)reallocate(NULL, 0, size);
  object->type = type;

  // 每次分配一个对象的内存，将其放入链表的头部
  object->next = vm.objects;
  vm.objects = object;

  return object;
}

// 字符串hash方法
static uint32_t hashString(const char* key, int length) {
  uint32_t hash = 2166136261u;

  for (int i = 0; i < length; i++) {
    hash ^= key[i];
    hash *= 16777619;
  }

  return hash;
}

// 分配一块内存空间以存储ObjString对象
static ObjString* allocateString(char* chars, int length) {
  ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
  // 将其chars指向chars
  string->chars = chars;
  string->hash = hashString(chars, length);
  // 每次初始化string的时候，都将strings收集到hashTable中
  tableSet(&vm.strings, string, NIL_VAL);
  string->length = length;

  return string;
}

ObjString* takeString(char* chars, int length) {
  return allocateString(chars, length);
}

// 分配一块内存空间以复制chars
ObjString* copyString(const char* chars, int length) {
  // 由于在lox中，字符串是不可变的，因此将所有的字符串对象保存在内存中，以便复用
  // 每当生成一个新的字符串对象时，检查table中是否有该字符串，如果有就复用
  ObjString* interned = tableFindString(&vm.strings, chars, length, hash);
  if (interned != NULL) {
    FREE_ARRAY(char, chars, length + 1);
    return interned;
  }


  // 分配一块sizeof(char) * (length + 1)大小的内存空间
  char* heapChars = ALLOCATE(char, length + 1);
  memcpy(heapChars, chars, length);
  // 将heapChars转为C类型的chars(带有\0的终止符)
  heapChars[length] = '\0';

  return allocateString(heapChars, length);
}

void printObject(Value value) {
  switch (OBJ_TYPE(value)) {
  case OBJ_STRING:
    printf("%s", AS_CSTRING(value));
    break;
  default:
    break;
  }
}
