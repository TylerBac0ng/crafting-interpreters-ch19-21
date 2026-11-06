// 20.1: hash table keys that aren't strings.
#include <math.h>
#include <stdio.h>

#include "../src/object.h"
#include "../src/table.h"
#include "../src/vm.h"

static void get(Table* table, const char* label, Value key) {
  Value value;
  printf("%-10s ", label);
  if (tableGet(table, key, &value)) {
    printValue(value);
    printf("\n");
  } else {
    printf("(missing)\n");
  }
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  initVM();

  Table table;
  initTable(&table);

  // An empty table doesn't crash, and nil is a real key, not a missing one.
  get(&table, "nil", NIL_VAL);

  tableSet(&table, NIL_VAL, NUMBER_VAL(1));
  tableSet(&table, BOOL_VAL(true), NUMBER_VAL(2));
  tableSet(&table, BOOL_VAL(false), NUMBER_VAL(3));
  tableSet(&table, NUMBER_VAL(1), NUMBER_VAL(4));
  tableSet(&table, NUMBER_VAL(2.5), NUMBER_VAL(5));
  tableSet(&table, OBJ_VAL(copyString("key", 3)), NUMBER_VAL(6));
  // The string "1" and the number 1 are different keys.
  tableSet(&table, OBJ_VAL(copyString("1", 1)), NUMBER_VAL(7));

  get(&table, "nil", NIL_VAL);
  get(&table, "true", BOOL_VAL(true));
  get(&table, "false", BOOL_VAL(false));
  get(&table, "1", NUMBER_VAL(1));
  get(&table, "2.5", NUMBER_VAL(2.5));
  get(&table, "\"key\"", OBJ_VAL(copyString("key", 3)));
  get(&table, "\"1\"", OBJ_VAL(copyString("1", 1)));
  get(&table, "3", NUMBER_VAL(3));
  get(&table, "\"other\"", OBJ_VAL(copyString("other", 5)));

  // tableSet() says whether the key is new.
  printf("set 1 again is new: %s\n",
         tableSet(&table, NUMBER_VAL(1), NUMBER_VAL(40)) ? "yes" : "no");
  get(&table, "1", NUMBER_VAL(1));

  // 0 == -0 in Lox, so they have to be the same key.
  tableSet(&table, NUMBER_VAL(0), NUMBER_VAL(8));
  get(&table, "-0", NUMBER_VAL(-0.0));
  printf("hash(0) == hash(-0): %s\n",
         hashValue(NUMBER_VAL(0)) == hashValue(NUMBER_VAL(-0.0)) ?
         "yes" : "no");

  // NaN != NaN, so NaN goes in but can never be found.
  double nan = NAN;
  tableSet(&table, NUMBER_VAL(nan), NUMBER_VAL(9));
  get(&table, "NaN", NUMBER_VAL(nan));

  // Delete, and find a key past the tombstone.
  printf("delete nil: %s\n", tableDelete(&table, NIL_VAL) ? "yes" : "no");
  printf("delete nil again: %s\n",
         tableDelete(&table, NIL_VAL) ? "yes" : "no");
  get(&table, "nil", NIL_VAL);
  get(&table, "true", BOOL_VAL(true));

  // Growing keeps every key, and a table with mixed keys works when large.
  Table big;
  initTable(&big);
  for (int i = 0; i < 100000; i++) tableSet(&big, NUMBER_VAL(i), NUMBER_VAL(-i));
  for (int i = 0; i < 100000; i += 2) tableDelete(&big, NUMBER_VAL(i));
  int found = 0;
  for (int i = 0; i < 100000; i++) {
    Value value;
    if (tableGet(&big, NUMBER_VAL(i), &value)) {
      if (i % 2 == 1 && AS_NUMBER(value) == -i) found++;
      else found = -1000000;
    }
  }
  printf("found %d of 50000 after deleting half\n", found);

  // 20.3: a steady number of live keys and a lot of deletes. Capacity stays
  // small, and everything is still there.
  Table queue;
  initTable(&queue);
  for (int i = 0; i < 2000000; i++) {
    tableSet(&queue, NUMBER_VAL(i), NUMBER_VAL(i));
    if (i >= 1000) tableDelete(&queue, NUMBER_VAL(i - 1000));
  }
  found = 0;
  for (int i = 1999000; i < 2000000; i++) {
    Value value;
    if (tableGet(&queue, NUMBER_VAL(i), &value)) found++;
  }
  Value gone;
  printf("after churn: found %d of 1000, old key found: %s, capacity %s\n",
         found, tableGet(&queue, NUMBER_VAL(0), &gone) ? "yes" : "no",
         queue.capacity <= 8192 ? "small" : "too big");
  freeTable(&queue);

  // tableAddAll() copies every kind of key.
  Table copy;
  initTable(&copy);
  tableAddAll(&table, &copy);
  get(&copy, "false", BOOL_VAL(false));
  get(&copy, "2.5", NUMBER_VAL(2.5));

  // tableFindString() has to skip keys that aren't strings.
  ObjString* key = copyString("key", 3);
  printf("find string in mixed table: %s\n",
         tableFindString(&table, "key", 3, key->hash) == key ? "yes" : "no");

  freeTable(&table);
  freeTable(&big);
  freeTable(&copy);
  freeVM();
  return 0;
}
