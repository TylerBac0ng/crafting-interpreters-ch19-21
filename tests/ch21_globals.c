// 21.1 and 21.2: global variables. The same programs have to behave the same
// with the book's hash table (GLOBAL_TABLE) and with indexed slots.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/chunk.h"
#include "../src/compiler.h"
#include "../src/object.h"
#include "../src/vm.h"

// Writes a program made of count copies of a statement. A %d in the
// statement gets the number of the copy.
static char* repeat(const char* format, int count) {
  int size = count * 64 + 1;
  char* source = malloc(size);
  int length = 0;
  source[0] = '\0';
  for (int i = 0; i < count; i++) {
    length += snprintf(source + length, size - length, format, i);
  }
  return source;
}

static int constantsUsed(const char* source) {
  Chunk chunk;
  initChunk(&chunk);
  compile(source, &chunk);
  int count = chunk.constants.count;
  freeChunk(&chunk);
  return count;
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  initVM();

  interpret("var a = 1; var b = 2; print a + b;");
  interpret("var s = \"be\"; s = s + \"ignets\"; print s;");
  interpret("var u; print u;");                 // No initializer means nil.
  interpret("var a = \"again\"; print a;");     // Redefining is allowed.
  interpret("print a = \"assign\"; print a;");  // Assignment is an expression.
  interpret("var x; var y; x = y = 3; print x + y;");

  // Values last between calls, as they do between lines of the REPL.
  interpret("var kept = \"kept\";");
  interpret("print kept;");

  // Runtime errors keep the variable's name.
  interpret("print missing;");
  interpret("missing = 1;");
  interpret("var self = self;");
  interpret("print missing;");    // Still undefined after the failed set.
  interpret("var missing = \"now defined\"; print missing;");

  // A function body isn't available yet, so this is the REPL case from 21.3:
  // a name used on one line and defined on the next.
  interpret("later;");
  interpret("var later = 1; print later;");

  // Compile errors.
  interpret("var a * b = 1;");
  interpret("var p = 1; var q = 2; p * q = 3;");
  interpret("print 1 ? 2 : p = 3;");
  interpret("var 1 = 2;");
  interpret("print;");
  interpret("print 1 print 2;");  // Recovers and reports both.
  interpret("var ok");

  // 21.1: this many references to one name don't fill the constant table.
  char* uses = repeat("print a;", 1000);
  printf("1000 uses of a: %d constants\n", constantsUsed(uses));
  keepSource(uses);

  // 21.2: slots have a two byte operand, so a program can have more than 256
  // globals. These have no initializers, so names are the only constants.
  // The table build stops at 256 (see ch21_globals.table.expected).
  char* defines = repeat("var v%d;", 300);
  InterpretResult result = interpret(defines);
  printf("300 globals: %s\n", result == INTERPRET_OK ? "ok" : "error");
  keepSource(defines);
  interpret("print v0; print v299;");

  freeVM();
  return 0;
}
