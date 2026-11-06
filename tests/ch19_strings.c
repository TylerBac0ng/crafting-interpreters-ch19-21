// 19.1 and 19.2: strings. The last lines count allocations, and those differ
// between the two representations (see ch19_strings.constant.expected).
#include <stdio.h>
#include <string.h>

#include "../src/memory.h"
#include "../src/object.h"
#include "../src/vm.h"

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  initVM();
  interpret("print \"st\" + \"ri\" + \"ng\";");
  interpret("print \"\" + \"\";");                // Empty strings.
  interpret("print \"a\" + \"\" == \"a\";");       // Concatenation is interned.
  interpret("print \"ab\" == \"a\" + \"b\";");
  interpret("print \"ab\" != \"a\" + \"c\";");
  interpret("print \"1\" == 1;");
  interpret("print !\"\";");                       // Strings are truthy.
  interpret("print \"one\ntwo\";");                // Multi-line.
  interpret("print \"a\" + 1;");
  interpret("print 1 + \"a\";");
  interpret("print -\"a\";");
  interpret("print \"a\" < \"b\";");
  interpret("print \"never closed");

  // One literal, then the same literal again, which allocates nothing.
  size_t count = allocationCount;
  ObjString* first = literalString("characters", 10);
  printf("first literal: %zu allocations\n", allocationCount - count);
  count = allocationCount;
  ObjString* second = literalString("characters", 10);
  printf("same literal: %zu allocations, same object: %s\n",
         allocationCount - count, first == second ? "yes" : "no");

  // A concatenation whose result already exists. The new string is thrown
  // away and the old one comes back.
  ObjString* copy = makeString(10);
  memcpy(stringBuffer(copy), "characters", 10);
  printf("finished duplicate is first: %s\n",
         finishString(copy) == first ? "yes" : "no");

  count = allocationCount;
  ObjString* owned = copyString("owned", 5);
  printf("string that owns its characters: %zu allocations\n",
         allocationCount - count);
  printf("copy ends at length: %s\n",
         owned->chars[owned->length] == '\0' ? "yes" : "no");
  printf("sizeof(ObjString) = %zu\n", sizeof(ObjString));
#ifdef CONSTANT_STRINGS
  printf("literal owns characters: %s\n", first->ownsChars ? "yes" : "no");
  // A substring of a longer source: the literal doesn't copy it, so the next
  // character is still there and nothing is terminated.
  const char* source = "literal-rest";
  ObjString* slice = literalString(source, 7);
  printf("literal points at the source: %s\n",
         slice->chars == source ? "yes" : "no");
  printf("copy owns characters: %s\n", owned->ownsChars ? "yes" : "no");
#else
  printf("characters follow the header: %s\n",
         (char*)first->chars == (char*)first + sizeof(ObjString) ?
         "yes" : "no");
#endif
  freeVM();
  return 0;
}
