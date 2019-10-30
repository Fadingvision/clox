#ifndef clox_chunk_h     
#define clox_chunk_h

#include "common.h"
#include "value.h"

typedef enum {
  // op-constant
  OP_CONSTANT,
  OP_CONSTANT_LONG,
  // Types of Values literal-ops
  OP_NIL,
  OP_TRUE,
  OP_FALSE,
  // Global Variables pop-op
  OP_POP,
  // Local Variables get-local-op
  OP_GET_LOCAL,
  // Local Variables set-local-op
  OP_SET_LOCAL,
  // Global Variables get-global-op
  OP_GET_GLOBAL,
  // Global Variables define-global-op
  OP_DEFINE_GLOBAL,
  // Global Variables set-global-op
  OP_SET_GLOBAL,
  // Closures upvalue-ops
  OP_GET_UPVALUE,
  OP_SET_UPVALUE,
  // Classes and Instances not-yet
  OP_GET_PROPERTY,
  OP_SET_PROPERTY,
  // Superclasses not-yet
  OP_GET_SUPER,
  // Types of Values comparison-ops
  OP_EQUAL,
  OP_GREATER,
  OP_LESS,
  // A Virtual Machine binary-ops
  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
  // Types of Values not-op
  OP_NOT,
  // A Virtual Machine negate-op
  OP_NEGATE,
  // Global Variables op-print
  OP_PRINT,
  // Jumping Back and Forth jump-op
  OP_JUMP,
  // Jumping Back and Forth jump-if-false-op
  OP_JUMP_IF_FALSE,
  // Jumping Back and Forth loop-op
  OP_LOOP,
  // Calls and Functions op-call
  OP_CALL,
  // Methods and Initializers not-yet
  OP_INVOKE,
  // Superclasses not-yet
  OP_SUPER,
  // Closures closure-op
  OP_CLOSURE,
  // Closures close-upvalue-op
  OP_CLOSE_UPVALUE,
  OP_RETURN,
  // Classes and Instances not-yet
  OP_CLASS,
  // Superclasses not-yet
  OP_INHERIT,
  // Methods and Initializers not-yet
  OP_METHOD
} OpCode;

// Dynamic Array
typedef struct {
  int count;
  int capacity;
  uint8_t* code;
  int* lines;
  ValueArray constants;
} Chunk;  

void initChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
void writeConstant(Chunk* chunk, Value value, int line);
void freeChunk(Chunk* chunk);
int addConstant(Chunk* chunk, Value value);

#endif