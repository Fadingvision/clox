#include "common.h"
#include "chunk.h"
#include "vm.h"
#include "debug.h"

int main(int argc, const char* argv[]) {
  Chunk chunk;
  initVM();                   
  initChunk(&chunk);

  // int constant = addConstant(&chunk, 1.2);
  // // Write first instruction: constant                  
  // writeChunk(&chunk, OP_CONSTANT, 222);        
  // // constant的操作数最多只存一个字节，因此最多能保存256(0 - 255)个constant
  // writeChunk(&chunk, constant, 222);

  // 1 + 2 * 3 - 4 / -5
  writeConstant(&chunk, 1, 222);
  writeConstant(&chunk, 2, 222);
  writeConstant(&chunk, 3, 222);
  writeChunk(&chunk, OP_MULTIPLY, 222);
  writeChunk(&chunk, OP_ADD, 222);
  writeConstant(&chunk, 4, 222);
  writeConstant(&chunk, 5, 222);
  writeChunk(&chunk, OP_NEGATE, 224);
  writeChunk(&chunk, OP_DIVIDE, 224);
  writeChunk(&chunk, OP_SUBTRACT, 224);

  writeChunk(&chunk, OP_RETURN, 224);

  // executing instructions
  interpret(&chunk);

  freeVM();
  freeChunk(&chunk);
  return 0;
}
