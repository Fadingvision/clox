#ifndef clox_common_h
#define clox_common_h

// vm执行字节码debug
// #define DEBUG_TRACE_EXECUTION

// 编译字节码debug
// #define DEBUG_PRINT_CODE

// 垃圾回收执行时机策略debug
// #define DEBUG_STRESS_GC

// 内存分配和回收打印debug
// #define DEBUG_LOG_GC

// 开启NAN优化策略，由于这项优化策略在部分机器上可能不支持，
// 因此定义为一个条件的宏，以便向后兼容 
#define NAN_BOXING

#define UINT8_COUNT (UINT8_MAX + 1)

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#endif