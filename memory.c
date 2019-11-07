#include <stdlib.h>                                               

#include "common.h"
#include "memory.h"
#include "vm.h"

void* reallocate(void* previous, size_t oldSize, size_t newSize) {
  if (newSize == 0) {
    free(previous);
    return NULL;                                
  }

  return realloc(previous, newSize);                              
}

static void freeObject(Obj* object) {
  switch (object->type) {
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      // free所有的字符串
      // FREE_ARRAY(char, string->chars, string->length + 1);
      // free对象本身
      FREE(ObjString, object);
      break;
    }
  }
}

// 循环链表释放所有的对象内存
void freeObjects() {
  Obj* object = vm.objects;
  while (object != NULL) {
    Obj* next = object->next;
    freeObject(object);
    object = next;
  }
}