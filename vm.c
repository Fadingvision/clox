#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "debug.h"
#include "memory.h"
#include "object.h"
#include "compiler.h"
#include "value.h"
#include "vm.h"

// 基于栈的虚拟机
VM vm;

// 第一个内置函数：clock函数
static Value clockNative(int argCount, Value* args) {
  // clock函数返回CPU的时钟周期计数
  // CPU时钟频率，也就是CLOCKS_PER_SEC
  // 两者相除得到程序消耗的时间
  return NUMBER_VAL((double)clock() / CLOCKS_PER_SEC);
}

static void resetStack() {
  // 将栈顶指向数组初始的第一个位置为清空栈
  vm.stackTop = vm.stack;
  vm.frameCount = 0;
  vm.openUpvalues = NULL;
}

// c的可变长参数函数
static void runtimeError(const char* format, ...) {
  // 定义一个va_list类型的变量，变量是指向参数的指针。
  va_list args;
  // va_start初始化刚定义的变量，第二个参数是最后一个显式声明的参数。
  va_start(args, format);
  // 将args已format的形式写入到stderr中
  vfprintf(stderr, format, args);
  // va_end将变量args重置为NULL。
  va_end(args);
  fputs("\n", stderr);

  // 简单写入出错的行数
  // CallFrame* frame = &vm.frames[vm.frameCount - 1];
  // size_t instruction = frame->ip - frame->function->chunk.code;
  // int line = frame->function->chunk.lines[instruction];
  // fprintf(stderr, "[line %d] in script\n", line);

  // 更健壮的错误提示： stack trace
  for (int i = vm.frameCount - 1; i >= 0; i--) {
    CallFrame* frame = &vm.frames[i];
    ObjFunction* function = frame->closure->function;
    // -1 because the IP is sitting on the next instruction to be
    // executed.
    // FIXME: restore register ip into frame's ip
    size_t instruction = frame->ip - function->chunk.code - 1;
    fprintf(stderr, "[line %d] in ",
            function->chunk.lines[instruction]);
    if (function->name == NULL) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s()\n", function->name->chars);
    }
  }

  resetStack();
}

// 定义一个内置函数
static void defineNative(const char* name, NativeFn function) {
  // Note: push, pop操作是为了垃圾回收
  push(OBJ_VAL(copyString(name, (int)strlen(name))));
  push(OBJ_VAL(newNative(function)));
  // 将其插入全局变量中
  tableSet(&vm.globals, AS_STRING(vm.stack[0]), vm.stack[1]);
  pop();
  pop();
}

// 除了nil和false本身，其余全为true
static bool toBool(Value value) {
  if (IS_NIL(value)) return false;
  if (IS_BOOL(value) && !AS_BOOL(value)) {
    return false;
  }
  return true;
}

static void concatenate() {
  ObjString* b = AS_STRING(peek(0));
  ObjString* a = AS_STRING(peek(1));

  // 把a和b的字符串拷贝到一个新的字符串中
  // int length = a->length + b->length;
  // char* chars = ALLOCATE(char, length + 1);
  // memcpy(chars, a->chars, a->length);
  // memcpy(chars + a->length, b->chars, b->length);
  // chars[length] = '\0';  

  ObjString* result = concatenateString(a, b);

  // GC edge-case:
  pop();
  pop();

  push(OBJ_VAL(result));
}

static bool call(ObjClosure* closure, int argCount) {
  // 参数个数校验
  if (argCount != closure->function->arity) {
    runtimeError("Expected %d arguments but got %d.",  
        closure->function->arity, argCount);
    return false;
  }

  // 函数堆栈溢出校验，也就是著名的stack overflow
  if (vm.frameCount == FRAMES_MAX) {
    runtimeError("Stack overflow.");
    return false;
  }

  // 往栈中Push一个调用帧
  CallFrame* frame = &vm.frames[vm.frameCount++];
  // 初始化
  frame->closure = closure;
  frame->ip = closure->function->chunk.code;

  // 减去参数的位置和函数自身占用的位置，则将其重置为函数调用开始的位置(见 vm.h 说明)
  frame->slots = vm.stackTop - argCount - 1;
  return true;
}

static bool callValue(Value callee, int argCount) {
  if (IS_OBJ(callee)) {
    switch (OBJ_TYPE(callee)) {
      // 如果调用的是一个类，则生成一个新的实例，并插入栈中
      case OBJ_CLASS: {
        ObjClass* klass = AS_CLASS(callee);
        // 将栈中的类替换为实例
        vm.stackTop[-argCount - 1] = OBJ_VAL(newInstance(klass));
        // 检查该类是否存在init方法，如果有则执行(此时的类的参数刚好在栈顶，则刚好被init使用)
        Value initializer;
        if (tableGet(&klass->methods, vm.initString, &initializer)) {
          return call(AS_CLOSURE(initializer), argCount);
        } else if (argCount != 0) {
          // 如果没有init方法，则类不应接收参数
          runtimeError("Expected 0 arguments but got %d.", argCount);
          return false;
        }

        return true;
      }
      case OBJ_CLOSURE: {
        return call(AS_CLOSURE(callee), argCount);
        break;
      }
      case OBJ_NATIVE: {
        NativeFn native = AS_NATIVE(callee);
        // 执行native函数
        Value result = (*native)(argCount, vm.stackTop - argCount);
        // native函数不存在执行帧，因此直接将多余的参数和函数本身丢弃，然后将返回值push到栈中
        vm.stackTop -= argCount + 1;
        push(result);
        return true;
      }
      case OBJ_BOUND_METHOD: {
        ObjBoundMethod* bound = AS_BOUND_METHOD(callee);
        // 由于this的查找位置被置为了compiler的第一个位置，因此需要stack该位置的值设为this的值也就是类的实例
        vm.stackTop[-argCount - 1] = bound->receiver;
        return call(bound->method, argCount);
      }
      default:
        break;
    }
  }

  runtimeError("Can only call functions and classes");
  return false;
}

// 直接从类中找到该方法，然后调用
static bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount) {
  Value method;
  if (!tableGet(&klass->methods, name, &method)) {
    runtimeError("Undefined property '%s'.", name->chars);
    return false;
  }

  return call(AS_CLOSURE(method), argCount);
}

static bool invoke(ObjString* name, int argCount) {
  // 此时的栈顶应该是[instance, ...arguments];
  Value receiver = peek(argCount);

  if (!IS_INSTANCE(receiver)) {
    runtimeError("Only instances have methods.");
    return false;
  }

  ObjInstance* instance = AS_INSTANCE(receiver);

  // 先在fields上寻找
  Value value;
  if (tableGet(&instance->fields, name, &value)) {
    vm.stackTop[-argCount - 1] = value;
    return callValue(value, argCount);
  }

  return invokeFromClass(instance->klass, name, argCount);
}

// 新建一个ObjUpvalue*
static ObjUpvalue* captureUpvalue(Value* local) {
  /* 
    Note: 如果直接新建，在下面这种情况下，每个a会在f和g中创建两个ObjUpvalue, 这破坏了ObjUpvalue的唯一性
    fun main{
      var a = 1;
      fun f() {
        print a;
      }
      fun g() {
        print a;
      }
    }
   */

  // 因此每次创建新的ObjUpvalue之前，都必须循环链表来查找是否已经有了一个指向同样Value的ObjUpvalue
  ObjUpvalue* preUpvalue = NULL;
  ObjUpvalue* upvalue = vm.openUpvalues;
  /* 
    三种情况退出while：
    1. upvalue指向地址和我们查找的地址一致, 代表我们已经找到了一个可以复用的值
    2. 已经到了链表尾部，遍历了所有的值都没有找到
    3. 找到一个Upvalue指向的地址小于我们寻找的地址，因为我们的链表是有序的(地址从大到小), 
      则代表之后循环的upvalue的地址只会更小，那么说明已经不存在一个可以复用的值了。
  */
  while (upvalue != NULL && upvalue->location > local) {
    preUpvalue = upvalue;
    upvalue = upvalue->next;
  }

  // 复用upvalue
  if (upvalue != NULL && upvalue->location == local) {
    return upvalue;
  }

  // 创建一个新的ObjUpvalue
  ObjUpvalue* createdUpvalue = newUpvalue(local);
  // 修复链表, 将新创建的值放入 preUpvalue 和 upvalue 之间
  createdUpvalue->next = upvalue;
  if (preUpvalue == NULL) {
    // 如果整个链表只有一个值，那直接插入头部
    vm.openUpvalues = createdUpvalue;
  } else {
    preUpvalue->next = createdUpvalue;
  }

  return createdUpvalue;
}

// close也就是持久化所有大于等于last位置的闭包变量
// 并将其从链表中去除（因为持久化之后该变量也就从stack中消失了，不能用于复用了）
static void closeUpvalues(Value* last) {
  while (vm.openUpvalues != NULL &&
    vm.openUpvalues->location >= last) {
      ObjUpvalue* upvalue = vm.openUpvalues;
      // 将Value的值存入一个新的closed字段，这个字段随着Obj对象一起保存在堆内存中，持久存在
      upvalue->closed = *upvalue->location;
      // 然后将location指向closed, 这样即使原来的stack中的值不存在了
      // 依然可以通过location来获取该值
      upvalue->location = &upvalue->closed;
      vm.openUpvalues = upvalue->next;
    }
}

static void defineMethod(ObjString* name) {
  // 当执行到OP_METHOD的时候，栈顶必然是一个由OP_CLOSURE生成的函数
  Value method = peek(0);
  // 栈顶上面就是我们的OP_CLASS生成的class对象
  ObjClass* klass = AS_CLASS(peek(1));

  // 将其保存在class对象的methods表中，然后从栈中删除该函数
  tableSet(&klass->methods, name, method);
  pop();
}

static bool bindMethod(ObjClass* klass, ObjString* name) {
  Value method;
  if (!tableGet(&klass->methods, name, &method)) {
    runtimeError("Undefined property '%s'.", name->chars);
    return false;
  }

  // 如果在类中找到该方法，将其与实例进行绑定组成boundMethod
  ObjBoundMethod* bound = newBoundMethod(peek(0), AS_CLOSURE(method));
  pop(); // 将实例出栈（不再需要了）
  push(OBJ_VAL(bound));
  return true;
}

static InterpretResult run() {
  CallFrame* frame = &vm.frames[vm.frameCount - 1];
  // 因为在执行过程中，读写ip是一个高频操作，
  // 使用register指令让编译器尽可能的将ip放入寄存器，加快ip的读写速度
  register uint8_t* ip = frame->ip;

  // 从当前函数的调用栈读取一个字节的指令
  #define READ_BYTE() (*ip++)
  // 位运算
  #define READ_SHORT() (ip += 2, (uint16_t)((ip[-2] << 8) | ip[-1]))
  #define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_BYTE()])
  #define READ_STRING() AS_STRING(READ_CONSTANT())
  #define BINARY_OP(valueType, op) \
    do { \
      if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
        runtimeError("Operands must be numbers."); \
        return INTERPRET_RUNTIME_ERROR; \
      } \
      \
      double b = AS_NUMBER(pop()); \
      double a = AS_NUMBER(pop()); \
      push(valueType(a op b)); \
    } while (false) // do while用于加一个块级作用域包裹代码块

  // 按序执行每个指令
  for(;;) {
    #ifdef DEBUG_TRACE_EXECUTION
      // DEBUG: 打印此时内存中所有参数
      printf("          ");
      for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
        printf("[ ");
        printValue(*slot);
        printf(" ]");
      }
      printf("\n");
      // DEBUG: 打印待执行的指令
      disassembleInstruction(&frame->closure->function->chunk, (int)(ip - frame->closure->function->chunk.code));
    #endif

    uint8_t instruction;
    switch (instruction = READ_BYTE()) {
      case OP_NEGATE: {
        // 类型检测
        if (!IS_NUMBER(peek(0))) {
          runtimeError("Operand must be a number");
          return INTERPRET_RUNTIME_ERROR;
        }
        // 取负数写入内存
        push(NUMBER_VAL(-AS_NUMBER(pop())));
        break;
      }
      case OP_NOT: {
        // 对栈顶的数取反，然后写入栈中
        push(BOOL_VAL(!toBool(pop())));
        break;
      }
      case OP_ADD: {
        // 支持字符串相加
        if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
          concatenate();
        } else {
          BINARY_OP(NUMBER_VAL, +);
        }
        break;
      }
      case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
      case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
      case OP_DIVIDE:   BINARY_OP(NUMBER_VAL, /); break;

      case OP_GREATER:  BINARY_OP(BOOL_VAL, >); break;
      case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        // 将比较后的结果转为Value写入内存
        push(BOOL_VAL(isEuqal(a, b)));
        break;
      }
      case OP_PRINT: {
        printValue(pop());
        printf("\n");
        break;
      }
      case OP_RETURN: {
        Value result = pop();
        
        // 当一个函数执行完之后，其中所有的闭包变量都应该被close(也就是持久化)
        closeUpvalues(frame->slots);

        // 函数出栈
        vm.frameCount--;
        if (vm.frameCount == 0) {
          // 如果已经是顶层了，说明整个程序已经执行完了，pop掉script函数，直接返回
          pop();
          return INTERPRET_OK;
        }

        // 重置栈顶：相当于抛弃所有函数执行期间的参数、局部变量，以及函数本身的值
        vm.stackTop = frame->slots;
        // 将函数返回结果入栈，供其他表达式使用
        push(result);
        // 当函数执行完之后，我们需要回到上一个包围函数环境中，继续执行
        frame = &vm.frames[vm.frameCount - 1];

        // 恢复ip至上一个函数的ip
        ip = frame->ip;
        break;
      }
      case OP_POP: {
        pop();
        break;
      }
      case OP_GET_UPVALUE: {
        // 在upvalues中的位置
        uint8_t slot = READ_BYTE();
        // 在upvalues中的location也就是stack中的Value的指针，用*取值，推入栈中
        push(*frame->closure->upvalues[slot]->location);
        break;
      }
      case OP_SET_UPVALUE: {
        // 在upvalues中的位置
        uint8_t slot = READ_BYTE();
        // 在upvalues中的location也就是stack中的Value的指针，对其进行赋值
        *frame->closure->upvalues[slot]->location = peek(0);
        break;
      }
      case OP_CLOSE_UPVALUE: {
        // 将这个闭包变量(此时在栈中的位置为vm.stackTop - 1)放入堆中，方便持久使用
        closeUpvalues(vm.stackTop - 1);
        // 利用完之后，将其正常地从stack中移除
        pop();
        break;
      }
      case OP_GET_LOCAL: {
        // 在locals中的位置 = 在slots中的位置
        uint8_t slot = READ_BYTE();
        // 直接将该值push在stack中供后续表达式使用
        push(frame->slots[slot]);
        break;
      }
      case OP_SET_LOCAL: {
        // 在locals中的位置 = 在slots中的位置
        uint8_t slot = READ_BYTE();
        // 直接将slots的值进行替换，也就完成了赋值
        frame->slots[slot] = peek(0);

        // 在赋值表达式中，并不需要pop(), 因为在compile赋值表达式的时候，默认插入了一个OP_POP指令
        break;
      }
      case OP_DEFINE_GLOBAL: {
        // 从栈中去除放入table中
        ObjString* name = READ_STRING();
        tableSet(&vm.globals, name, peek(0));
        pop();
        break;
      }
      case OP_GET_GLOBAL: {
        // 类似于constant, 从table中取到之后推入栈中，待其他的表达式使用
        ObjString* name = READ_STRING();
        Value value;
        if (!tableGet(&vm.globals, name, &value)) {
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        push(value);
        break;
      }
      case OP_SET_GLOBAL: {
        ObjString* name = READ_STRING();
        Value value;
        if (!tableGet(&vm.globals, name, &value)) {
          runtimeError("Undefined variable '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        } else {
          tableSet(&vm.globals, name, peek(0));
        }
        break;
      }
      // 读出来写入内存
      case OP_CONSTANT: {
        Value constant = READ_CONSTANT();
        push(constant);
        break;
      }
      case OP_NIL: push(NIL_VAL); break;
      case OP_FALSE: push(BOOL_VAL(false)); break;
      case OP_TRUE: push(BOOL_VAL(true)); break;

      // logic control flow
      case OP_JUMP_IF_FALSE: {
        // 读出跳过的字节大小（两个字节存储在OP_JUMP_IF_FALSE后）
        uint16_t offset = READ_SHORT();
        // 此时的条件表达式产生的值应该在栈顶，
        // 如果该条件为假，则跳过offset字节的指令
        // 条件为真，则这offset个字节的指令会正常执行
        if (!toBool(peek(0))) ip += offset;
        break;
      }
      case OP_JUMP: {
        // 读出跳过的字节大小
        uint16_t offset = READ_SHORT();
        // 无条件跳过offset字节的指令
        ip += offset;
        break;
      }
      case OP_LOOP: {
        // 读出回跳的字节大小
        uint16_t offset = READ_SHORT();
        // 无条件回跳offset字节的指令
        ip -= offset;
        break;
      }
      case OP_CALL: {
        // 读出参数的个数, 此时栈中[callee, arg1, arg2]
        // 直到参数的个数，就知道函数在栈中的位置
        uint16_t argCount = READ_BYTE();
        // 往frames中push一个调用帧
        if (!callValue(peek(argCount), argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        // 保存当前函数的ip位置
        frame->ip = ip;
        // 将frame替换成当前需要执行的callee的调用帧，下次循环的时候就进入了函数的真正执行
        frame = &vm.frames[vm.frameCount - 1];
        // 将ip指向新的函数调用的ip地址
        ip = frame->ip;
        break;
      }
      case OP_CLOSURE: {
        ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
        // 将函数包装到一个闭包对象中入栈
        ObjClosure* closure = newClosure(function);
        push(OBJ_VAL(closure));

        // 将该闭包函数所有的upvalues(编译时)写入runtime对应的closure对象中的upvalues数组(runtime)
        for (int i = 0; i < closure->upvalueCount; i++) {
          uint8_t isLocal = READ_BYTE();
          uint8_t index = READ_BYTE();

          // 如果该upvalue引用的是一个stack中的值，则需要新建一个ObjUpvalue值用于在stack中的值释放之后使用
          // 相反，如果该upvalue引用的是另一个upvalue，那么它引用的肯定是当前父环境的upvalue，直接复用其地址，相当于不用新建一个ObjUpvalue

          // Note: 这是很重要的一点，必须保证每一个闭包变量对应的是唯一的一个ObjUpvalue, 
          // 不然当多个闭包函数对同一个变量进行引用以及分别赋值的时候，不会发生错乱，从而保证他们始终都引用的是同一个闭包变量
          if (isLocal) {
            closure->upvalues[i] = captureUpvalue(frame->slots + index);
          } else {
            closure->upvalues[i] = frame->closure->upvalues[index];
          }
        }
        
        break;
      }
      case OP_CLASS: {
        // 将类生成一个Class对象入栈
        push(OBJ_VAL(newClass(READ_STRING())));
        break;
      }
      case OP_INHERIT: {
        Value superClass = peek(1);

        if (!IS_CLASS(superclass)) {
          runtimeError("Superclass must be a class.");
          return INTERPRET_RUNTIME_ERROR;
        }

        ObjClass* subClass = AS_CLASS(peek(0));
        // 这里直接将所有父类的方法复制到子类的方法表中去，这样就实现了继承(copy-down inheritance)
        // 但是这里需要注意的是这种实现方式`不支持` monkey patching, 也就是动态的修改类方法，
        // 因为父类的方法在继承的那一刻就确定了，后面动态修改的方法不会由子类继承
        tableAddAll(&AS_CLASS(superClass)->methods, &subClass->methods);
        pop(); // 删除子类
        break;
      }
      case OP_GET_SUPER: {
        // 读取方法名
        ObjString* name = READ_STRING();
        // 读取父类
        ObjClass* superclass = AS_CLASS(pop());
        if (!bindMethod(superclass, name)) {
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_METHOD: {
        defineMethod(READ_STRING());
        break;
      }
      case OP_INVOKE: {
        ObjString* method = READ_STRING();
        int argCount = READ_BYTE();
        if (!invoke(method, argCount)) {
          return INTERPRET_RUNTIME_ERROR;
        }

        // 保存当前函数的ip位置
        frame->ip = ip;
        // 将frame替换成当前需要执行的callee的调用帧，下次循环的时候就进入了函数的真正执行
        frame = &vm.frames[vm.frameCount - 1];
        // 将ip指向新的函数调用的ip地址
        ip = frame->ip;
        break;
      }
      case OP_GET_PROPERTY: {
        // 判断是否在实例对象上进行读取属性操作
        if (!IS_INSTANCE(peek(0))) {
          runtimeError("Only instances have properties.");
          return INTERPRET_RUNTIME_ERROR;
        }

        // 此时的实例在栈顶
        ObjInstance* instance = AS_INSTANCE(peek(0));
        // 属性名
        ObjString* name = READ_STRING();

        Value value;
        if (tableGet(&instance->fields, name, &value)) {
          pop(); // 将实例出栈（不再需要了）
          push(value); // 将属性值入栈待使用
          break;
        }

        // 在methods中寻找, 如果没找到，直接报
        if (!bindMethod(instance->klass, name)) {
          // TOFIX: 暂时将读取未定义的属性视为一个runtimeError
          runtimeError("Undefined property '%s'.", name->chars);
          return INTERPRET_RUNTIME_ERROR;
        }
        break;
      }
      case OP_SET_PROPERTY: {
        // 判断是否在实例对象上进行读取属性操作
        if (!IS_INSTANCE(peek(1))) {
          runtimeError("Only instances have properties.");
          return INTERPRET_RUNTIME_ERROR;
        }

        // 此时的实例在栈顶后一位
        ObjInstance* instance = AS_INSTANCE(peek(1));         
        // 待赋值的参数在栈顶
        tableSet(&instance->fields, READ_STRING(), peek(0));

        // 将赋值的值取出
        Value value = pop();
        // 将实例出栈（不再需要了）
        pop();
        // 重新将赋值的值入栈待使用，例如：print obj.foo = "bar";
        push(value);
        break;
      }
    }
  }

  #undef READ_BYTE
  #undef READ_SHORT
  #undef READ_CONSTANT
  #undef READ_STRING
  #undef BINARY_OP
}

void initVM() {
  resetStack();
  vm.objects = NULL;
  vm.bytesAllocated = 0;
  // 初始化的回收阈值为1MB的内存
  vm.nextGC = 1024 * 1024;
  vm.grayCount = 0;
  vm.grayCapacity = 0;
  vm.grayStack = NULL;
  initTable(&vm.strings);
  initTable(&vm.globals);

  // 由于我们的所有字符串都是持久化了的，所以这里也把init持久化
  vm.initString = copyString("init", 4);

  // 在初始化vm的时候，注入我们的内置函数
  defineNative("clock", clockNative);
}

void freeVM() {
  freeTable(&vm.strings);
  freeTable(&vm.globals);
  vm.initString = NULL;
  freeObjects();
}

// 执行源码
InterpretResult interpret(const char* source) {
  // 将源码编译成字节码
  ObjFunction* function = compile(source);
  if (function == NULL)  {
    return INTERPRET_COMPILE_ERROR;
  }

  /* 
    Note: 我们的局部变量都是通过该偏移量去获取的，因此locals的位置和stack中的位置必须保持一致
    因为在编译时已经将该函数名推入了locals中。所以这里必须将函数的值也推入stack中，以保持两个数组的偏移量一致
  */
  push(OBJ_VAL(function));

  // 初始化第一个调用帧，也就是顶级函数
  ObjClosure* closure = newClosure(function);
  // GC边界情况：因为newClosure可能会触发垃圾回收，push之后pop是为了将function对象放入栈中保持其引用，避免其被意外的free掉
  pop();
  // 将顶级匿名闭包函数入栈
  push(OBJ_VAL(closure));
  // 调用（也就是将其插入调用帧）
  callValue(OBJ_VAL(closure), 0);

  // CallFrame* frame = &vm.frames[vm.frameCount++];
  // frame->function = function;
  // frame->ip = function->chunk.code;
  // // 此时函数栈的底部应该等于整个执行栈的底部
  // frame->slots = vm.stack;

  // 执行字节码
  InterpretResult result = run();
  return result;
}

void push(Value value) {
  *vm.stackTop = value;
  vm.stackTop++;
}

Value pop() {
  vm.stackTop--;
  return *vm.stackTop; 
}

// 返回距离栈顶distance距离的元素
Value peek(int distance) {
  return vm.stackTop[-1 - distance];
}