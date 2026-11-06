// 20.3: benchmarks for the hash table. Each case runs five times and prints
// the median. Build with -DLUA_DOUBLE_HASH to compare hashes for numbers.
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/memory.h"
#include "../src/object.h"
#include "../src/table.h"
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

static double median(double times[5]) {
  qsort(times, 5, sizeof(double), compareDoubles);
  return times[2];
}

// A fixed generator, so every run uses the same order.
static uint32_t randomState = 12345;
static uint32_t nextRandom(void) {
  randomState ^= randomState << 13;
  randomState ^= randomState >> 17;
  randomState ^= randomState << 5;
  return randomState;
}

static void shuffle(Value* keys, int count) {
  for (int i = count - 1; i > 0; i--) {
    int j = nextRandom() % (i + 1);
    Value temp = keys[i];
    keys[i] = keys[j];
    keys[j] = temp;
  }
}

// Short strings that look like identifiers.
static Value* stringKeys(const char* prefix, int count) {
  Value* keys = malloc(sizeof(Value) * count);
  char text[64];
  for (int i = 0; i < count; i++) {
    int length = snprintf(text, sizeof(text), "%s%d", prefix, i);
    keys[i] = OBJ_VAL(copyString(text, length));
  }
  return keys;
}

static Value* numberKeys(int count) {
  Value* keys = malloc(sizeof(Value) * count);
  for (int i = 0; i < count; i++) keys[i] = NUMBER_VAL(i);
  return keys;
}

// Keeps the compiler from dropping the lookups.
static volatile int sink;

// Looks up every key in a table, in a random order, until at least minimum
// lookups have run.
static double lookups(Table* table, Value* keys, int count, int minimum) {
  double times[5];
  int rounds = minimum / count + 1;
  for (int run = 0; run < 5; run++) {
    int hits = 0;
    double start = now();
    for (int round = 0; round < rounds; round++) {
      for (int i = 0; i < count; i++) {
        Value value;
        hits += tableGet(table, keys[i], &value);
      }
    }
    times[run] = (now() - start) / (rounds * (double)count);
    sink = hits;
  }
  return median(times);
}

static double inserts(Value* keys, int count) {
  double times[5];
  for (int run = 0; run < 5; run++) {
    Table table;
    initTable(&table);
    double start = now();
    for (int i = 0; i < count; i++) tableSet(&table, keys[i], NIL_VAL);
    times[run] = (now() - start) / count;
    freeTable(&table);
  }
  return median(times);
}

static void fill(Table* table, Value* keys, int count) {
  initTable(table);
  for (int i = 0; i < count; i++) tableSet(table, keys[i], NIL_VAL);
}

// 1: lookups of keys that are there, at sizes from a small scope to a big
// program. Shows the cost once the table no longer fits in the CPU's caches.
static void benchSizes(void) {
  printf("hits by size (string keys, ns per lookup)\n");
  int sizes[] = {10, 100, 1000, 10000, 100000, 1000000};
  for (int i = 0; i < 6; i++) {
    int count = sizes[i];
    Value* keys = stringKeys("size", count);
    Table table;
    fill(&table, keys, count);
    shuffle(keys, count);
    printf("  %8d keys (capacity %8d): %6.1f\n", count, table.capacity,
           lookups(&table, keys, count, 10000000));
    freeTable(&table);
    free(keys);
  }
}

// 2: looking up keys that aren't there. A miss has to probe to an empty
// bucket, and globals that shadow nothing and method lookups walking up a
// class hierarchy both miss a lot.
static void benchMisses(void) {
  int count = 100000;
  Value* keys = stringKeys("here", count);
  Value* missing = stringKeys("gone", count);
  Table table;
  fill(&table, keys, count);
  shuffle(keys, count);
  shuffle(missing, count);
  printf("hits vs misses (%d string keys, ns per lookup)\n", count);
  printf("  hits:   %6.1f\n", lookups(&table, keys, count, 10000000));
  printf("  misses: %6.1f\n", lookups(&table, missing, count, 10000000));
  freeTable(&table);
  free(keys);
  free(missing);
}

// 3: load factor. Compares a table just after it grew with one just before.
static void benchLoad(void) {
  // 12288 keys is exactly 75% of 16384 and 12289 forces a capacity of 32768.
  int sizes[] = {12288, 12289};
  printf("load factor (string keys, ns per lookup)\n");
  for (int i = 0; i < 2; i++) {
    Value* keys = stringKeys("load", sizes[i]);
    Value* missing = stringKeys("nope", sizes[i]);
    Table table;
    fill(&table, keys, sizes[i]);
    shuffle(keys, sizes[i]);
    shuffle(missing, sizes[i]);
    double hits = lookups(&table, keys, sizes[i], 10000000);
    double misses = lookups(&table, missing, sizes[i], 10000000);
    printf("  load %.3f: hit %5.1f, miss %5.1f\n",
           (double)sizes[i] / table.capacity, hits, misses);
    freeTable(&table);
    free(keys);
    free(missing);
  }
}

// 4: building a table, which includes every resize along the way.
static void benchInserts(void) {
  int sizes[] = {1000, 100000, 1000000};
  printf("inserts from empty (string keys, ns per insert)\n");
  for (int i = 0; i < 3; i++) {
    Value* keys = stringKeys("insert", sizes[i]);
    printf("  %8d keys: %6.1f\n", sizes[i], inserts(keys, sizes[i]));
    free(keys);
  }
}

// 5: long keys. Lookups only look at the cached hash and the pointer, so the
// length of the key shouldn't matter. Creating a string does hash it.
static void benchKeyLength(void) {
  int lengths[] = {8, 64, 1024};
  int count = 1000;
  printf("key length (ns per lookup, ns to intern an existing string)\n");
  for (int i = 0; i < 3; i++) {
    int length = lengths[i];
    char* text = malloc(length + 1);
    Value* keys = malloc(sizeof(Value) * count);
    for (int j = 0; j < count; j++) {
      memset(text, 'k', length);
      text[snprintf(text, length, "%d", j)] = 'k';
      text[length] = '\0';
      keys[j] = OBJ_VAL(copyString(text, length));
    }
    Table table;
    fill(&table, keys, count);
    shuffle(keys, count);
    double get = lookups(&table, keys, count, 10000000);

    double times[5];
    int rounds = 2000000 / length + 1;
    for (int run = 0; run < 5; run++) {
      double start = now();
      for (int j = 0; j < rounds; j++) {
        sink = copyString(text, length)->length;
      }
      times[run] = (now() - start) / rounds;
    }
    printf("  %5d chars: lookup %5.1f, intern %8.1f\n", length, get,
           median(times));
    freeTable(&table);
    free(keys);
    free(text);
  }
}

// 6: deletes. A queue of keys: add a new one, delete the oldest. The number
// of live keys stays the same, so this finds out what tombstones cost.
static void benchChurn(void) {
  int live = 10000;
  int operations = 20000000;
  Table table;
  initTable(&table);
  for (int i = 0; i < live; i++) {
    tableSet(&table, NUMBER_VAL(i), NIL_VAL);
  }
  printf("churn (%d live number keys, add one and delete the oldest)\n", live);
  printf("  start: capacity %d\n", table.capacity);

  int step = operations / 4;
  double next = live;
  for (int part = 1; part <= 4; part++) {
    double start = now();
    for (int i = 0; i < step; i++) {
      tableSet(&table, NUMBER_VAL(next), NIL_VAL);
      tableDelete(&table, NUMBER_VAL(next - live));
      next++;
    }
    printf("  after %8d: %6.1f ns per pair, capacity %d, bytes %zu\n",
           part * step, (now() - start) / step, table.capacity,
           sizeof(Entry) * table.capacity);
  }
  freeTable(&table);
}

// 7: a bad key set: strings picked so their hashes end in the same bits.
static uint32_t fnv(const char* key, int length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}

static void benchCollisions(void) {
  int count = 2000;
  Value* bad = malloc(sizeof(Value) * count);
  int found = 0;
  char text[32];
  for (uint32_t i = 0; found < count; i++) {
    int length = snprintf(text, sizeof(text), "attack%u", i);
    if ((fnv(text, length) & 0xffff) == 0) {
      bad[found++] = OBJ_VAL(copyString(text, length));
    }
  }
  Value* good = stringKeys("fair", count);
  printf("colliding keys (%d string keys, ns per operation)\n", count);
  Table table;
  fill(&table, good, count);
  shuffle(good, count);
  printf("  normal keys:    insert %8.1f, hit %8.1f\n", inserts(good, count),
         lookups(&table, good, count, 2000000));
  freeTable(&table);
  fill(&table, bad, count);
  shuffle(bad, count);
  printf("  colliding keys: insert %8.1f, hit %8.1f\n", inserts(bad, count),
         lookups(&table, bad, count, 2000000));
  freeTable(&table);
  free(good);
  free(bad);
}

// 8: keys that are numbers (20.1). Whole numbers in a row are a likely key
// set for a user's map, and they are the worst case for Lua's hash here.
static void benchNumbers(void) {
  int sizes[] = {1000, 20000};
  printf("number keys 0..n-1 (ns per operation)\n");
  for (int i = 0; i < 2; i++) {
    Value* keys = numberKeys(sizes[i]);
    Table table;
    fill(&table, keys, sizes[i]);
    shuffle(keys, sizes[i]);
    printf("  %6d keys: insert %8.1f, hit %8.1f\n", sizes[i],
           inserts(keys, sizes[i]), lookups(&table, keys, sizes[i], 2000000));
    freeTable(&table);
    free(keys);
  }
}

// 9: what 20.1 cost for strings. A copy of the book's table, with ObjString*
// keys, against this one, on the same interned keys.
typedef struct {
  ObjString* key;
  Value value;
} StringEntry;

typedef struct {
  int count;
  int capacity;
  StringEntry* entries;
} StringTable;

static StringEntry* findString(StringEntry* entries, int capacity,
                               ObjString* key) {
  uint32_t index = key->hash % capacity;
  StringEntry* tombstone = NULL;
  for (;;) {
    StringEntry* entry = &entries[index];
    if (entry->key == NULL) {
      if (IS_NIL(entry->value)) {
        return tombstone != NULL ? tombstone : entry;
      } else if (tombstone == NULL) {
        tombstone = entry;
      }
    } else if (entry->key == key) {
      return entry;
    }
    index = (index + 1) % capacity;
  }
}

static void stringTableSet(StringTable* table, ObjString* key, Value value) {
  if (table->count + 1 > table->capacity * 0.75) {
    int capacity = GROW_CAPACITY(table->capacity);
    StringEntry* entries = malloc(sizeof(StringEntry) * capacity);
    for (int i = 0; i < capacity; i++) {
      entries[i].key = NULL;
      entries[i].value = NIL_VAL;
    }
    for (int i = 0; i < table->capacity; i++) {
      StringEntry* entry = &table->entries[i];
      if (entry->key == NULL) continue;
      *findString(entries, capacity, entry->key) = *entry;
    }
    free(table->entries);
    table->entries = entries;
    table->capacity = capacity;
  }

  StringEntry* entry = findString(table->entries, table->capacity, key);
  if (entry->key == NULL) table->count++;
  entry->key = key;
  entry->value = value;
}

static void benchKeyType(void) {
  int sizes[] = {100, 100000};
  printf("Value keys vs ObjString* keys (ns per lookup)\n");
  printf("  entry size: %zu vs %zu bytes\n", sizeof(Entry),
         sizeof(StringEntry));
  for (int i = 0; i < 2; i++) {
    int count = sizes[i];
    Value* keys = stringKeys("type", count);
    Table table;
    fill(&table, keys, count);
    StringTable strings = {0, 0, NULL};
    for (int j = 0; j < count; j++) {
      stringTableSet(&strings, AS_STRING(keys[j]), NIL_VAL);
    }
    shuffle(keys, count);

    double times[5];
    int rounds = 20000000 / count + 1;
    for (int run = 0; run < 5; run++) {
      int hits = 0;
      double start = now();
      for (int round = 0; round < rounds; round++) {
        for (int j = 0; j < count; j++) {
          hits += findString(strings.entries, strings.capacity,
                             AS_STRING(keys[j]))->key != NULL;
        }
      }
      times[run] = (now() - start) / (rounds * (double)count);
      sink = hits;
    }
    double stringKeyTime = median(times);
    double valueKeys = lookups(&table, keys, count, 20000000);
    printf("  %6d keys: Value %5.1f, ObjString* %5.1f\n", count, valueKeys,
           stringKeyTime);
    freeTable(&table);
    free(strings.entries);
    free(keys);
  }
}

int main(int argc, const char* argv[]) {
  initVM();
  if (argc > 1 && strcmp(argv[1], "numbers") == 0) {
    benchNumbers();
  } else if (argc > 1 && strcmp(argv[1], "churn") == 0) {
    benchChurn();
  } else {
    benchSizes();
    benchMisses();
    benchLoad();
    benchInserts();
    benchKeyLength();
    benchChurn();
    benchCollisions();
    benchNumbers();
    benchKeyType();
  }
  freeVM();
  return 0;
}
