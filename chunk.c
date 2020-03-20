#include <stdlib.h>

#include "chunk.h"
#include "memory.h"
#include "vm.h"
#include "value.h"

void initChunk(Chunk* chunk) {
  chunk->count = 0;   
  chunk->capacity = 0;
  chunk->code = NULL;
  chunk->lines = NULL;
  initValueArray(&chunk->constants);
}

void writeChunk(Chunk* chunk, uint8_t byte, int line) { 
  if (chunk->capacity < chunk->count + 1) {
    int oldCapacity = chunk->capacity;    
    chunk->capacity = GROW_CAPACITY(oldCapacity); 
    chunk->code = GROW_ARRAY(chunk->code, uint8_t,
      oldCapacity, chunk->capacity);
    chunk->lines = GROW_ARRAY(chunk->lines, int,
      oldCapacity, chunk->capacity);
  }

  chunk->code[chunk->count] = byte;
  chunk->lines[chunk->count] = line;
  chunk->count++;
}

void freeChunk(Chunk* chunk) {      
  FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
  FREE_ARRAY(int, chunk->lines, chunk->capacity);
  freeValueArray(&chunk->constants);
  initChunk(chunk);
}

int addConstant(Chunk* chunk, Value value) {
  // GC的边界情况：因为在writeValueArray中，正式将Value写入constants数组之前，
  // 如果在写入数组之前发现constants数组空间不足，需要重新分配空间
  // 这个时候就有可能触发一次垃圾回收，但这个时候value是一个没有宿主的情况，垃圾回收就会将其回收掉
  // 那么value就会变成一个空值，从而引起bug,
  // 因此我们简单的将其出入栈，保持其引用，使其不会被垃圾回收所回收
  push(value);
  writeValueArray(&chunk->constants, value);
  pop();
  return chunk->constants.count - 1;
}

// void writeConstant(Chunk* chunk, Value value, int line) {
//   writeValueArray(&chunk->constants, value);
//   int index = chunk->constants.count - 1;
//   if (index <= 255) {
//     writeChunk(chunk, OP_CONSTANT, line);
//     writeChunk(chunk, index, line);
//   } else {
//     writeChunk(chunk, OP_CONSTANT_LONG, line);
//     if (chunk->capacity < chunk->count + 3) {
//       int oldCapacity = chunk->capacity;
//       chunk->capacity = GROW_CAPACITY(oldCapacity); 
//       chunk->code = GROW_ARRAY(chunk->code, uint8_t,
//         oldCapacity, chunk->capacity);
//       chunk->lines = GROW_ARRAY(chunk->lines, int,
//         oldCapacity, chunk->capacity);
//     }

//     // 在连续内存（count -> count + 2）上写入 256？
//     // chunk->code[chunk->count] = index;
//     chunk->code[chunk->count] = index & 0xff;
//     chunk->code[chunk->count + 1] = (index>>8)  & 0xff;
//     chunk->code[chunk->count + 2] = (index>>16)  & 0xff;

//     chunk->lines[chunk->count] = line;
//     chunk->count += 3;
//   }
// }

