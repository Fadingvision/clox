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

void freeObject(Obj* object) {
  switch (object->type) {
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      // free所有的字符串
      // FREE_ARRAY(char, string->chars, string->length + 1);
      // free对象本身
      FREE(ObjString, object);
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* func = (ObjFunction*)object;
      // free函数体的指令集
      freeChunk(&func->chunk);
      // free对象本身
      FREE(ObjFunction, object);
      break;
    }
    case OBJ_CLOSURE: {
      // free对象本身
      FREE(objClosure, object);
      break;
    }
    case OBJ_NATIVE: {
      FREE(ObjNative, object);
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