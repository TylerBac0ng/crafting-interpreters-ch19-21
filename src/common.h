#ifndef clox_common_h
#define clox_common_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Debugging flags, passed with -D (see the Makefile):
//   DEBUG_PRINT_CODE        disassemble each chunk after compiling it
//   DEBUG_TRACE_EXECUTION   print the stack and each instruction as it runs
//   DEBUG_TRACE_PARSE       print each parsing function call (17.1)
//
// Build options, also passed with -D:
//   MINIMAL_INSTRUCTIONS  the reduced instruction set (18.1)
//   CONSTANT_STRINGS      strings that point at the source (19.2), instead of
//                         flexible array members (19.1)
//   GLOBAL_TABLE          the book's hash table of globals, with the constant
//                         reuse from 21.1, instead of indexed slots (21.2)
//   NO_CONSTANT_DEDUP     with GLOBAL_TABLE, turns off 21.1 (for benchmarks)
//   LUA_DOUBLE_HASH       Lua's hash for numbers (for benchmarks, see table.c)
//   BOOK_TABLE_GROWTH     the book's rule for growing, which counts tombstones
//                         (for benchmarks, see tableSet())

#endif
