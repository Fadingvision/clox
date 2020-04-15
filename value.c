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
  #ifdef NAN_BOXING                             
    if (IS_BOOL(value)) {                       
      printf(AS_BOOL(value) ? "true" : "false");
    } else if (IS_NIL(value)) {                 
      printf("nil");                            
    } else if (IS_NUMBER(value)) {              
      printf("%g", AS_NUMBER(value));           
    } else if (IS_OBJ(value)) {                 
      printObject(value);                       
    }                                           
  #else
    switch (value.type) {
      case VAL_BOOL:   printf(AS_BOOL(value) ? "true" : "false"); break;
      case VAL_NIL:    printf("nil"); break;
      case VAL_NUMBER: printf("%f", AS_NUMBER(value)); break;
      case VAL_OBJ:    printObject(value); break;
    }
  #endif
}

// 为什么不直接用内存比较：由于各个平台在存储Struct的时候内存偏移量不一致
bool isEuqal(Value a, Value b) {
  #ifdef NAN_BOXING
    // 这里为了实现IEEE754标准的NaN !== NaN; 也就是说即使两个底层相同的位的NaN数字也不能被视为相等。
    // c中的double类型就实现了IEEE754, 我们需要将数字转为double类型进行对比既可
    if (IS_NUMBER(a) && IS_NUMBER(b)) return AS_NUMBER(a) == AS_NUMBER(b);
    // 由于我们的Value被表示成了uint64_t类型，所以只需要直接比较该值既可判断是否相等
    return a == b;
  #else
    // 这里使用类型和值进行比较
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
  #endif
}
