#ifndef clox_chunk_h
#define clox_chunk_h

#include "common.h"
#include "value.h"

// With MINIMAL_INSTRUCTIONS (18.1), the instructions the compiler can do
// without are left out of the enum, so any code that still uses them fails
// to compile.
typedef enum {
  OP_CONSTANT,
#ifndef MINIMAL_INSTRUCTIONS
  OP_NIL,
  OP_TRUE,
  OP_FALSE,
#endif
  OP_EQUAL,
#ifndef MINIMAL_INSTRUCTIONS
  OP_GREATER,
#endif
  OP_LESS,
#ifndef MINIMAL_INSTRUCTIONS
  // Single instructions for !=, >= and <= (18.2).
  OP_NOT_EQUAL,
  OP_GREATER_EQUAL,
  OP_LESS_EQUAL,
#endif
  OP_ADD,
#ifndef MINIMAL_INSTRUCTIONS
  OP_SUBTRACT,
#endif
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_NOT,
#ifndef MINIMAL_INSTRUCTIONS
  OP_NEGATE,
#endif
  // For the conditional operator (17.3).
  OP_POP,
  OP_PRINT,
  // By default the operand of these is a two byte slot number (21.2). With
  // GLOBAL_TABLE it is a one byte constant holding the variable's name.
  OP_DEFINE_GLOBAL,
  OP_GET_GLOBAL,
  OP_SET_GLOBAL,
  OP_JUMP,
  OP_JUMP_IF_FALSE,
  OP_RETURN,
} OpCode;

typedef struct {
  int count;
  int capacity;
  uint8_t* code;
  int* lines;
  ValueArray constants;
} Chunk;

void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line);
int addConstant(Chunk* chunk, Value value);

#endif
