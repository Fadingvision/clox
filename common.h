#ifndef clox_common_h
#define clox_common_h

// vm执行字节码debug
// #define DEBUG_TRACE_EXECUTION
// 编译字节码debug
#define DEBUG_PRINT_CODE
// 垃圾回收执行时机策略debug
// #define DEBUG_STRESS_GC
// 内存分配和回收打印debug
// #define DEBUG_LOG_GC

#define UINT8_COUNT (UINT8_MAX + 1)

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#endif