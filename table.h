#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

// hash-table
typedef struct {
  int count;
  int capacity;
  Entry* entries;
} Table;

// 键值对
typedef struct {
  ObjString* key;
  Value value;
} Entry;

void initTable(Table* table);
void freeTable(Table* table);

// table methods
bool tableSet(Table* table, ObjString* key, Value value);
bool tableGet(Table* table, ObjString* key, Value* value);
bool tableDelete(Table* table, ObjString* key);
void tableAddAll(Table* from, Table* to);

ObjString* tableFindString(Table* table, const char* chars, int length,
                           uint32_t hash);

#endif