#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "table.h"
#include "value.h"

// 哈希表的负载数，当数量超过了容量的75%，则需要增加容量
#define TABLE_MAX_LOAD 0.75

void initTable(Table* table) {
  table->count = 0;
  table->capacity = 0;
  table->entries = NULL;
}

// 回收table内存
void freeTable(Table* table) {
  FREE_ARRAY(Entry, table->entries, table->capacity);
  initTable(table);
}

// 找到key所在的位置，如果key不存在，找到一个僵尸位或者空位
static Entry* findEntry(Entry* entries, int capacity,
                        ObjString* key) {
	// hash值为较大的正数，用取模的值为数组的下标
  uint32_t index = key->hash % capacity;
  // 复用僵尸位
  Entry* tombstone = NULL;
  // 解决键值的冲突问题：
  // Open Address的一种方法：Linear probing(线性探索)
  // 具体为：每次index+1循环数组, 直到找到一个空位、或key已经在的位置
  for (;;) {
    Entry* entry = &entries[index];
    // 如果key为Null, 则有两种情况：空位，僵尸位
    if (entry->key == NULL) {
    	// 如果是空位，则返回僵尸位或者空位
      if (IS_NIL(entry->value)) {
        return tombstone != NULL ? tombstone : entry;
      } else {
        // 缓存僵尸位
        if (tombstone == NULL) tombstone = entry;
      }
      // lox中的字符串都做了缓存处理，字面相同的字符串必然指向同一个内存地址，因此可以直接比较
    } else if (entry->key == key) {
      return entry;
    }
    // 如果为capcity的值，自动归0
    index = (index + 1) % capacity;
  }
}

// 由于index的位置是由hash值和容量一起取模决定的
// 调整了容量之后，需要将之前的key重新计算index
static void adjustCapacity(Table* table, int capacity) {
	// 分配一个新的entry数组，并初始化
  Entry* entries = ALLOCATE(Entry, capacity);
  for (int i = 0; i < capacity; i++) {
    entries[i].key = NULL;
    entries[i].value = NIL_VAL;
  }

  // 重置count
  table->count = 0;

  for (int i = 0; i < table->capacity; i++) {
  	// 丢弃掉空位和僵尸位
    Entry* entry = &table->entries[i];
    if (entry->key == NULL) continue;

    // 计算新的index然后复制到新的entries中
    Entry* dest = findEntry(entries, capacity, entry->key);
    dest->key = entry->key;
    dest->value = entry->value;
    table->count++;
  }

  // 释放老的entries内存
  FREE_ARRAY(Entry, table->entries, table->capacity);
  table->entries = entries;
  table->capacity = capacity;
}

// 插入键值对
bool tableSet(Table* table, ObjString* key, Value value) {
	// 当数量超过了容量的75%，则需要增加容量
	if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
    int capacity = GROW_CAPACITY(table->capacity);
    adjustCapacity(table, capacity);
  }

	// 找到是否存在该键值的坑位
	Entry* entry = findEntry(table->entries, table->capacity, key);

	// 如果是新的值并且是空位，则需要将count更新
	bool isNewKey = entry->key == NULL;
	if (isNewKey && IS_NIL(entry->value)) table->count++;

	entry->key = key;
	entry->value = value;
	return isNewKey;
}

void tableAddAll(Table* from, Table* to) {
  for (int i = 0; i < from->capacity; i++) {
    Entry* entry = &from->entries[i];
    if (entry->key != NULL) {
      tableSet(to, entry->key, entry->value);
    }
  }
}

// 检查table是否能找到与char相同的字符串
ObjString* tableFindString(Table* table, const char* chars, int length,
                           uint32_t hash) {
  if (table->count == 0) return NULL;

  uint32_t index = hash % table->capacity;

  for (;;) {
    Entry* entry = &table->entries[index];

    if (entry->key == NULL) {
    	// 如果是空位，说明没有该字符串
      if (IS_NIL(entry->value)) return NULL;
    } else if (entry->key->length == length &&
        entry->key->hash == hash &&
        memcmp(entry->key->chars, chars, length) == 0) {
      // 如果长度相等，hash相同，并且字符串相等则证明是同一个字符串
      return entry->key;
    }

    index = (index + 1) % table->capacity;
  }
}

// 获取键值
bool tableGet(Table* table, ObjString* key, Value* value) {
  if (table->count == 0) return false;

  Entry* entry = findEntry(table->entries, table->capacity, key);
  if (entry->key == NULL) return false;

  *value = entry->value;
  return true;
}

// 删除键值对
bool tableDelete(Table* table, ObjString* key) {                 
  if (table->count == 0) return false;

  Entry* entry = findEntry(table->entries, table->capacity, key);
  if (entry->key == NULL) return false;

  // TODO: 为什么要保持连续性？
  // 为了保持连续性，并不能真正将其删除，而是用一个{NULL, true}的僵尸位来填充，这种方法叫做tombstones
  entry->key = NULL;
  entry->value = BOOL_VAL(true);

  return true;
}

void tableRemoveWhite(Table* table) {
  for (int i = 0; i < table->capacity; i++) {
    Entry* entry = &table->entries[i];
    // 如果键值即将被回收，则直接删除该键值对
    if (entry->key != NULL && !entry->key->obj.isMarked) {
      tableDelete(table, entry->key);
    }
  }
}
