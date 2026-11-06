// flags: -DDEBUG_TRACE_PARSE
// 17.1: prints the parse function calls for the expression from the
// challenge, and for a conditional expression (17.3).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/vm.h"

// Since chapter 21 a program is a list of statements, so the expression goes
// in a print statement. keepSource() frees the buffer when the VM does.
static void run(const char* source) {
  char* statement = malloc(strlen(source) + 16);
  snprintf(statement, strlen(source) + 16, "print %s;", source);
  printf("%s\n", source);
  interpret(statement);
  keepSource(statement);
  printf("\n");
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  initVM();
  run("(-1 + 2) * 3 - -4");
  run("1 < 2 ? 3 : 4 + 5");
  freeVM();
  return 0;
}
