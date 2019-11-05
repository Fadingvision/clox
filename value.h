#ifndef clox_value_h 
#define clox_value_h 

#include "common.h"

typedef enum {
  VAL_BOOL,
  VAL_NIL,
  VAL_NUMBER,
} ValueType;

typedef struct {
  ValueType type;
  // 为了节约内存空间，使用布尔值和数字联合类型，他们共同分享八个字节的内存空间，但是不能同时存在，因此需要type区分
  union {
    bool boolean;
    double number;
  } as;
} Value;

// 判断字节码的值是否是对应的类型，每次在取值之间必须进行类型判断，
// 防止错误的将布尔类型的值解释为数字类型，或者反之
#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_NUMBER)

// 将普通c的类型的value转为clox的Value类型，方便作为字节码存储
#define BOOL_VAL(value)   ((Value){ VAL_BOOL, { .boolean = value } })
#define NIL_VAL           ((Value){ VAL_NIL, { .number = 0 } })
#define NUMBER_VAL(value) ((Value){ VAL_NUMBER, { .number = value } })

// 将字节码中的值转为c对应的类型，方便虚拟机执行
#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)

typedef struct {     
  int capacity;
  int count;
  Value* values;
} ValueArray;

void initValueArray(ValueArray* array);              
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValue(Value value);
bool isEuqal(Value a, Value b);

#endif