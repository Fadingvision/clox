#include <stdlib.h>

#include "chunk.h"
#include "memory.h"
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
  writeValueArray(&chunk->constants, value);
  return chunk->constants.count - 1;
}

void writeConstant(Chunk* chunk, Value value, int line) {
  writeValueArray(&chunk->constants, value);
  int index = chunk->constants.count - 1;
  if (index <= 255) {
    writeChunk(chunk, OP_CONSTANT, line);
    writeChunk(chunk, index, line);
  } else {
    writeChunk(chunk, OP_CONSTANT_LONG, line);
    if (chunk->capacity < chunk->count + 3) {
      int oldCapacity = chunk->capacity;
      chunk->capacity = GROW_CAPACITY(oldCapacity); 
      chunk->code = GROW_ARRAY(chunk->code, uint8_t,
        oldCapacity, chunk->capacity);
      chunk->lines = GROW_ARRAY(chunk->lines, int,
        oldCapacity, chunk->capacity);
    }

    // 在连续内存（count -> count + 2）上写入 256？
    // chunk->code[chunk->count] = index;
    chunk->code[chunk->count] = index & 0xff;
    chunk->code[chunk->count + 1] = (index>>8)  & 0xff;
    chunk->code[chunk->count + 2] = (index>>16)  & 0xff;

    chunk->lines[chunk->count] = line;
    chunk->count += 3;
  }
}

