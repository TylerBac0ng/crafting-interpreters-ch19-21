#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "vm.h"

VM vm; // [one]
static void resetStack() {
  vm.stackTop = vm.stack;
}
static void runtimeError(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  size_t instruction = vm.ip - vm.chunk->code - 1;
  int line = vm.chunk->lines[instruction];
  fprintf(stderr, "[line %d] in script\n", line);
  resetStack();
}

#ifdef CONSTANT_STRINGS
typedef struct KeptSource {
  struct KeptSource* next;
  char* source;
} KeptSource;
#endif

void keepSource(char* source) {
#ifdef CONSTANT_STRINGS
  KeptSource* kept = malloc(sizeof(KeptSource));
  if (kept == NULL) exit(1);
  kept->source = source;
  kept->next = vm.sources;
  vm.sources = kept;
#else
  free(source);
#endif
}

void initVM() {
  resetStack();
  vm.objects = NULL;
#ifdef GLOBAL_TABLE
  initTable(&vm.globals);
#else
  initTable(&vm.globalNames);
  initValueArray(&vm.globalValues);
  initValueArray(&vm.globalIdentifiers);
#endif
  initTable(&vm.strings);
#ifdef CONSTANT_STRINGS
  vm.sources = NULL;
#endif
}

void freeVM() {
#ifdef GLOBAL_TABLE
  freeTable(&vm.globals);
#else
  freeTable(&vm.globalNames);
  freeValueArray(&vm.globalValues);
  freeValueArray(&vm.globalIdentifiers);
#endif
  freeTable(&vm.strings);
  freeObjects();
#ifdef CONSTANT_STRINGS
  while (vm.sources != NULL) {
    KeptSource* next = vm.sources->next;
    free(vm.sources->source);
    free(vm.sources);
    vm.sources = next;
  }
#endif
}
void push(Value value) {
  *vm.stackTop = value;
  vm.stackTop++;
}
Value pop() {
  vm.stackTop--;
  return *vm.stackTop;
}
static Value peek(int distance) {
  return vm.stackTop[-1 - distance];
}
static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}
static void concatenate() {
  ObjString* b = AS_STRING(pop());
  ObjString* a = AS_STRING(pop());

  int length = a->length + b->length;
  ObjString* result = makeString(length);
  char* chars = stringBuffer(result);
  memcpy(chars, a->chars, a->length);
  memcpy(chars + a->length, b->chars, b->length);
  push(OBJ_VAL(finishString(result)));
}
#ifndef GLOBAL_TABLE
// The name of the global in a slot, for an error message.
static ObjString* globalName(int slot) {
  return AS_STRING(vm.globalIdentifiers.values[slot]);
}
#endif
static InterpretResult run() {
#define READ_BYTE() (*vm.ip++)
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define READ_SHORT() \
    (vm.ip += 2, (uint16_t)((vm.ip[-2] << 8) | vm.ip[-1]))
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define BINARY_OP(valueType, op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        runtimeError("Operands must be numbers."); \
        return INTERPRET_RUNTIME_ERROR; \
      } \
      double b = AS_NUMBER(pop()); \
      double a = AS_NUMBER(pop()); \
      push(valueType(a op b)); \
    } while (false)

  for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("          ");
    for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
      printf("[ ");
      printValue(*slot);
      printf(" ]");
    }
    printf("\n");
    disassembleInstruction(vm.chunk,
                           (int)(vm.ip - vm.chunk->code));
#endif

    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);
        break;
      }
#ifndef MINIMAL_INSTRUCTIONS
      case OP_NIL: push(NIL_VAL); break;
      case OP_TRUE: push(BOOL_VAL(true)); break;
      case OP_FALSE: push(BOOL_VAL(false)); break;
#endif
      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(valuesEqual(a, b)));
        break;
      }
#ifndef MINIMAL_INSTRUCTIONS
      case OP_NOT_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(BOOL_VAL(!valuesEqual(a, b)));
        break;
      }
      case OP_GREATER:       BINARY_OP(BOOL_VAL, >); break;
      case OP_GREATER_EQUAL: BINARY_OP(BOOL_VAL, >=); break;
      case OP_LESS_EQUAL:    BINARY_OP(BOOL_VAL, <=); break;
#endif
      case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
      case OP_ADD: {
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          concatenate();
        } else if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
          double b = AS_NUMBER(pop());
          double a = AS_NUMBER(pop());
          push(NUMBER_VAL(a + b));
        } else {
          runtimeError("Operands must be two numbers or two strings.");
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
#ifndef MINIMAL_INSTRUCTIONS
      case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
#endif
      case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
      case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;
      case OP_NOT:
        push(BOOL_VAL(isFalsey(pop())));
        break;
#ifndef MINIMAL_INSTRUCTIONS
      case OP_NEGATE:
        if (!IS_NUMBER(peek(0))) {
          runtimeError("Operand must be a number.");
          return INTERPRET_RUNTIME_ERROR;
        }
        push(NUMBER_VAL(-AS_NUMBER(pop())));
        break;
#endif
      case OP_POP: pop(); break;
      case OP_PRINT: {
        printValue(pop());
        printf("\n");
        break;
      }
#ifdef GLOBAL_TABLE
      case OP_DEFINE_GLOBAL: {
        ObjString* name = READ_STRING();
        tableSet(&vm.globals, OBJ_VAL(name), peek(0));
        pop();
        break;
      }
      case OP_GET_GLOBAL: {
        ObjString* name = READ_STRING();
        Value value;
        if (!tableGet(&vm.globals, OBJ_VAL(name), &value)) {
          runtimeError("Undefined variable '%.*s'.", name->length, name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        push(value);
        break;
      }
      case OP_SET_GLOBAL: {
        ObjString* name = READ_STRING();
        if (tableSet(&vm.globals, OBJ_VAL(name), peek(0))) {
          tableDelete(&vm.globals, OBJ_VAL(name));
          runtimeError("Undefined variable '%.*s'.", name->length, name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
#else
      // 21.2: the slot was found at compile time, so no hashing happens here.
      // The only runtime work left is checking that the slot has a value.
      case OP_DEFINE_GLOBAL: {
        vm.globalValues.values[READ_SHORT()] = pop();
        break;
      }
      case OP_GET_GLOBAL: {
        uint16_t slot = READ_SHORT();
        Value value = vm.globalValues.values[slot];
        if (IS_EMPTY(value)) {
          ObjString* name = globalName(slot);
          runtimeError("Undefined variable '%.*s'.", name->length, name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        push(value);
        break;
      }
      case OP_SET_GLOBAL: {
        uint16_t slot = READ_SHORT();
        if (IS_EMPTY(vm.globalValues.values[slot])) {
          ObjString* name = globalName(slot);
          runtimeError("Undefined variable '%.*s'.", name->length, name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        vm.globalValues.values[slot] = peek(0);
        break;
      }
#endif
      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        vm.ip += offset;
        break;
      }
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if (isFalsey(peek(0))) vm.ip += offset;
        break;
      }
      case OP_RETURN: {
        // Exit interpreter.
        return INTERPRET_OK;
      }
    }
  }

#undef READ_BYTE
#undef READ_CONSTANT
#undef READ_SHORT
#undef READ_STRING
#undef BINARY_OP
}
InterpretResult runChunk(Chunk* chunk) {
  vm.chunk = chunk;
  vm.ip = vm.chunk->code;
  return run();
}
InterpretResult interpret(const char* source) {
  Chunk chunk;
  initChunk(&chunk);

  if (!compile(source, &chunk)) {
    freeChunk(&chunk);
    return INTERPRET_COMPILE_ERROR;
  }

  InterpretResult result = runChunk(&chunk);

  freeChunk(&chunk);
  return result;
}
