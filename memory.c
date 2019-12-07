#include <stdlib.h>                                               

#include "common.h"
#include "memory.h"
#include "table.h"
#include "compiler.h"
#include "vm.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

#define GC_HEAP_GROW_FACTOR 2

void* reallocate(void* previous, size_t oldSize, size_t newSize) {
  // 更新堆内存使用量的值
  vm.bytesAllocated += newSize - oldSize;

  // 在debug模式下，每次新分配了内存之前都跑一次垃圾回收(bad)
  if (newSize > oldSize) {
    #ifdef DEBUG_STRESS_GC
      collectGarbage();
    #endif
  }

  // 每次当总内存使用量超过了下一次垃圾回收的阈值时，我们跑一次垃圾回收
  if (vm.bytesAllocated > vm.nextGC) {
    collectGarbage();
  }

  if (newSize == 0) {
    free(previous);
    return NULL;                                
  }

  return realloc(previous, newSize);                              
}

void freeObject(Obj* object) {

  // debug模式下，每次释放对象内存之前都打印该对象的内存信息
  #ifdef DEBUG_LOG_GC
    printf("%p free type %d\n", (void*)object, object->type);
  #endif

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
      // free upvalues数组
      ObjClosure* closure = (ObjClosure*)object;
      FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
      // free对象本身
      FREE(ObjClosure, object);
      break;
    }
    case OBJ_UPVALUE: {
      FREE(ObjUpvalue, object);
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

  // 在结束vm之后，同样需要清除我们的垃圾回收器本身所占用的内存
  free(vm.grayStack);
}

// 标记对象为灰色，并将其放入灰色数组
void markObject(Obj* object) {
  if (object == NULL) return;
  // 阻止循环引用，为灰色说明已经被标记过了，不需要进入灰色数组了
  if (object->isMarked) return;

  // 打印被标记对象的信息
  #ifdef DEBUG_LOG_GC
    printf("%p mark ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
  #endif

  object->isMarked = true;

  // 动态数组
  if (vm.grayCapacity < vm.grayCount + 1) {
    vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
    vm.grayStack = realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);
  }
  vm.grayStack[vm.grayCount++] = object;
}

// 标记Value
void markValue(Value value) {
  // 只标记对象类型的值，也就是在堆中的内存
  if (!IS_OBJ(value)) return;
  markObject(AS_OBJ(value));
}

void markTable(Table* table) {
  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    markObject((Obj*)entry->key);
    markValue(entry->value);
  }
}

// 标记所有的根对象，根对象是不可回收的
static void markRoots() {
  // 在执行垃圾回收的时候，所有栈中的对象也就是局部变量和一些临时变量，都是可能被使用的，都被视为根对象
  for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
    markValue(*slot);
  }

  // 所有的在调用帧中的runtime函数也为根对象
  for (int i = 0; i < vm.frameCount; i++) {
    markObject((Obj*)vm.frames[i].closure);
  }
  
  // 还存在栈中引用的闭包对象都是根对象
  for (ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
    markObject((Obj*)upvalue);
  }

  // 自然的，所有全局变量和其中的内置函数也被视为根对象
  markTable(&vm.globals);

  // 编译期间产生的函数对象也为根对象
  markCompilerRoots();

  // 哪些对象可能会被回收呢：
  // 1. 在堆中未被引用的闭包对象（closed）.
  // 2. 临时使用的字符串对象例如： var a = "hello" + "world";
}

static void markArray(ValueArray* array) {
  for (int i = 0; i < array->count; i++) {
    markValue(array->values[i]);
  }
}

static void blackenObject(Obj* object) {
  // debug blacken的对象信息
  #ifdef DEBUG_LOG_GC
    printf("%p blacken ", (void*)object);
    printValue(OBJ_VAL(object));
    printf("\n");
  #endif

  switch (object->type) {
    // 闭包变量中的Value
    case OBJ_UPVALUE:
      markValue(((ObjUpvalue*)object)->closed);
      break;

    // 函数的名字和Values
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      markObject((Obj*)function->name);
      markArray(&function->chunk.constants);
      break;
    }

    // 闭包对象中的闭包变量数组和闭包函数
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      markObject((Obj*)closure->function);
      for (int i = 0; i < closure->upvalueCount; i++) {
        markObject((Obj*)closure->upvalues[i]);
      }
      break;
    }

    // 字符串对象和native对象是不存在引用的
    case OBJ_NATIVE:
    case OBJ_STRING:
      break;
  }
}

// 追踪所有灰色对象的引用对象，直到灰色对象数组为空为止
static void traceRefrences() {
  while (vm.grayCount > 0) {
    // 将其移除灰色数组，并追踪其所有的引用对象
    Obj* object = vm.grayStack[--vm.grayCount];
    blackenObject(object);
  }
}

// 遍历objects链表，回收所有的未标记对象内存
static void sweep() {
  Obj* previous = NULL;
  Obj* object = vm.objects;

  while (object != NULL) {
    if (object->isMarked) {
      // 如果是已标记的对象，需要将其重置为白色对象，然后等待下一轮垃圾回收
      object->isMarked = false;

      previous = object;
      object = object->next;
    } else {
      Obj* unreached = object;

      object = object->next;
      if (previous == NULL) {
        vm.objects = object;
      } else {
        previous->next = object;
      }

      freeObject(unreached);
    }
  }
}


/* 
  标记对象：三色标记法，标记-清除(mark-sweep)算法

  三色标记法是图遍历算法的一种常用辅助方法，在寻找环，拓扑排序方面都有使用。

  白色：所有还未经遍历的对象
  灰色：正在经历遍历的对象
  黑色：已经完成遍历的对象

  在垃圾回收中：所有的根对象都默认标记为灰色对象，将所有的灰色对象放入一个灰色数组中，
  依次进行遍历，将每个灰色对象中可以引用到的其他对象标记为灰色，然后放入灰色数组中，
  然后将该对象本身标记为黑色(为灰色又不在灰色数组中的对象自动视为黑色)，从灰色数组中移除。
  直到灰色对象数组被清空为止，则剩下的白色对象就是未被引用的对象，也就是垃圾回收所需要释放的对象

  在完成垃圾回收之后，需要将所有的黑色对象重新标记为白色对象，以便下一次垃圾回收周期使用。
*/
void collectGarbage() {
  #ifdef DEBUG_LOG_GC
    printf("-- gc begin\n");
  #endif

  size_t before = vm.bytesAllocated;

  // 1. 标记所有的根对象为灰色
  markRoots();

  // 2. 递归的追踪所有对象的引用
  traceRefrences();

  // 2.5 追踪所有的弱引用（弱引用与强引用相对，是指不能确保其引用的对象不会被垃圾回收器回收的引用。）
  // 当回收字符串对象之后，某些字符串对象将不复存在，但是我们的全局table: vm.strings
  // 维护了一个这样的表，因此为了避免hastable中的键值(弱引用)指针指向一个空对象，在真正回收ObjString之前，
  // 需要将vm.strings这样的弱引用删除
  tableRemoveWhite(&vm.strings);

  // 3. 回收未被引用的对象
  sweep();

  // 更新为下一次进行垃圾回收的阈值：当前内存使用量的两倍
  vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;

  #ifdef DEBUG_LOG_GC
    printf("-- gc end\n");
    // 每次回收之后，打印出该次回收的内存量和下一次回收的阈值
    printf("   collected %ld bytes (from %ld to %ld) next at %ld\n",
         before - vm.bytesAllocated, before, vm.bytesAllocated,   
         vm.nextGC);
  #endif
}