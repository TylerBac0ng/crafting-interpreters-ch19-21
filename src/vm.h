#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "table.h"
#include "value.h"

#define STACK_MAX 256

typedef struct {
  Chunk* chunk;
  uint8_t* ip;
  Value stack[STACK_MAX];
  Value* stackTop;
#ifdef GLOBAL_TABLE
  Table globals;
#else
  // 21.2: the compiler gives each global name a slot. globalNames maps the
  // name to its slot number, and globalValues holds the values, with
  // EMPTY_VAL in a slot that isn't defined yet. globalIdentifiers has the
  // name for each slot, for error messages.
  Table globalNames;
  ValueArray globalValues;
  ValueArray globalIdentifiers;
#endif
  Table strings;
  Obj* objects;
#ifdef CONSTANT_STRINGS
  struct KeptSource* sources;
#endif
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void initVM();
void freeVM();
InterpretResult interpret(const char* source);
InterpretResult runChunk(Chunk* chunk);
// 19.2: takes ownership of a heap allocated source string and frees it in
// freeVM(), since constant strings can point into it.
void keepSource(char* source);
void push(Value value);
Value pop();

#endif
