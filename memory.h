#ifndef clox_memory_h                    
#define clox_memory_h                    

#include "object.h"

#define GROW_CAPACITY(capacity) \
  ((capacity) < 8 ? 8 : (capacity) * 2)

#define FREE(type, pointer) \
    reallocate(pointer, sizeof(type), 0)

#define FREE_ARRAY(type, pointer, oldCount) \
  reallocate(pointer, sizeof(type) * (oldCount), 0)

#define GROW_ARRAY(previous, type, oldCount, count) \
  (type*)reallocate(previous, sizeof(type) * (oldCount), \
    sizeof(type) * (count))

#define ALLOCATE(type, count) \
    (type*)reallocate(NULL, 0, sizeof(type) * (count))

void* reallocate(void* previous, size_t oldSize, size_t newSize);
void freeObjects();

#endif