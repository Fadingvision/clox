#include <stdio.h> 
#include <string.h> 

#include "object.h"
#include "memory.h"
#include "value.h"

void initValueArray(ValueArray* array) {
  array->values = NULL;                 
  array->capacity = 0;                  
  array->count = 0;
}

void writeValueArray(ValueArray* array, Value value) {       
  if (array->capacity < array->count + 1) {                  
    int oldCapacity = array->capacity;  
    array->capacity = GROW_CAPACITY(oldCapacity);            
    array->values = GROW_ARRAY(array->values, Value,         
      oldCapacity, array->capacity);
  }

  array->values[array->count] = value;
  array->count++;
}

void freeValueArray(ValueArray* array) {            
  FREE_ARRAY(Value, array->values, array->capacity);
  initValueArray(array);       
}

void printValue(Value value) {
  switch (value.type) {
    case VAL_BOOL:   printf(AS_BOOL(value) ? "true" : "false"); break;
    case VAL_NIL:    printf("nil"); break;
    case VAL_NUMBER: printf("%f", AS_NUMBER(value)); break;
    case VAL_OBJ:    printObject(value); break;
  }
}

// 为什么不直接用内存比较：由于各个平台在存储Struct的时候内存偏移量不一致
// 这里使用类型和值进行比较
bool isEuqal(Value a, Value b) {
  if (a.type != b.type) return false;
  switch (a.type) {
    case VAL_BOOL:   return AS_BOOL(a) == AS_BOOL(b);
    case VAL_NIL:    return true;
    case VAL_NUMBER: return AS_NUMBER(a) == AS_NUMBER(b);
    case VAL_OBJ: {
      // 由于同一个字符串的对象被收集到对象池中，因此只要内存地址相同，则一定是同一个字符串
      return AS_OBJ(a) == AS_OBJ(b);
    }
  }
}
