#include "common.h"
#include "chunk.h"
#include "debug.h"

int main(int argc, const char* argv[]) {
  Chunk chunk;                          
  initChunk(&chunk);

  int constant = addConstant(&chunk, 1.2);
  // Write first instruction: constant                  
  writeChunk(&chunk, OP_CONSTANT, 222);        
  writeChunk(&chunk, constant, 222);
  constant = addConstant(&chunk, 2.35);
  writeChunk(&chunk, OP_CONSTANT, 223);        
  writeChunk(&chunk, constant, 223);

  writeChunk(&chunk, OP_RETURN, 224);

  disassembleChunk(&chunk, "test chunk");

  freeChunk(&chunk);
  return 0;
}
