// 21.1 and 21.2: benchmarks for global variables. Lox can't loop yet, so
// these write a long program, compile it once, and run the chunk many times.
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../src/chunk.h"
#include "../src/compiler.h"
#include "../src/vm.h"

static double now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1e9 + t.tv_nsec;
}

static int compareDoubles(const void* a, const void* b) {
  double x = *(const double*)a, y = *(const double*)b;
  return (x > y) - (x < y);
}

#ifndef NO_CONSTANT_DEDUP
// 21.2: 1000 statements like g1 = g2 - g3; over a number of globals. Each
// is three global accesses, so this mostly measures OP_GET and OP_SET_GLOBAL.
static void benchAccess(int globals) {
  int statements = 1000;
  int runs = 20000;
  char* source = malloc((size_t)(globals + statements) * 64);
  char* end = source;
  end += sprintf(end, "var g0 = 1;\n");
  for (int i = 1; i < globals; i++) end += sprintf(end, "var g%d = g0;\n", i);

  char* body = end;
  uint32_t random = 12345;
  for (int i = 0; i < statements; i++) {
    int slots[3];
    for (int j = 0; j < 3; j++) {
      random ^= random << 13;
      random ^= random >> 17;
      random ^= random << 5;
      slots[j] = random % globals;
    }
    end += sprintf(end, "g%d = g%d - g%d;\n", slots[0], slots[1], slots[2]);
  }

  interpret(source);

  Chunk chunk;
  initChunk(&chunk);
  if (!compile(body, &chunk)) exit(1);

  double times[5];
  for (int run = 0; run < 5; run++) {
    double start = now();
    for (int i = 0; i < runs; i++) runChunk(&chunk);
    times[run] = (now() - start) / ((double)runs * statements);
  }
  qsort(times, 5, sizeof(double), compareDoubles);
  printf("  %3d globals: %6.1f ns per statement (%d constants)\n", globals,
         times[2], chunk.constants.count);
  freeChunk(&chunk);
  keepSource(source);
}
#endif

// 21.1: compile a program of 250 uses of one name or of 250 different names.
// That's as long as it can be with no reuse, before the table runs out.
static void benchCompile(const char* label, bool different) {
  int uses = 250;
  int runs = 20000;
  char* source = malloc(uses * 32);
  char* end = source;
  for (int i = 0; i < uses; i++) {
    end += different ? sprintf(end, "name%d;\n", i) : sprintf(end, "name;\n");
  }

  double times[5];
  int constants = 0;
  for (int run = 0; run < 5; run++) {
    double start = now();
    for (int i = 0; i < runs; i++) {
      Chunk chunk;
      initChunk(&chunk);
      if (!compile(source, &chunk)) exit(1);
      constants = chunk.constants.count;
      freeChunk(&chunk);
    }
    times[run] = (now() - start) / ((double)runs * uses);
  }
  qsort(times, 5, sizeof(double), compareDoubles);
  printf("  %-22s %6.1f ns per use (%d constants)\n", label, times[2],
         constants);
  keepSource(source);
}

int main(void) {
  initVM();
#ifdef NO_CONSTANT_DEDUP
  // Without 21.1 the program in benchAccess() doesn't fit in a chunk.
  printf("run time: too many constants to compile\n");
#else
  printf("run time\n");
  benchAccess(8);
  benchAccess(200);
#endif
  printf("compile time\n");
  benchCompile("250 uses of one name:", false);
  benchCompile("250 different names:", true);
  freeVM();
  return 0;
}
