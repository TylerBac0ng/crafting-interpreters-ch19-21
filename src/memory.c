#include <stdlib.h>

#include "memory.h"
#include "vm.h"

size_t allocationCount = 0;
size_t bytesAllocated = 0;

void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
  bytesAllocated += newSize - oldSize;
  if (pointer == NULL) allocationCount++;

  if (newSize == 0) {
    free(pointer);
    return NULL;
  }

  void* result = realloc(pointer, newSize);
  if (result == NULL) exit(1);
  return result;
}

static void freeObject(Obj* object) {
  switch (object->type) {
    case OBJ_STRING:
      freeString((ObjString*)object);
      break;
  }
}

void freeObjects() {
  Obj* object = vm.objects;
  while (object != NULL) {
    Obj* next = object->next;
    freeObject(object);
    object = next;
  }
  vm.objects = NULL;
}
