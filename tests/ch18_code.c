// flags: -DDEBUG_PRINT_CODE
// 18.1 and 18.2: the bytecode for each operator in both builds.
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
  run("1 != 2");
  run("1 > 2");
  run("1 >= 2");
  run("1 <= 2");
  run("3 - 2");
  run("-3");
  run("true == !nil");
  run("-1 - -1");             // The -1 constant is shared in the minimal build.
  run("true ? 1 : 2");
  freeVM();
  return 0;
}
