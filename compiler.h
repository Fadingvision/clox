#ifndef clox_compiler_h
#define clox_compiler_h

#include "chunk.h"
#include "object.h"

ObjFunction* compile(const char* source);
// 标记编译期间的根对象
void markCompilerRoots();

#endif