#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

#define TABLE_MAX_LOAD 0.75

void initTable(Table* table) {
  table->count = 0;
  table->tombstones = 0;
  table->capacity = 0;
  table->entries = NULL;
}

void freeTable(Table* table) {
  FREE_ARRAY(Entry, table->entries, table->capacity);
  initTable(table);
}

// 20.1: equal numbers must hash the same, so 0 and -0 have to match even
// though their bits differ. The answer in the book's repo uses Lua's hash
// (LUA_DOUBLE_HASH here), but Lua indexes with an odd number. Our capacity is
// always a power of two, and the low bits of a small whole number are all
// zero, so those keys all collide. This uses the finalizer from MurmurHash3
// so that every bit of the input affects the low bits.
static uint32_t hashDouble(double value) {
#ifdef LUA_DOUBLE_HASH
  union {
    double value;
    uint32_t ints[2];
  } cast;
  cast.value = value + 1.0;
  return cast.ints[0] + cast.ints[1];
#else
  if (value == 0) value = 0; // Turns -0 into 0.

  union {
    double value;
    uint64_t bits;
  } cast;
  cast.value = value;

  uint64_t hash = cast.bits;
  hash ^= hash >> 33;
  hash *= 0xff51afd7ed558ccdULL;
  hash ^= hash >> 33;
  hash *= 0xc4ceb9fe1a85ec53ULL;
  hash ^= hash >> 33;
  return (uint32_t)hash;
#endif
}

uint32_t hashValue(Value value) {
  switch (value.type) {
    case VAL_BOOL:   return AS_BOOL(value) ? 3 : 5;
    case VAL_NIL:    return 7;
    case VAL_NUMBER: return hashDouble(AS_NUMBER(value));
    case VAL_OBJ:    return AS_STRING(value)->hash;
    case VAL_EMPTY:  return 0;
  }
  return 0; // Unreachable.
}

static Entry* findEntry(Entry* entries, int capacity, Value key) {
  uint32_t index = hashValue(key) % capacity;
  Entry* tombstone = NULL;

  for (;;) {
    Entry* entry = &entries[index];
    if (IS_EMPTY(entry->key)) {
      if (IS_NIL(entry->value)) {
        // Empty entry.
        return tombstone != NULL ? tombstone : entry;
      } else {
        // We found a tombstone.
        if (tombstone == NULL) tombstone = entry;
      }
    } else if (valuesEqual(entry->key, key)) {
      // We found the key.
      return entry;
    }

    index = (index + 1) % capacity;
  }
}

bool tableGet(Table* table, Value key, Value* value) {
  if (table->count == 0) return false;

  Entry* entry = findEntry(table->entries, table->capacity, key);
  if (IS_EMPTY(entry->key)) return false;

  *value = entry->value;
  return true;
}

static void adjustCapacity(Table* table, int capacity) {
  Entry* entries = ALLOCATE(Entry, capacity);
  for (int i = 0; i < capacity; i++) {
    entries[i].key = EMPTY_VAL;
    entries[i].value = NIL_VAL;
  }

  table->count = 0;
  table->tombstones = 0;
  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    if (IS_EMPTY(entry->key)) continue;

    Entry* dest = findEntry(entries, capacity, entry->key);
    dest->key = entry->key;
    dest->value = entry->value;
    table->count++;
  }

  FREE_ARRAY(Entry, table->entries, table->capacity);
  table->entries = entries;
  table->capacity = capacity;
}

bool tableSet(Table* table, Value key, Value value) {
  if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
#ifdef BOOK_TABLE_GROWTH
    adjustCapacity(table, GROW_CAPACITY(table->capacity));
#else
    // 20.3: when the table is full of tombstones it doesn't need more room,
    // only a clean up. Doubling anyway makes a table that has a steady number
    // of live keys, but sees a lot of deletes, grow without limit.
    int live = table->count - table->tombstones;
    bool mostlyTombstones = live + 1 <= table->capacity * TABLE_MAX_LOAD / 2;
    adjustCapacity(table, mostlyTombstones ? table->capacity
                                           : GROW_CAPACITY(table->capacity));
#endif
  }

  Entry* entry = findEntry(table->entries, table->capacity, key);
  bool isNewKey = IS_EMPTY(entry->key);
  if (isNewKey) {
    if (IS_NIL(entry->value)) {
      table->count++;
    } else {
      table->tombstones--; // Reusing a tombstone.
    }
  }

  entry->key = key;
  entry->value = value;
  return isNewKey;
}

bool tableDelete(Table* table, Value key) {
  if (table->count == 0) return false;

  // Find the entry.
  Entry* entry = findEntry(table->entries, table->capacity, key);
  if (IS_EMPTY(entry->key)) return false;

  // Place a tombstone in the entry.
  entry->key = EMPTY_VAL;
  entry->value = BOOL_VAL(true);
  table->tombstones++;
  return true;
}

void tableAddAll(Table* from, Table* to) {
  for (int i = 0; i < from->capacity; i++) {
    Entry* entry = &from->entries[i];
    if (!IS_EMPTY(entry->key)) {
      tableSet(to, entry->key, entry->value);
    }
  }
}

ObjString* tableFindString(Table* table, const char* chars, int length,
                           uint32_t hash) {
  if (table->count == 0) return NULL;

  uint32_t index = hash % table->capacity;
  for (;;) {
    Entry* entry = &table->entries[index];
    if (IS_EMPTY(entry->key)) {
      // Stop if we find an empty non-tombstone entry.
      if (IS_NIL(entry->value)) return NULL;
    } else if (IS_STRING(entry->key)) {
      ObjString* string = AS_STRING(entry->key);
      if (string->length == length && string->hash == hash &&
          memcmp(string->chars, chars, length) == 0) {
        // We found it.
        return string;
      }
    }

    index = (index + 1) % table->capacity;
  }
}
