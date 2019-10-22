#include <stdio.h>                                     

#include "debug.h"
#include "value.h"

// single byte instruction, like: 'OP_RETURN'
static int simpleInstruction(const char* name, int offset) {
  printf("%s\n", name);                                     
  return offset + 1;                                        
}

static int constantInstruction(const char* name, Chunk* chunk,
                               int offset) {                  
  uint8_t constant = chunk->code[offset + 1];

  // name and index of constant                
  printf("%-16s %4d '", name, constant);                     
  printValue(chunk->constants.values[constant]);         
  printf("'\n");
  return offset + 2;                                              
}

void disassembleChunk(Chunk* chunk, const char* name) {
  // show debug title
  printf("== %s ==\n", name);                          

  for (int offset = 0; offset < chunk->count;) {
    // offset: every instruction offset.
    offset = disassembleInstruction(chunk, offset);    
  }                                         
}

// show every instruction
int disassembleInstruction(Chunk* chunk, int offset) {
  printf("%04d ", offset);

  // Show a | for any instruction that comes from the same source line as the preceding one.
  if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
    printf("   | ");                                                   
  } else {                                                             
    printf("%4d ", chunk->lines[offset]);                              
  }

  // read instruction
  uint8_t instruction = chunk->code[offset];          
  switch (instruction) {
    case OP_RETURN:
      return simpleInstruction("OP_RETURN", offset);
    case OP_CONSTANT:
      return constantInstruction("OP_CONSTANT", chunk, offset);
    default:                                          
      printf("Unknown opcode %d\n", instruction);     
      return offset + 1;                    
  }                                                   
}
  