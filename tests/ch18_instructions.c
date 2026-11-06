// 18.1 and 18.2: the same expressions in both builds. Only the last few lines
// differ between them (see ch18_instructions.minimal.expected).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/vm.h"

// Since chapter 21 a program is a list of statements, so the expression goes
// in a print statement. keepSource() frees the buffer when the VM does.
static void run(const char* source) {
  char* statement = malloc(strlen(source) + 16);
  snprintf(statement, strlen(source) + 16, "print %s;", source);
  printf("%-24s => ", source);
  interpret(statement);
  keepSource(statement);
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  initVM();
  const char* comparisons[] = {"<", "<=", ">", ">=", "==", "!="};
  const char* operands[][2] = {{"1", "2"}, {"2", "2"}, {"3", "2"},
                               {"-0", "0"}};
  char source[64];
  for (int i = 0; i < 6; i++) {
    for (int j = 0; j < 4; j++) {
      snprintf(source, sizeof(source), "%s %s %s", operands[j][0],
               comparisons[i], operands[j][1]);
      run(source);
    }
  }

  run("5 - 7");
  run("7 - 5 - 1");
  run("-3");
  run("--3");
  run("-0");                  // x * -1 keeps the sign of zero.
  run("0 - 0");
  run("-0 - 0");
  run("1 - -1");
  run("nil");
  run("true");
  run("false");
  run("!nil == true");
  run("true != false");
  run("nil == false");
  run("!(5 - 4 > 3 * 2 == !nil)"); // The book's example.
  run("1 + 2 * 3 - 4 / -2");

  // The minimal build gets these different.
  run("0/0 < 1");                  // NaN compares false with everything...
  run("0/0 <= 1");                 // ...but !(NaN > 1) is true.
  run("0/0 >= 1");
  run("-nil");                     // Error message comes from OP_MULTIPLY.
  run("nil > 1");
  freeVM();
  return 0;
}
