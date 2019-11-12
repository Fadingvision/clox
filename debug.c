#include <stdio.h> 
#include <string.h> 

#include "debug.h"
#include "value.h"

// single byte instruction, like: 'OP_RETURN'
static int simpleInstruction(const char* name, int offset) {
  printf("%s\n", name);
  return offset + 1;
}

// 有附加参数的指令
static int constantInstruction(const char* name, Chunk* chunk,
    int offset) {
  uint8_t constant = chunk->code[offset + 1];

  // name and index of constant 
  printf("%-16s %4d '", name, constant);
  printValue(chunk->constants.values[constant]);
  printf("'\n");
  return offset + 2;
}

static int constantLongInstruction(const char* name, Chunk* chunk,
    int offset) {
  int constant = 0;
  // 如何在连续内存（offset+1 -> offset + 3）上读出256？
  memcpy(&constant, &chunk->code[offset + 1], sizeof(uint8_t) * 3);
  // uint8_t constant = chunk->code[offset + 1];

  // name and index of constant 
  printf("%-16s %4d '", name, constant);
  printValue(chunk->constants.values[constant]);
  printf("'\n");
  return offset + 4;
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
    case OP_ADD:     
      return simpleInstruction("OP_ADD", offset);
    case OP_SUBTRACT:
      return simpleInstruction("OP_SUBTRACT", offset);
    case OP_MULTIPLY:
      return simpleInstruction("OP_MULTIPLY", offset);
    case OP_DIVIDE:  
      return simpleInstruction("OP_DIVIDE", offset);
    case OP_NEGATE:
      return simpleInstruction("OP_NEGATE", offset);
    case OP_NOT:
      return simpleInstruction("OP_NOT", offset);
    case OP_RETURN:
      return simpleInstruction("OP_RETURN", offset);
    case OP_CONSTANT_LONG:
      return constantLongInstruction("OP_CONSTANT_LONG", chunk, offset);
    case OP_CONSTANT:
      return constantInstruction("OP_CONSTANT", chunk, offset);
    case OP_NIL:
      return simpleInstruction("OP_NIL", offset);
    case OP_FALSE:
      return simpleInstruction("OP_FALSE", offset);
    case OP_TRUE:
      return simpleInstruction("OP_TRUE", offset);
    case OP_EQUAL:
      return simpleInstruction("OP_EQUAL", offset);
    case OP_GREATER:
      return simpleInstruction("OP_GREATER", offset);
    case OP_LESS:
      return simpleInstruction("OP_LESS", offset);
    case OP_PRINT:
      return simpleInstruction("OP_PRINT", offset);
    case OP_DEFINE_GLOBAL:
      return constantInstruction("OP_DEFINE_GLOBAL", chunk, offset);
    case OP_SET_GLOBAL:
      return constantInstruction("OP_SET_GLOBAL", chunk, offset);
    case OP_GET_GLOBAL:
      return constantInstruction("OP_GET_GLOBAL", chunk, offset);
    case OP_POP:
      return simpleInstruction("OP_POP", offset);
    default:
      printf("Unknown opcode %d\n", instruction);     
      return offset + 1;  
  }
}
  