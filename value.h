#ifndef clox_value_h 
#define clox_value_h 

#include "common.h"

typedef struct sObj Obj;
typedef struct sObjString ObjString;


/* 
  在原始的Value定义中，它包含了一个8字节的Union类型，4字节的type tag, 编译器为了对齐会在中间加上4个字节的padding.
  这样整个Value值占据了16个字节，这样当程序中的Value变多之后，就会消耗大量的内存占用.

  为了优化这一点我们利用一种叫做`NaN boxing`的方法。原理如下：
  在我们的lox程序中和js一样，只有一种数字类型，那就是float64.
  在IEEE5754的浮点数标准中，一个浮点数的二进制表示为 1位正负标志位 + 11位指数位 + 52位分数位，
  当 11位指数位全部置为1的时候，这个数字就代表了一个叫做 NaN (not a number)的值，通常用来表示 无限大或者分母为0的 概念

  在这些NaN中，根据分数位的最高位的不同又分为：`signalling NaNs`(最高位为0) 和 `quiet NaNs`(最高位为1).

  signalling NaNs通常被程序内部使用，用来告诉用户产生了错误的计算结果，例如0为分母的值。

  我们利用的就是其中的 quiet NaNs. 除掉 11位指数位和分数位的最高位和次高位(用于避免 `QNan Floating-Point Indefinite`),
  我们一共有51位可以使用，我们可以完美的利用这个空间来存储 nil, true, false, object的指针地址(理论上这个地址是64位，但在实际应用中，绝大多数的机器都只会使用48位低位) 这些值。
  48位地址足够存储262,144 GB的内存，这就足够了。

  这种方法的好处之一就是不用再对数字类型进行任何的类型转换，因为value类型的本质就是double。

 */
#ifdef NAN_BOXING
  #define QNAN     ((uint64_t)0x7ffc000000000000)
  #define SIGN_BIT ((uint64_t)0x8000000000000000)
  #define TAG_NIL   1 // 01.                     
  #define TAG_FALSE 2 // 10.                     
  #define TAG_TRUE  3 // 11.

  typedef uint64_t Value;

  #define NIL_VAL         ((Value)(uint64_t)(QNAN | TAG_NIL))
  #define FALSE_VAL       ((Value)(uint64_t)(QNAN | TAG_FALSE))
  #define TRUE_VAL        ((Value)(uint64_t)(QNAN | TAG_TRUE))
  #define BOOL_VAL(b)     ((b) ? TRUE_VAL : FALSE_VAL)
  #define OBJ_VAL(obj) \
    (Value)(SIGN_BIT | QNAN | (uint64_t)(uintptr_t)(obj))
  #define NUMBER_VAL(num) numToValue(num)

  typedef union {
    uint64_t bits;
    double num;
  } DoubleUnion;

  // 利用DoubleUnion来进行double类型和uint_64之间的互相转换，
  static inline Value numToValue(double num) {
    DoubleUnion data;
    data.num = num;
    return data.bits;
  }

  #define AS_OBJ(v)       ((Obj*)(uintptr_t)((v) & ~(SIGN_BIT | QNAN)))
  #define AS_BOOL(v)      ((v) == TRUE_VAL)
  #define AS_NUMBER(v)    valueToNum(v)
  static inline double valueToNum(Value value) {
    DoubleUnion data;                           
    data.bits = value;                          
    return data.num;                            
  }

  #define IS_NUMBER(v)    (((v) & QNAN) != QNAN)
  #define IS_NIL(v)       ((v) == NIL_VAL)
  #define IS_BOOL(v)      (((v) & FALSE_VAL) == FALSE_VAL)
  #define IS_OBJ(v)       (((v) & (QNAN | SIGN_BIT)) == (QNAN | SIGN_BIT))

#else
  typedef enum {
    VAL_BOOL,
    VAL_NIL,
    VAL_NUMBER,
    VAL_OBJ,
  } ValueType;

  typedef struct {
    ValueType type;
    // 为了节约内存空间，使用布尔值和数字联合类型，他们共同分享八个字节的内存空间，但是不能同时存在，因此需要type区分
    union {
      bool boolean;
      double number;
      // obj用来存储字符串，函数，类等占用大内存的元素，这种内存空间叫做堆(heap)
      // 而栈中保存一个指向堆内存的内存地址
      Obj* obj;
    } as;
  } Value;

  // 判断字节码的值是否是对应的类型，每次在取值之间必须进行类型判断，
  // 防止错误的将布尔类型的值解释为数字类型，或者反之
  #define IS_BOOL(value)    ((value).type == VAL_BOOL)
  #define IS_NIL(value)     ((value).type == VAL_NIL)
  #define IS_NUMBER(value)  ((value).type == VAL_NUMBER)
  #define IS_OBJ(value)     ((value).type == VAL_OBJ)

  // 将普通c的类型的value转为clox的Value类型，方便作为字节码存储
  #define BOOL_VAL(value)   ((Value){ VAL_BOOL, { .boolean = value } })
  #define NIL_VAL           ((Value){ VAL_NIL, { .number = 0 } })
  #define NUMBER_VAL(value) ((Value){ VAL_NUMBER, { .number = value } })
  #define OBJ_VAL(object)   ((Value){ VAL_OBJ, { .obj = (Obj*)object } })

  // 将字节码中的值转为c对应的类型，方便虚拟机执行
  #define AS_BOOL(value)    ((value).as.boolean)
  #define AS_NUMBER(value)  ((value).as.number)
  #define AS_OBJ(value)     ((value).as.obj)
#endif

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