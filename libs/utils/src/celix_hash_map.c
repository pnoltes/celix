/**
 *Licensed to the Apache Software Foundation (ASF) under one
 *or more contributor license agreements.  See the NOTICE file
 *distributed with this work for additional information
 *regarding copyright ownership.  The ASF licenses this file
 *to you under the Apache License, Version 2.0 (the
 *"License"); you may not use this file except in compliance
 *with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *Unless required by applicable law or agreed to in writing,
 *software distributed under the License is distributed on an
 *"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 *specific language governing permissions and limitations
 *under the License.
 */

#include "celix_string_hash_map.h"
#include "celix_long_hash_map.h"
#include "celix_hash_map_private.h"

#include <stdlib.h>
#include <memory.h>
#include <math.h>
#include <assert.h>
#include <stdint.h>

#include "celix_utils.h"
#include "celix_err.h"
#include "celix_stdlib_cleanup.h"

static unsigned int DEFAULT_INITIAL_CAPACITY = 16;
static double DEFAULT_LOAD_FACTOR = 0.75;
static unsigned int MAXIMUM_CAPACITY = INT32_MAX/10;



union celix_hash_map_key {
    const char* strKey;
    long longKey;
};


struct celix_hash_map_entry {
    celix_hash_map_key_t key;
    celix_hash_map_value_t value;
    celix_hash_map_entry_t* next;
    unsigned int hash;
};

struct celix_hash_map {
    celix_hash_map_entry_t** buckets;
    unsigned int bucketsSize; //nr of buckets
    unsigned int size; //nr of total entries
    double loadFactor;
    celix_hash_map_key_type_e keyType;
    celix_hash_map_value_t emptyValue;
    unsigned int (*hashKeyFunction)(const celix_hash_map_key_t* key);
    bool (*equalsKeyFunction)(const celix_hash_map_key_t* key1, const celix_hash_map_key_t* key2);
    void (*simpleRemovedCallback)(void* value);
    void* removedCallbackData;
    void (*removedStringEntryCallback)(void* data, const char* removedKey, celix_hash_map_value_t removedValue);
    void (*removedStringKeyCallback)(void* data, char* key);
    void (*removedLongEntryCallback)(void* data, long removedKey, celix_hash_map_value_t removedValue);
    bool storeKeysWeakly;
};

struct celix_string_hash_map {
    celix_hash_map_t genericMap;
};

struct celix_long_hash_map {
    celix_hash_map_t genericMap;
};

static unsigned int celix_stringHashMap_hash(const celix_hash_map_key_t* key) {
    return celix_utils_stringHash(key->strKey);
}

static unsigned int celix_longHashMap_hash(const celix_hash_map_key_t* key) {
    return key->longKey ^ (key->longKey >> (sizeof(key->longKey)*8/2));
}

static bool celix_stringHashMap_keyEquals(const celix_hash_map_key_t* key1, const celix_hash_map_key_t* key2) {
    return celix_utils_stringEquals(key1->strKey, key2->strKey);
}

static bool celix_longHashMap_keyEquals(const celix_hash_map_key_t* key1, const celix_hash_map_key_t* key2) {
    return key1->longKey == key2->longKey;
}

static unsigned int celix_hashMap_threshold(celix_hash_map_t* map) {
    return (unsigned int)floor((double)map->bucketsSize * map->loadFactor);
}

static unsigned int celix_hashMap_indexFor(unsigned int h, unsigned int length) {
    return h & (length - 1);
}

static celix_hash_map_entry_t* celix_hashMap_getEntry(const celix_hash_map_t* map, const char* strKey, long longKey) {
    celix_hash_map_key_t key;
    if (map->keyType == CELIX_HASH_MAP_STRING_KEY) {
        key.strKey = strKey;
    } else {
        key.longKey = longKey;
    }
    unsigned int hash = map->hashKeyFunction(&key);
    unsigned int index = celix_hashMap_indexFor(hash, map->bucketsSize);
    for (celix_hash_map_entry_t* entry = map->buckets[index]; entry != NULL; entry = entry->next) {
        if (entry->hash == hash && map->equalsKeyFunction(&key, &entry->key)) {
            return entry;
        }
    }
    return NULL;
}

static void* celix_hashMap_get(const celix_hash_map_t* map, const char* strKey, long longKey) {
    celix_hash_map_entry_t* entry = celix_hashMap_getEntry(map, strKey, longKey);
    if (entry != NULL) {
        return entry->value.ptrValue;
    }
    return NULL;
}

static long celix_hashMap_getLong(const celix_hash_map_t* map, const char* strKey, long longKey, long fallbackValue) {
    celix_hash_map_entry_t* entry = celix_hashMap_getEntry(map, strKey, longKey);
    if (entry != NULL) {
        return entry->value.longValue;
    }
    return fallbackValue;
}

static double celix_hashMap_getDouble(const celix_hash_map_t* map, const char* strKey, long longKey, double fallbackValue) {
    celix_hash_map_entry_t* entry = celix_hashMap_getEntry(map, strKey, longKey);
    if (entry != NULL) {
        return entry->value.doubleValue;
    }
    return fallbackValue;
}

static bool celix_hashMap_getBool(const celix_hash_map_t* map, const char* strKey, long longKey, bool fallbackValue) {
    celix_hash_map_entry_t* entry = celix_hashMap_getEntry(map, strKey, longKey);
    if (entry != NULL) {
        return entry->value.boolValue;
    }
    return fallbackValue;
}

bool celix_hashMap_hasKey(const celix_hash_map_t* map, const char* strKey, long longKey) {
    return celix_hashMap_getEntry(map, strKey, longKey) != NULL;
}

celix_status_t celix_hashMap_resize(celix_hash_map_t *map, size_t newCapacity) {
    if (map->bucketsSize >= MAXIMUM_CAPACITY) {
        return CELIX_SUCCESS;
    }

    assert(newCapacity > map->bucketsSize); //hash map can only grow

    celix_hash_map_entry_t **newBuf = realloc(map->buckets, newCapacity * sizeof(celix_hash_map_entry_t *));
    if (!newBuf) {
        celix_err_push("Cannot realloc memory for hash map");
        return CELIX_ENOMEM;
    }

    map->buckets = newBuf;

    //clear newly added mem
    for (unsigned int i = map->bucketsSize; i < newCapacity; ++i) {
        map->buckets[i] = NULL;
    }

    //reinsert old entries
    for (unsigned i = 0; i < map->bucketsSize; i++) {
        celix_hash_map_entry_t *entry = map->buckets[i];
        if (entry != NULL) {
            map->buckets[i] = NULL;
            do {
                celix_hash_map_entry_t *next = entry->next;
                unsigned int bucketIndex = celix_hashMap_indexFor(entry->hash, newCapacity);
                entry->next = map->buckets[bucketIndex];
                map->buckets[bucketIndex] = entry;
                entry = next;
            } while (entry != NULL);
        }
    }

    //update bucketSize to new capacity
    map->bucketsSize = newCapacity;

    return CELIX_SUCCESS;
}

static void celix_hashMap_callRemovedCallback(celix_hash_map_t* map, celix_hash_map_entry_t* removedEntry) {
    if (map->simpleRemovedCallback) {
        map->simpleRemovedCallback(removedEntry->value.ptrValue);
    } else if (map->removedLongEntryCallback) {
        map->removedLongEntryCallback(map->removedCallbackData, removedEntry->key.longKey, removedEntry->value);
    } else if (map->removedStringEntryCallback) {
        map->removedStringEntryCallback(map->removedCallbackData, removedEntry->key.strKey, removedEntry->value);
    }
}

static void celix_hashMap_destroyRemovedKey(celix_hash_map_t* map, char* removedKey) {
    if (map->removedStringKeyCallback) {
        map->removedStringKeyCallback(map->removedCallbackData, removedKey);
    }
    if (!map->storeKeysWeakly) {
        free(removedKey);
    }
}

static void celix_hashMap_destroyRemovedEntry(celix_hash_map_t* map, celix_hash_map_entry_t* removedEntry) {
    celix_hashMap_callRemovedCallback(map, removedEntry);
    free(removedEntry);
}

celix_status_t celix_hashMap_addEntry(celix_hash_map_t* map, unsigned int hash, const celix_hash_map_key_t* key, const celix_hash_map_value_t* value, unsigned int bucketIndex) {
    celix_hash_map_entry_t* entry = map->buckets[bucketIndex];
    celix_hash_map_entry_t* newEntry = malloc(sizeof(*newEntry));
    if (!newEntry) {
        celix_err_push("Cannot allocate memory for hash map entry");
        return CELIX_ENOMEM;
    }

    newEntry->hash = hash;
    if (map->keyType == CELIX_HASH_MAP_STRING_KEY) {
        newEntry->key.strKey = map->storeKeysWeakly ? key->strKey : celix_utils_strdup(key->strKey);
    } else {
        newEntry->key.longKey = key->longKey;
    }
    memcpy(&newEntry->value, value, sizeof(*value));
    newEntry->next = entry;
    map->buckets[bucketIndex] = newEntry;
    if (map->size++ >= celix_hashMap_threshold(map)) {
        return celix_hashMap_resize(map, 2 * map->bucketsSize);
    }
    return CELIX_SUCCESS;
}

static celix_status_t celix_hashMap_putValue(celix_hash_map_t* map, const char* strKey, long longKey, const celix_hash_map_value_t* value, celix_hash_map_value_t* replacedValueOut) {
    celix_hash_map_key_t key;
    if (map->keyType == CELIX_HASH_MAP_STRING_KEY) {
        key.strKey = strKey;
    } else {
        key.longKey = longKey;
    }
    unsigned int hash = map->hashKeyFunction(&key);
    unsigned int index = celix_hashMap_indexFor(hash, map->bucketsSize);
    for (celix_hash_map_entry_t* entry = map->buckets[index]; entry != NULL; entry = entry->next) {
        if (entry->hash == hash && map->equalsKeyFunction(&key, &entry->key)) {
            //entry found, replacing entry
            if (replacedValueOut != NULL) {
                *replacedValueOut = entry->value;
            }
            celix_hashMap_callRemovedCallback(map, entry);
            memcpy(&entry->value, value, sizeof(*value));
            return CELIX_SUCCESS;
        }
    }
    celix_status_t status = celix_hashMap_addEntry(map, hash, &key, value, index);
    if (status == CELIX_SUCCESS && replacedValueOut != NULL) {
        memset(replacedValueOut, 0, sizeof(*replacedValueOut));
    }
    return status;
}

static celix_status_t celix_hashMap_put(celix_hash_map_t* map, const char* strKey, long longKey, void* v, void** previousValueOut) {
    celix_hash_map_value_t value;
    memset(&value, 0, sizeof(value));
    value.ptrValue = v;
    return celix_hashMap_putValue(map, strKey, longKey, &value, NULL);
}

static celix_status_t celix_hashMap_putLong(celix_hash_map_t* map, const char* strKey, long longKey, long v, long* previousValueOut) {
    celix_hash_map_value_t value;
    memset(&value, 0, sizeof(value));
    value.longValue = v;
    return celix_hashMap_putValue(map, strKey, longKey, &value, NULL);
}

static celix_status_t celix_hashMap_putDouble(celix_hash_map_t* map, const char* strKey, long longKey, double v) {
    celix_hash_map_value_t value;
    memset(&value, 0, sizeof(value));
    value.doubleValue = v;
    return celix_hashMap_putValue(map, strKey, longKey, &value, NULL);
}

static celix_status_t celix_hashMap_putBool(celix_hash_map_t* map, const char* strKey, long longKey, bool v, bool* previousValueOut) {
    celix_hash_map_value_t value;
    memset(&value, 0, sizeof(value));
    value.boolValue = v;
    return celix_hashMap_putValue(map, strKey, longKey, &value, NULL);
}

static bool celix_hashMap_remove(celix_hash_map_t* map, const char* strKey, long longKey) {
    celix_hash_map_key_t key;
    if (map->keyType == CELIX_HASH_MAP_STRING_KEY) {
        key.strKey = strKey;
    } else {
        key.longKey = longKey;
    }

    unsigned int hash = map->hashKeyFunction(&key);
    unsigned int index = celix_hashMap_indexFor(hash, map->bucketsSize);
    celix_hash_map_entry_t* visit = map->buckets[index];
    celix_hash_map_entry_t* removedEntry = NULL;
    celix_hash_map_entry_t* prev = NULL;

    while (visit != NULL) {
        if (visit->hash == hash && map->equalsKeyFunction(&key, &visit->key)) {
            //hash & equals -> found entry
            map->size--;
            if (map->buckets[index] == visit) {
                //current entry is first entry in bucket, set next to first entry in bucket
                map->buckets[index] = visit->next;
            } else {
                //entry is in between, update link of prev to next entry
                prev->next = visit->next;
            }
            removedEntry = visit;
            break;
        }
        prev = visit;
        visit = visit->next;
    }
    if (removedEntry != NULL) {
        char* removedKey = NULL;
        if (map->keyType == CELIX_HASH_MAP_STRING_KEY) {
            removedKey = (char*)removedEntry->key.strKey;
        }
        celix_hashMap_destroyRemovedEntry(map, removedEntry);
        if (removedKey) {
            celix_hashMap_destroyRemovedKey(map, removedKey);
        }
        return true;
    }
    return false;
}

celix_status_t celix_hashMap_init(
        celix_hash_map_t* map,
        celix_hash_map_key_type_e keyType,
        unsigned int initialCapacity,
        double loadFactor,
        unsigned int (*hashKeyFn)(const celix_hash_map_key_t*),
        bool (*equalsKeyFn)(const celix_hash_map_key_t*, const celix_hash_map_key_t*)) {
    map->loadFactor = loadFactor;
    map->size = 0;
    map->bucketsSize = initialCapacity;
    map->keyType = keyType;
    memset(&map->emptyValue, 0, sizeof(map->emptyValue));
    map->hashKeyFunction = hashKeyFn;
    map->equalsKeyFunction = equalsKeyFn;
    map->simpleRemovedCallback = NULL;
    map->removedCallbackData = NULL;
    map->removedLongEntryCallback = NULL;
    map->removedStringEntryCallback = NULL;
    map->removedStringKeyCallback = NULL;
    map->storeKeysWeakly = false;

    map->buckets = calloc(initialCapacity, sizeof(celix_hash_map_entry_t*));
    return map->buckets == NULL ? CELIX_ENOMEM : CELIX_SUCCESS;
}

static void celix_hashMap_clear(celix_hash_map_t* map) {
    for (unsigned int i = 0; i < map->bucketsSize; i++) {
        celix_hash_map_entry_t* entry = map->buckets[i];
        while (entry != NULL) {
            celix_hash_map_entry_t* removedEntry = entry;
            entry = entry->next;
            char* removedKey = NULL;
            if (map->keyType == CELIX_HASH_MAP_STRING_KEY) {
                removedKey = (char*)removedEntry->key.strKey;
            }
            celix_hashMap_destroyRemovedEntry(map, removedEntry);
            if (removedKey) {
                celix_hashMap_destroyRemovedKey(map, removedKey);
            }
        }
        map->buckets[i] = NULL;
    }
    map->size = 0;
}

static celix_hash_map_entry_t* celix_hashMap_firstEntry(const celix_hash_map_t* map) {
    celix_hash_map_entry_t* entry = NULL;
    for (unsigned int index = 0; index < map->bucketsSize; ++index) {
        entry = map->buckets[index];
        if (entry != NULL) {
            break;
        }
    }
    return entry;
}

static celix_hash_map_entry_t* celix_hashMap_nextEntry(const celix_hash_map_t* map, celix_hash_map_entry_t* entry) {
    if (entry == NULL) {
        //end entry, just return NULL
        return NULL;
    }

    celix_hash_map_entry_t* next = NULL;
    if (entry != NULL) {
        if (entry->next != NULL) {
            next = entry->next;
        } else {
            unsigned int index = celix_hashMap_indexFor(entry->hash, map->bucketsSize) + 1; //next index
            while (index < map->bucketsSize) {
                next = map->buckets[index++];
                if (next != NULL) {
                    break;
                }
            }
        }
    }
    return next;
}


celix_string_hash_map_t* celix_stringHashMap_createWithOptions(const celix_string_hash_map_create_options_t* opts) {
    celix_autofree celix_string_hash_map_t* map = calloc(1, sizeof(*map));
    if (!map) {
        celix_err_push("Cannot allocate memory for hash map");
        return NULL;
    }

    unsigned int cap = opts->initialCapacity > 0 ? opts->initialCapacity : DEFAULT_INITIAL_CAPACITY;
    double fac = opts->loadFactor > 0 ? opts->loadFactor : DEFAULT_LOAD_FACTOR;
    celix_status_t status = celix_hashMap_init(&map->genericMap, CELIX_HASH_MAP_STRING_KEY, cap, fac, celix_stringHashMap_hash, celix_stringHashMap_keyEquals);
    if (status != CELIX_SUCCESS) {
        celix_err_push("Cannot initialize hash map");
        return NULL;
    }

    map->genericMap.simpleRemovedCallback = opts->simpleRemovedCallback;
    map->genericMap.removedCallbackData = opts->removedCallbackData;
    map->genericMap.removedStringEntryCallback = opts->removedCallback;
    map->genericMap.removedStringKeyCallback = opts->removedKeyCallback;
    map->genericMap.storeKeysWeakly = opts->storeKeysWeakly;

    return celix_steal_ptr(map);
}

celix_string_hash_map_t* celix_stringHashMap_create() {
    celix_string_hash_map_create_options_t opts = CELIX_EMPTY_STRING_HASH_MAP_CREATE_OPTIONS;
    return celix_stringHashMap_createWithOptions(&opts);
}

celix_long_hash_map_t* celix_longHashMap_createWithOptions(const celix_long_hash_map_create_options_t* opts) {
    celix_autofree celix_long_hash_map_t* map = calloc(1, sizeof(*map));

    if (!map) {
        celix_err_push("Cannot allocate memory for hash map");
        return NULL;
    }

    unsigned int cap = opts->initialCapacity > 0 ? opts->initialCapacity : DEFAULT_INITIAL_CAPACITY;
    double fac = opts->loadFactor > 0 ? opts->loadFactor : DEFAULT_LOAD_FACTOR;
    celix_status_t status = celix_hashMap_init(&map->genericMap, CELIX_HASH_MAP_LONG_KEY, cap, fac, celix_longHashMap_hash, celix_longHashMap_keyEquals);
    if (status != CELIX_SUCCESS) {
        celix_err_push("Cannot initialize hash map");
        return NULL;
    }

    map->genericMap.simpleRemovedCallback = opts->simpleRemovedCallback;
    map->genericMap.removedCallbackData = opts->removedCallbackData;
    map->genericMap.removedLongEntryCallback = opts->removedCallback;
    map->genericMap.storeKeysWeakly = false;

    return celix_steal_ptr(map);
}

celix_long_hash_map_t* celix_longHashMap_create() {
    celix_long_hash_map_create_options_t opts = CELIX_EMPTY_LONG_HASH_MAP_CREATE_OPTIONS;
    return celix_longHashMap_createWithOptions(&opts);
}

void celix_stringHashMap_destroy(celix_string_hash_map_t* map) {
    if (map != NULL) {
        celix_hashMap_clear(&map->genericMap);
        free(map->genericMap.buckets);
        free(map);
    }
}

void celix_longHashMap_destroy(celix_long_hash_map_t* map) {
    if (map != NULL) {
        celix_hashMap_clear(&map->genericMap);
        free(map->genericMap.buckets);
        free(map);
    }
}

size_t celix_stringHashMap_size(const celix_string_hash_map_t* map) {
    return map->genericMap.size;
}

size_t celix_longHashMap_size(const celix_long_hash_map_t* map) {
    return map->genericMap.size;
}

void* celix_stringHashMap_get(const celix_string_hash_map_t* map, const char* key) {
    return celix_hashMap_get(&map->genericMap, key, 0);
}

void* celix_longHashMap_get(const celix_long_hash_map_t* map, long key) {
    return celix_hashMap_get(&map->genericMap, NULL, key);
}

long celix_stringHashMap_getLong(const celix_string_hash_map_t* map, const char* key, long fallbackValue) {
    return celix_hashMap_getLong(&map->genericMap, key, 0, fallbackValue);
}

long celix_longHashMap_getLong(const celix_long_hash_map_t* map, long key, long fallbackValue) {
    return celix_hashMap_getLong(&map->genericMap, NULL, key, fallbackValue);

}

double celix_stringHashMap_getDouble(const celix_string_hash_map_t* map, const char* key, double fallbackValue) {
    return celix_hashMap_getDouble(&map->genericMap, key, 0, fallbackValue);
}

double celix_longHashMap_getDouble(const celix_long_hash_map_t* map, long key, double fallbackValue) {
    return celix_hashMap_getDouble(&map->genericMap, NULL, key, fallbackValue);
}

bool celix_stringHashMap_getBool(const celix_string_hash_map_t* map, const char* key, bool fallbackValue) {
    return celix_hashMap_getBool(&map->genericMap, key, 0, fallbackValue);
}

bool celix_longHashMap_getBool(const celix_long_hash_map_t* map, long key, bool fallbackValue) {
    return celix_hashMap_getBool(&map->genericMap, NULL, key, fallbackValue);
}

celix_status_t celix_stringHashMap_put(celix_string_hash_map_t* map, const char* key, void* value) {
    return celix_hashMap_put(&map->genericMap, key, 0, value, NULL);
}

celix_status_t celix_longHashMap_put(celix_long_hash_map_t* map, long key, void* value) {
    return celix_hashMap_put(&map->genericMap, NULL, key, value, NULL);
}

celix_status_t celix_stringHashMap_putLong(celix_string_hash_map_t* map, const char* key, long value) {
    return celix_hashMap_putLong(&map->genericMap, key, 0, value, NULL);
}

celix_status_t celix_longHashMap_putLong(celix_long_hash_map_t* map, long key, long value) {
    return celix_hashMap_putLong(&map->genericMap, NULL, key, value, NULL);
}

celix_status_t celix_stringHashMap_putDouble(celix_string_hash_map_t* map, const char* key, double value) {
    return celix_hashMap_putDouble(&map->genericMap, key, 0, value);
}

celix_status_t celix_longHashMap_putDouble(celix_long_hash_map_t* map, long key, double value) {
    return celix_hashMap_putDouble(&map->genericMap, NULL, key, value);
}

celix_status_t celix_stringHashMap_putBool(celix_string_hash_map_t* map, const char* key, bool value) {
    return celix_hashMap_putBool(&map->genericMap, key, 0, value, NULL);
}

celix_status_t celix_longHashMap_putBool(celix_long_hash_map_t* map, long key, bool value) {
    return celix_hashMap_putBool(&map->genericMap, NULL, key, value, NULL);
}

bool celix_stringHashMap_hasKey(const celix_string_hash_map_t* map, const char* key) {
    return celix_hashMap_hasKey(&map->genericMap, key, 0);
}

bool celix_longHashMap_hasKey(const celix_long_hash_map_t* map, long key) {
    return celix_hashMap_hasKey(&map->genericMap, NULL, key);
}

bool celix_stringHashMap_remove(celix_string_hash_map_t* map, const char* key) {
    return celix_hashMap_remove(&map->genericMap, key, 0);
}

bool celix_longHashMap_remove(celix_long_hash_map_t* map, long key) {
    return celix_hashMap_remove(&map->genericMap, NULL, key);
}

void celix_stringHashMap_clear(celix_string_hash_map_t* map) {
    celix_hashMap_clear(&map->genericMap);
}

void celix_longHashMap_clear(celix_long_hash_map_t* map) {
    celix_hashMap_clear(&map->genericMap);
}

static bool celix_hashMap_equals(const celix_hash_map_t* map1, const celix_hash_map_t* map2) {
    if (map1 == map2) {
        return true;
    }

    if (map1->size != map2->size) {
        return false;
    }

    for (celix_hash_map_entry_t* entry = celix_hashMap_firstEntry(map1); entry != NULL; entry = celix_hashMap_nextEntry(map1, entry)) {
        celix_hash_map_entry_t* entryMap2 = celix_hashMap_getEntry(map2, entry->key.strKey, entry->key.longKey);

        //note using memcmp, so for void* values the pointer value is compared, not the value itself.
        if (entryMap2 == NULL || memcmp(&entryMap2->value, &entry->value, sizeof(entryMap2->value)) != 0) {
            return false;
        }
    }
    return true;
}

bool celix_stringHashMap_equals(const celix_string_hash_map_t* map1, const celix_string_hash_map_t* map2) {
    if (map1 == NULL && map2 == NULL) {
        return true;
    }
    if (map1 == NULL || map2 == NULL) {
        return false;
    }
    return celix_hashMap_equals(&map1->genericMap, &map2->genericMap);
}

bool celix_longHashMap_equals(const celix_long_hash_map_t* map1, const celix_long_hash_map_t* map2) {
    if (map1 == NULL && map2 == NULL) {
        return true;
    }
    if (map1 == NULL || map2 == NULL) {
        return false;
    }
    if (map1->genericMap.size != map2->genericMap.size) {
        return false;
    }
    return celix_hashMap_equals(&map1->genericMap, &map2->genericMap);
}

celix_string_hash_map_iterator_t celix_stringHashMap_begin(const celix_string_hash_map_t* map) {
    celix_string_hash_map_iterator_t iter;
    memset(&iter, 0, sizeof(iter));
    iter._internal[0] = (void*)&map->genericMap;
    if (map->genericMap.size > 0) {
        celix_hash_map_entry_t *entry = celix_hashMap_firstEntry(&map->genericMap);
        iter._internal[1] = entry;
        iter.value = entry->value;
        iter.key = entry->key.strKey;
    }
    return iter;
}

celix_long_hash_map_iterator_t celix_longHashMap_begin(const celix_long_hash_map_t* map) {
    celix_long_hash_map_iterator_t iter;
    memset(&iter, 0, sizeof(iter));
    iter._internal[0] = (void*)&map->genericMap;
    if (map->genericMap.size > 0) {
        celix_hash_map_entry_t *entry = celix_hashMap_firstEntry(&map->genericMap);
        iter._internal[1] = entry;
        iter.value = entry->value;
        iter.key = entry->key.longKey;
    }
    return iter;
}

celix_string_hash_map_iterator_t celix_stringHashMap_end(const celix_string_hash_map_t* map) {
    celix_string_hash_map_iterator_t iter;
    iter._internal[0] = (void*)&map->genericMap;
    iter._internal[1] = NULL;
    iter.index = map->genericMap.size;
    iter.key = "";
    memset(&iter.value, 0, sizeof(iter.value));
    return iter;
}

celix_long_hash_map_iterator_t celix_longHashMap_end(const celix_long_hash_map_t* map) {
    celix_long_hash_map_iterator_t iter;
    iter._internal[0] = (void*)&map->genericMap;
    iter._internal[1] = NULL;
    iter.index = map->genericMap.size;
    iter.key = 0L;
    memset(&iter.value, 0, sizeof(iter.value));
    return iter;
}

bool celix_stringHashMapIterator_isEnd(const celix_string_hash_map_iterator_t* iter) {
    return iter->_internal[1] == NULL; //check if entry is NULL
}

bool celix_longHashMapIterator_isEnd(const celix_long_hash_map_iterator_t* iter) {
    return iter->_internal[1] == NULL; //check if entry is NULL
}

void celix_stringHashMapIterator_next(celix_string_hash_map_iterator_t* iter) {
    const celix_hash_map_t* map = iter->_internal[0];
    celix_hash_map_entry_t *entry = iter->_internal[1];
    iter->index += 1;
    entry = celix_hashMap_nextEntry(map, entry);
    if (entry) {
        iter->_internal[1] = entry;
        iter->key = entry->key.strKey;
        iter->value = entry->value;
    } else {
        iter->_internal[1] = NULL;
        iter->key = NULL;
        memset(&iter->value, 0, sizeof(iter->value));
    }
}

void celix_longHashMapIterator_next(celix_long_hash_map_iterator_t* iter) {
    const celix_hash_map_t* map = iter->_internal[0];
    celix_hash_map_entry_t *entry = iter->_internal[1];
    iter->index += 1;
    entry = celix_hashMap_nextEntry(map, entry);
    if (entry) {
        iter->_internal[1] = entry;
        iter->key = entry->key.longKey;
        iter->value = entry->value;
    } else {
        iter->_internal[1] = NULL;
        iter->key = 0L;
        memset(&iter->value, 0, sizeof(iter->value));
    }
}

bool celix_stringHashMapIterator_equals(
        const celix_string_hash_map_iterator_t* iterator,
        const celix_string_hash_map_iterator_t* other) {
    return iterator->_internal[0] == other->_internal[0] /* same map */ &&
           iterator->_internal[1] == other->_internal[1] /* same entry */;
}

bool celix_longHashMapIterator_equals(
        const celix_long_hash_map_iterator_t* iterator,
        const celix_long_hash_map_iterator_t* other) {
    return iterator->_internal[0] == other->_internal[0] /* same map */ &&
           iterator->_internal[1] == other->_internal[1] /* same entry */;
}

void celix_stringHashMapIterator_remove(celix_string_hash_map_iterator_t* iter) {
    celix_hash_map_t* map = iter->_internal[0];
    celix_hash_map_entry_t *entry = iter->_internal[1];
    const char* key = entry->key.strKey;
    celix_stringHashMapIterator_next(iter);
    celix_hashMap_remove(map, key, 0);
}

void celix_longHashMapIterator_remove(celix_long_hash_map_iterator_t* iter) {
    celix_hash_map_t* map = iter->_internal[0];
    celix_hash_map_entry_t *entry = iter->_internal[1];
    long key = entry->key.longKey;
    celix_longHashMapIterator_next(iter);
    celix_hashMap_remove(map, NULL, key);
}
