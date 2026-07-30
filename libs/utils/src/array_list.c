/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *  KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <assert.h>

#include <stdlib.h>
#include <string.h>

#include "celix_array_list.h"
#include "celix_err.h"
#include "celix_properties.h"
#include "celix_stdlib_cleanup.h"
#include "celix_utils.h"
#include "celix_version.h"

#define STRING_VALUE_UNDEFINED_EL_TYPE "Undefined"
#define STRING_VALUE_POINTER_EL_TYPE "Pointer"
#define STRING_VALUE_STRING_EL_TYPE "String"
#define STRING_VALUE_LONG_EL_TYPE "Long"
#define STRING_VALUE_DOUBLE_EL_TYPE "Double"
#define STRING_VALUE_BOOL_EL_TYPE "Bool"
#define STRING_VALUE_VERSION_EL_TYPE "Version"
#define STRING_VALUE_PROPERTIES_EL_TYPE "Properties"
#define STRING_VALUE_ARRAY_LIST_EL_TYPE "ArrayList"
#define STRING_VALUE_VARIANT_EL_TYPE "Variant"

struct celix_array_list {
    celix_array_list_element_type_t elementType;
    celix_array_list_entry_t* elementData;
    size_t size;
    size_t capacity;
    celix_arrayList_equals_fp equalsCallback;
    celix_array_list_compare_entries_fp compareCallback;
    celix_array_list_copy_entry_fp copyCallback;
    void (*simpleRemovedCallback)(void* value);
    void* removedCallbackData;
    void (*removedCallback)(void* data, celix_array_list_entry_t entry);
};

static bool celix_arrayList_undefinedEquals(celix_array_list_entry_t a, celix_array_list_entry_t b) {
    return memcmp(&a.voidPtrVal, &b.voidPtrVal, sizeof(a)) == 0;
}

static int celix_arrayList_comparePtrEntries(celix_array_list_entry_t a, celix_array_list_entry_t b) {
    return a.voidPtrVal > b.voidPtrVal ? 1 : (a.voidPtrVal < b.voidPtrVal ? -1 : 0);
}

static bool celix_arrayList_ptrEquals(celix_array_list_entry_t a, celix_array_list_entry_t b) {
    return celix_arrayList_comparePtrEntries(a, b) == 0;
}

static int celix_arrayList_compareStringEntries(celix_array_list_entry_t a, celix_array_list_entry_t b) {
    return strcmp(a.stringVal, b.stringVal);
}

static bool celix_arrayList_stringEquals(celix_array_list_entry_t a, celix_array_list_entry_t b) {
    return celix_utils_stringEquals(a.stringVal, b.stringVal);
}

static int celix_arrayList_compareLongEntries(celix_array_list_entry_t a, celix_array_list_entry_t b) {
    return a.longVal > b.longVal ? 1 : (a.longVal < b.longVal ? -1 : 0);
}

static bool celix_arrayList_longEquals(celix_array_list_entry_t a, celix_array_list_entry_t b) {
    return celix_arrayList_compareLongEntries(a, b) == 0;
}

static int celix_arrayList_compareDoubleEntries(celix_array_list_entry_t a, celix_array_list_entry_t b) {
    return a.doubleVal > b.doubleVal ? 1 : (a.doubleVal < b.doubleVal ? -1 : 0);
}

static bool celix_arrayList_doubleEquals(celix_array_list_entry_t a, celix_array_list_entry_t b) {
    return celix_arrayList_compareDoubleEntries(a, b) == 0;
}

static int celix_arrayList_compareBoolEntries(celix_array_list_entry_t a, celix_array_list_entry_t b) {
    return a.boolVal - b.boolVal;
}

static bool celix_arrayList_boolEquals(celix_array_list_entry_t a, celix_array_list_entry_t b) {
    return celix_arrayList_compareBoolEntries(a, b) == 0;
}

static int celix_arrayList_compareVersionEntries(celix_array_list_entry_t a, celix_array_list_entry_t b) {
    return celix_version_compareTo(a.versionVal, b.versionVal);
}

static bool celix_arrayList_versionEquals(celix_array_list_entry_t a, celix_array_list_entry_t b) {
    return celix_arrayList_compareVersionEntries(a, b) == 0;
}

inline static bool
celix_arrayList_equalsForElement(celix_array_list_t* list, celix_array_list_entry_t a, celix_array_list_entry_t b) {
    // by class invariant, equalsCallback is never NULL
    return list->equalsCallback(a, b);
}

static celix_status_t celix_arrayList_copyStringEntry(celix_array_list_entry_t src, celix_array_list_entry_t* dst) {
    assert(dst);
    dst->stringVal = celix_utils_strdup(src.stringVal);
    if (!dst->stringVal) {
        return ENOMEM;
    }
    return CELIX_SUCCESS;
}

static celix_status_t celix_arrayList_copyVersionEntry(celix_array_list_entry_t src, celix_array_list_entry_t* dst) {
    assert(dst);
    dst->versionVal = celix_version_copy(src.versionVal);
    if (!dst->versionVal) {
        return ENOMEM;
    }
    return CELIX_SUCCESS;
}

static void celix_arrayList_destroyVersion(void* v) {
    celix_version_t* version = v;
    celix_version_destroy(version);
}

static void celix_arrayList_destroyProperties(void* value) { celix_properties_destroy(value); }

static void celix_arrayList_destroyArrayList(void* value) { celix_arrayList_destroy(value); }

static void celix_arrayList_destroyVariant(void* value) {
    celix_array_list_variant_t* variant = value;
    if (!variant) {
        return;
    }
    switch (variant->type) {
    case CELIX_ARRAY_LIST_VARIANT_TYPE_STRING:
        free((char*)variant->value.stringValue);
        break;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_VERSION:
        celix_version_destroy((celix_version_t*)variant->value.versionValue);
        break;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_PROPERTIES:
        celix_properties_destroy((celix_properties_t*)variant->value.propertiesValue);
        break;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_ARRAY_LIST:
        celix_arrayList_destroy((celix_array_list_t*)variant->value.arrayListValue);
        break;
    default:
        break;
    }
    free(variant);
}

static celix_status_t celix_arrayList_copyVariantValue(const celix_array_list_variant_t* src,
                                                       celix_array_list_variant_t** dst) {
    celix_array_list_variant_t* copy = calloc(1, sizeof(*copy));
    if (!copy) {
        celix_err_push("Failed to allocate memory for variant array-list entry");
        return CELIX_ENOMEM;
    }
    copy->type = src->type;
    celix_status_t status = CELIX_SUCCESS;
    switch (src->type) {
    case CELIX_ARRAY_LIST_VARIANT_TYPE_NULL:
        break;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_STRING:
        if (!src->value.stringValue) {
            status = CELIX_ILLEGAL_ARGUMENT;
            break;
        }
        copy->value.stringValue = celix_utils_strdup(src->value.stringValue);
        status = copy->value.stringValue ? CELIX_SUCCESS : CELIX_ENOMEM;
        break;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_LONG:
        copy->value.longValue = src->value.longValue;
        break;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_DOUBLE:
        copy->value.doubleValue = src->value.doubleValue;
        break;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_BOOL:
        copy->value.boolValue = src->value.boolValue;
        break;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_VERSION:
        if (!src->value.versionValue) {
            status = CELIX_ILLEGAL_ARGUMENT;
            break;
        }
        copy->value.versionValue = celix_version_copy(src->value.versionValue);
        status = copy->value.versionValue ? CELIX_SUCCESS : CELIX_ENOMEM;
        break;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_PROPERTIES:
        if (!src->value.propertiesValue) {
            status = CELIX_ILLEGAL_ARGUMENT;
            break;
        }
        copy->value.propertiesValue = celix_properties_copy(src->value.propertiesValue);
        status = copy->value.propertiesValue ? CELIX_SUCCESS : CELIX_ENOMEM;
        break;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_ARRAY_LIST:
        if (!src->value.arrayListValue) {
            status = CELIX_ILLEGAL_ARGUMENT;
            break;
        }
        copy->value.arrayListValue = celix_arrayList_copy(src->value.arrayListValue);
        status = copy->value.arrayListValue ? CELIX_SUCCESS : CELIX_ENOMEM;
        break;
    default:
        status = CELIX_ILLEGAL_ARGUMENT;
        break;
    }
    if (status != CELIX_SUCCESS) {
        if (status == CELIX_ILLEGAL_ARGUMENT) {
            celix_err_push("Invalid variant array-list entry");
        }
        celix_arrayList_destroyVariant(copy);
        return status;
    }
    *dst = copy;
    return CELIX_SUCCESS;
}

static celix_status_t celix_arrayList_copyPropertiesEntry(celix_array_list_entry_t src, celix_array_list_entry_t* dst) {
    dst->propertiesVal = celix_properties_copy(src.propertiesVal);
    return dst->propertiesVal ? CELIX_SUCCESS : CELIX_ENOMEM;
}

static celix_status_t celix_arrayList_copyArrayListEntry(celix_array_list_entry_t src, celix_array_list_entry_t* dst) {
    dst->arrayListVal = celix_arrayList_copy(src.arrayListVal);
    return dst->arrayListVal ? CELIX_SUCCESS : CELIX_ENOMEM;
}

static celix_status_t celix_arrayList_copyVariantEntry(celix_array_list_entry_t src, celix_array_list_entry_t* dst) {
    celix_array_list_variant_t* copy = NULL;
    celix_status_t status = celix_arrayList_copyVariantValue(src.variantVal, &copy);
    dst->variantVal = copy;
    return status;
}

static bool celix_arrayList_propertiesEquals(celix_array_list_entry_t a, celix_array_list_entry_t b) {
    return celix_properties_equals(a.propertiesVal, b.propertiesVal);
}

static bool celix_arrayList_arrayListEquals(celix_array_list_entry_t a, celix_array_list_entry_t b) {
    return celix_arrayList_equals(a.arrayListVal, b.arrayListVal);
}

static bool celix_arrayList_variantEquals(celix_array_list_entry_t a, celix_array_list_entry_t b) {
    const celix_array_list_variant_t* va = a.variantVal;
    const celix_array_list_variant_t* vb = b.variantVal;
    if (va == vb) {
        return true;
    }
    if (!va || !vb || va->type != vb->type) {
        return false;
    }
    switch (va->type) {
    case CELIX_ARRAY_LIST_VARIANT_TYPE_NULL:
        return true;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_STRING:
        return celix_utils_stringEquals(va->value.stringValue, vb->value.stringValue);
    case CELIX_ARRAY_LIST_VARIANT_TYPE_LONG:
        return va->value.longValue == vb->value.longValue;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_DOUBLE:
        return va->value.doubleValue == vb->value.doubleValue;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_BOOL:
        return va->value.boolValue == vb->value.boolValue;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_VERSION:
        return celix_version_compareTo(va->value.versionValue, vb->value.versionValue) == 0;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_PROPERTIES:
        return celix_properties_equals(va->value.propertiesValue, vb->value.propertiesValue);
    case CELIX_ARRAY_LIST_VARIANT_TYPE_ARRAY_LIST:
        return celix_arrayList_equals(va->value.arrayListValue, vb->value.arrayListValue);
    default:
        return false;
    }
}

static void celix_arrayList_callRemovedCallback(celix_array_list_t* list, int index) {
    celix_array_list_entry_t entry = list->elementData[index];
    if (list->simpleRemovedCallback != NULL) {
        list->simpleRemovedCallback(entry.voidPtrVal);
    } else if (list->removedCallback != NULL) {
        list->removedCallback(list->removedCallbackData, entry);
    }
}

static celix_status_t celix_arrayList_ensureCapacity(celix_array_list_t* list, size_t capacity) {
    celix_array_list_entry_t* newList;
    size_t oldCapacity = list->capacity;
    if (capacity > oldCapacity) {
        size_t newCapacity = (oldCapacity * 3) / 2 + 1;
        newList = realloc(list->elementData, sizeof(celix_array_list_entry_t) * newCapacity);
        if (!newList) {
            celix_err_push("Failed to reallocate memory for elementData");
            return ENOMEM;
        }
        list->capacity = newCapacity;
        list->elementData = newList;
    }
    return CELIX_SUCCESS;
}

static void celix_arrayList_setTypeSpecificCallbacks(celix_array_list_t* list) {
    switch (list->elementType) {
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_POINTER:
        list->equalsCallback = celix_arrayList_ptrEquals;
        list->compareCallback = celix_arrayList_comparePtrEntries;
        break;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING:
        list->simpleRemovedCallback = free;
        list->equalsCallback = celix_arrayList_stringEquals;
        list->compareCallback = celix_arrayList_compareStringEntries;
        list->copyCallback = celix_arrayList_copyStringEntry;
        break;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_LONG:
        list->equalsCallback = celix_arrayList_longEquals;
        list->compareCallback = celix_arrayList_compareLongEntries;
        break;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_DOUBLE:
        list->equalsCallback = celix_arrayList_doubleEquals;
        list->compareCallback = celix_arrayList_compareDoubleEntries;
        break;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_BOOL:
        list->equalsCallback = celix_arrayList_boolEquals;
        list->compareCallback = celix_arrayList_compareBoolEntries;
        break;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_VERSION:
        list->simpleRemovedCallback = celix_arrayList_destroyVersion;
        list->equalsCallback = celix_arrayList_versionEquals;
        list->compareCallback = celix_arrayList_compareVersionEntries;
        list->copyCallback = celix_arrayList_copyVersionEntry;
        break;

    case CELIX_ARRAY_LIST_ELEMENT_TYPE_PROPERTIES:
        list->simpleRemovedCallback = celix_arrayList_destroyProperties;
        list->equalsCallback = celix_arrayList_propertiesEquals;
        list->copyCallback = celix_arrayList_copyPropertiesEntry;
        break;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_ARRAY_LIST:
        list->simpleRemovedCallback = celix_arrayList_destroyArrayList;
        list->equalsCallback = celix_arrayList_arrayListEquals;
        list->copyCallback = celix_arrayList_copyArrayListEntry;
        break;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_VARIANT:
        list->simpleRemovedCallback = celix_arrayList_destroyVariant;
        list->equalsCallback = celix_arrayList_variantEquals;
        list->copyCallback = celix_arrayList_copyVariantEntry;
        break;
    default:
        assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_UNDEFINED);
        list->equalsCallback = celix_arrayList_undefinedEquals;
        break;
    }
}

celix_array_list_t* celix_arrayList_createWithOptions(const celix_array_list_create_options_t* opts) {
    celix_autofree celix_array_list_t* list = calloc(1, sizeof(*list));
    if (!list) {
        celix_err_push("Failed to allocate memory for list");
        return NULL;
    }

    size_t initialCap = opts->initialCapacity > 0 ? opts->initialCapacity : 10;
    list->capacity = initialCap;
    list->elementData = calloc(list->capacity, sizeof(celix_array_list_entry_t));
    if (!list->elementData) {
        celix_err_push("Failed to allocate memory for elementData");
        return NULL;
    }

    list->elementType = opts->elementType;
    celix_arrayList_setTypeSpecificCallbacks(list);

    // if opts contains callbacks, use them and override the default ones
    if (opts->simpleRemovedCallback) {
        list->simpleRemovedCallback = opts->simpleRemovedCallback;
    }
    if (opts->removedCallback) {
        list->removedCallback = opts->removedCallback;
        list->removedCallbackData = opts->removedCallbackData;
    }
    if (opts->equalsCallback) {
        list->equalsCallback = opts->equalsCallback;
    }
    if (opts->compareCallback) {
        list->compareCallback = opts->compareCallback;
    }
    if (opts->copyCallback) {
        list->copyCallback = opts->copyCallback;
    }
    return celix_steal_ptr(list);
}

static celix_array_list_t* celix_arrayList_createTypedArray(celix_array_list_element_type_t type) {
    celix_array_list_create_options_t opts = CELIX_EMPTY_ARRAY_LIST_CREATE_OPTIONS;
    opts.elementType = type;
    return celix_arrayList_createWithOptions(&opts);
}

celix_array_list_t* celix_arrayList_create() {
    return celix_arrayList_createTypedArray(CELIX_ARRAY_LIST_ELEMENT_TYPE_UNDEFINED);
}

celix_array_list_t* celix_arrayList_createPointerArray() {
    return celix_arrayList_createTypedArray(CELIX_ARRAY_LIST_ELEMENT_TYPE_POINTER);
}

celix_array_list_t* celix_arrayList_createStringArray() {
    return celix_arrayList_createTypedArray(CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING);
}

celix_array_list_t* celix_arrayList_createLongArray() {
    return celix_arrayList_createTypedArray(CELIX_ARRAY_LIST_ELEMENT_TYPE_LONG);
}

celix_array_list_t* celix_arrayList_createDoubleArray() {
    return celix_arrayList_createTypedArray(CELIX_ARRAY_LIST_ELEMENT_TYPE_DOUBLE);
}

celix_array_list_t* celix_arrayList_createBoolArray() {
    return celix_arrayList_createTypedArray(CELIX_ARRAY_LIST_ELEMENT_TYPE_BOOL);
}

celix_array_list_t* celix_arrayList_createVersionArray() {
    return celix_arrayList_createTypedArray(CELIX_ARRAY_LIST_ELEMENT_TYPE_VERSION);
}

celix_array_list_t* celix_arrayList_createPropertiesArray(void) {
    return celix_arrayList_createTypedArray(CELIX_ARRAY_LIST_ELEMENT_TYPE_PROPERTIES);
}

celix_array_list_t* celix_arrayList_createArrayListArray(void) {
    return celix_arrayList_createTypedArray(CELIX_ARRAY_LIST_ELEMENT_TYPE_ARRAY_LIST);
}

celix_array_list_t* celix_arrayList_createVariantArray(void) {
    return celix_arrayList_createTypedArray(CELIX_ARRAY_LIST_ELEMENT_TYPE_VARIANT);
}

void celix_arrayList_destroy(celix_array_list_t* list) {
    if (list != NULL) {
        celix_arrayList_clear(list);
        free(list->elementData);
        free(list);
    }
}

celix_array_list_element_type_t celix_arrayList_getElementType(const celix_array_list_t* list) {
    return list->elementType;
}

int celix_arrayList_size(const celix_array_list_t* list) { return (int)list->size; }

static celix_array_list_entry_t arrayList_getEntry(const celix_array_list_t* list, int index) {
    celix_array_list_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    if (index < list->size) {
        entry = list->elementData[index];
    }
    return entry;
}

celix_array_list_entry_t celix_arrayList_getEntry(const celix_array_list_t* list, int index) {
    return arrayList_getEntry(list, index);
}

void* celix_arrayList_get(const celix_array_list_t* list, int index) {
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_POINTER ||
           list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_UNDEFINED);
    return arrayList_getEntry(list, index).voidPtrVal;
}

const char* celix_arrayList_getString(const celix_array_list_t* list, int index) {
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING ||
           list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_UNDEFINED);
    return arrayList_getEntry(list, index).stringVal;
}

long int celix_arrayList_getLong(const celix_array_list_t* list, int index) {
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_LONG ||
           list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_UNDEFINED);
    return arrayList_getEntry(list, index).longVal;
}

double celix_arrayList_getDouble(const celix_array_list_t* list, int index) {
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_DOUBLE ||
           list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_UNDEFINED);
    return arrayList_getEntry(list, index).doubleVal;
}

bool celix_arrayList_getBool(const celix_array_list_t* list, int index) {
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_BOOL ||
           list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_UNDEFINED);
    return arrayList_getEntry(list, index).boolVal;
}

const celix_version_t* celix_arrayList_getVersion(const celix_array_list_t* list, int index) {
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_VERSION);
    return arrayList_getEntry(list, index).versionVal;
}

const celix_properties_t* celix_arrayList_getProperties(const celix_array_list_t* list, int index) {
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_PROPERTIES);
    return arrayList_getEntry(list, index).propertiesVal;
}

const celix_array_list_t* celix_arrayList_getArrayList(const celix_array_list_t* list, int index) {
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_ARRAY_LIST);
    return arrayList_getEntry(list, index).arrayListVal;
}

const celix_array_list_variant_t* celix_arrayList_getVariant(const celix_array_list_t* list, int index) {
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_VARIANT);
    return arrayList_getEntry(list, index).variantVal;
}

static celix_status_t celix_arrayList_addEntry(celix_array_list_t* list, celix_array_list_entry_t entry) {
    celix_status_t status = celix_arrayList_ensureCapacity(list, list->size + 1);
    if (status == CELIX_SUCCESS) {
        list->elementData[list->size++] = entry;
    } else {
        if (list->simpleRemovedCallback) {
            list->simpleRemovedCallback(entry.voidPtrVal);
        } else if (list->removedCallback) {
            list->removedCallback(list->removedCallbackData, entry);
        }
    }
    return status;
}

celix_status_t celix_arrayList_add(celix_array_list_t* list, void* element) {
    assert(element);
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_POINTER ||
           list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_UNDEFINED);
    celix_array_list_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.voidPtrVal = element;
    return celix_arrayList_addEntry(list, entry);
}

celix_status_t celix_arrayList_addString(celix_array_list_t* list, const char* val) {
    assert(val);
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING ||
           list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_UNDEFINED);
    celix_array_list_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    if (list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING) {
        entry.stringVal = celix_utils_strdup(val);
        if (entry.stringVal == NULL) {
            return ENOMEM;
        }
    } else {
        entry.stringVal = val;
    }
    return celix_arrayList_addEntry(list, entry);
}

celix_status_t celix_arrayList_assignString(celix_array_list_t* list, char* value) {
    assert(value);
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING);
    celix_array_list_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.stringVal = value;
    return celix_arrayList_addEntry(list, entry);
}

celix_status_t celix_arrayList_addLong(celix_array_list_t* list, long val) {
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_LONG ||
           list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_UNDEFINED);
    celix_array_list_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.longVal = val;
    return celix_arrayList_addEntry(list, entry);
}

celix_status_t celix_arrayList_addDouble(celix_array_list_t* list, double val) {
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_DOUBLE ||
           list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_UNDEFINED);
    celix_array_list_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.doubleVal = val;
    return celix_arrayList_addEntry(list, entry);
}

celix_status_t celix_arrayList_addBool(celix_array_list_t* list, bool val) {
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_BOOL ||
           list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_UNDEFINED);
    celix_array_list_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.boolVal = val;
    return celix_arrayList_addEntry(list, entry);
}

celix_status_t celix_arrayList_addVersion(celix_array_list_t* list, const celix_version_t* value) {
    assert(value);
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_VERSION ||
           list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_UNDEFINED);
    celix_array_list_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    if (list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_VERSION) {
        entry.versionVal = celix_version_copy(value);
        if (entry.versionVal == NULL) {
            return ENOMEM;
        }
    } else {
        entry.versionVal = value;
    }
    return celix_arrayList_addEntry(list, entry);
}

celix_status_t celix_arrayList_assignVersion(celix_array_list_t* list, celix_version_t* value) {
    assert(value);
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_VERSION);
    celix_array_list_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.versionVal = value;
    return celix_arrayList_addEntry(list, entry);
}

celix_status_t celix_arrayList_addProperties(celix_array_list_t* list, const celix_properties_t* value) {
    assert(value);
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_PROPERTIES);
    celix_properties_t* copy = celix_properties_copy(value);
    if (!copy) {
        return CELIX_ENOMEM;
    }
    return celix_arrayList_assignProperties(list, copy);
}

celix_status_t celix_arrayList_assignProperties(celix_array_list_t* list, celix_properties_t* value) {
    assert(value);
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_PROPERTIES);
    celix_array_list_entry_t entry = {.propertiesVal = value};
    return celix_arrayList_addEntry(list, entry);
}

celix_status_t celix_arrayList_addArrayList(celix_array_list_t* list, const celix_array_list_t* value) {
    assert(value);
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_ARRAY_LIST);
    if (list == value) {
        celix_err_push("Cannot add an array list to itself");
        return CELIX_ILLEGAL_ARGUMENT;
    }
    celix_array_list_t* copy = celix_arrayList_copy(value);
    if (!copy) {
        return CELIX_ENOMEM;
    }
    return celix_arrayList_assignArrayList(list, copy);
}

celix_status_t celix_arrayList_assignArrayList(celix_array_list_t* list, celix_array_list_t* value) {
    assert(value);
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_ARRAY_LIST);
    if (list == value) {
        celix_err_push("Cannot assign an array list to itself");
        return CELIX_ILLEGAL_ARGUMENT;
    }
    celix_array_list_entry_t entry = {.arrayListVal = value};
    return celix_arrayList_addEntry(list, entry);
}

celix_status_t celix_arrayList_addVariant(celix_array_list_t* list, const celix_array_list_variant_t* value) {
    assert(value);
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_VARIANT);
    if (value->type == CELIX_ARRAY_LIST_VARIANT_TYPE_ARRAY_LIST && value->value.arrayListValue == list) {
        celix_err_push("Cannot add an array list to itself through a variant");
        return CELIX_ILLEGAL_ARGUMENT;
    }
    celix_array_list_variant_t* copy = NULL;
    celix_status_t status = celix_arrayList_copyVariantValue(value, &copy);
    if (status != CELIX_SUCCESS) {
        return status;
    }
    celix_array_list_entry_t entry = {.variantVal = copy};
    return celix_arrayList_addEntry(list, entry);
}

int celix_arrayList_indexOf(celix_array_list_t* list, celix_array_list_entry_t entry) {
    size_t size = celix_arrayList_size(list);
    int i;
    int index = -1;
    for (i = 0; i < size; ++i) {
        bool eq = celix_arrayList_equalsForElement(list, entry, list->elementData[i]);
        if (eq) {
            index = i;
            break;
        }
    }
    return index;
}
void celix_arrayList_removeAt(celix_array_list_t* list, int index) {
    if (index >= 0 && index < list->size) {
        celix_arrayList_callRemovedCallback(list, index);
        size_t numMoved = list->size - index - 1;
        memmove(list->elementData + index, list->elementData + index + 1, sizeof(celix_array_list_entry_t) * numMoved);
        memset(&list->elementData[--list->size], 0, sizeof(celix_array_list_entry_t));
    }
}

void celix_arrayList_removeEntry(celix_array_list_t* list, celix_array_list_entry_t entry) {
    int index = celix_arrayList_indexOf(list, entry);
    celix_arrayList_removeAt(list, index);
}

void celix_arrayList_remove(celix_array_list_t* list, void* ptr) {
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_POINTER ||
           list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_UNDEFINED);
    celix_array_list_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.voidPtrVal = ptr;
    celix_arrayList_removeEntry(list, entry);
}

void celix_arrayList_removeString(celix_array_list_t* list, const char* val) {
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING ||
           list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_UNDEFINED);
    celix_array_list_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.stringVal = val;
    celix_arrayList_removeEntry(list, entry);
}

void celix_arrayList_removeLong(celix_array_list_t* list, long val) {
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_LONG ||
           list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_UNDEFINED);
    celix_array_list_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.longVal = val;
    celix_arrayList_removeEntry(list, entry);
}

void celix_arrayList_removeDouble(celix_array_list_t* list, double val) {
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_DOUBLE ||
           list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_UNDEFINED);
    celix_array_list_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.doubleVal = val;
    celix_arrayList_removeEntry(list, entry);
}

void celix_arrayList_removeBool(celix_array_list_t* list, bool val) {
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_BOOL ||
           list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_UNDEFINED);
    celix_array_list_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.boolVal = val;
    celix_arrayList_removeEntry(list, entry);
}

void celix_arrayList_removeVersion(celix_array_list_t* list, const celix_version_t* val) {
    assert(list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_VERSION ||
           list->elementType == CELIX_ARRAY_LIST_ELEMENT_TYPE_UNDEFINED);
    celix_array_list_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.versionVal = val;
    celix_arrayList_removeEntry(list, entry);
}

void celix_arrayList_clear(celix_array_list_t* list) {
    for (int i = 0; i < list->size; ++i) {
        celix_arrayList_callRemovedCallback(list, i);
        memset(&list->elementData[i], 0, sizeof(celix_array_list_entry_t));
    }
    list->size = 0;
}

#if defined(__APPLE__)
static int celix_arrayList_compareEntries(void* arg, const void* voidA, const void* voidB) {
#else
static int celix_arrayList_compareEntries(const void* voidA, const void* voidB, void* arg) {
#endif
    celix_array_list_compare_entries_fp compare = arg;
    const celix_array_list_entry_t* a = voidA;
    const celix_array_list_entry_t* b = voidB;
    return compare(*a, *b);
}

void celix_arrayList_sort(celix_array_list_t* list) {
    if (list->compareCallback) {
        celix_arrayList_sortEntries(list, list->compareCallback);
    }
}

bool celix_arrayList_equals(const celix_array_list_t* listA, const celix_array_list_t* listB) {
    if (listA == listB) {
        return true;
    }
    if (!listA || !listB) {
        return false;
    }
    if (listA->elementType != listB->elementType) {
        return false;
    }
    if (listA->size != listB->size) {
        return false;
    }
    if (listA->equalsCallback != listB->equalsCallback) {
        return false;
    }
    for (int i = 0; i < listA->size; ++i) {
        if (!listA->equalsCallback(listA->elementData[i], listB->elementData[i])) {
            return false;
        }
    }
    return true;
}

celix_array_list_t* celix_arrayList_copy(const celix_array_list_t* list) {
    if (!list) {
        return NULL;
    }

    celix_array_list_create_options_t opts = CELIX_EMPTY_ARRAY_LIST_CREATE_OPTIONS;
    opts.elementType = list->elementType;
    opts.equalsCallback = list->equalsCallback;
    opts.compareCallback = list->compareCallback;
    opts.removedCallback = list->removedCallback;
    opts.removedCallbackData = list->removedCallbackData;
    opts.simpleRemovedCallback = list->simpleRemovedCallback;
    opts.copyCallback = list->copyCallback;
    celix_autoptr(celix_array_list_t) copy = celix_arrayList_createWithOptions(&opts);
    if (!copy) {
        return NULL;
    }

    celix_status_t status = celix_arrayList_ensureCapacity(copy, list->capacity);
    if (status != CELIX_SUCCESS) {
        return NULL;
    }

    for (int i = 0; i < celix_arrayList_size(list); ++i) {
        celix_array_list_entry_t entry;
        if (list->copyCallback) {
            memset(&entry, 0, sizeof(entry));
            status = list->copyCallback(list->elementData[i], &entry);
            if (status != CELIX_SUCCESS) {
                celix_err_push("Failed to copy entry");
                return NULL;
            }
        } else {
            entry = list->elementData[i]; // shallow copy
        }

        (void)celix_arrayList_addEntry(copy, entry); // Cannot fail, because ensureCapacity is already called to
                                                     // ensure enough space
    }

    return celix_steal_ptr(copy);
}

void celix_arrayList_sortEntries(celix_array_list_t* list, celix_array_list_compare_entries_fp compare) {
#if defined(__APPLE__)
    qsort_r(list->elementData, list->size, sizeof(celix_array_list_entry_t), compare, celix_arrayList_compareEntries);
#else
    qsort_r(list->elementData, list->size, sizeof(celix_array_list_entry_t), celix_arrayList_compareEntries, compare);
#endif
}

const char* celix_arrayList_elementTypeToString(celix_array_list_element_type_t type) {
    switch (type) {
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_UNDEFINED:
        return STRING_VALUE_UNDEFINED_EL_TYPE;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_POINTER:
        return STRING_VALUE_POINTER_EL_TYPE;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING:
        return STRING_VALUE_STRING_EL_TYPE;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_LONG:
        return STRING_VALUE_LONG_EL_TYPE;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_DOUBLE:
        return STRING_VALUE_DOUBLE_EL_TYPE;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_BOOL:
        return STRING_VALUE_BOOL_EL_TYPE;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_VERSION:
        return STRING_VALUE_VERSION_EL_TYPE;

    case CELIX_ARRAY_LIST_ELEMENT_TYPE_PROPERTIES:
        return STRING_VALUE_PROPERTIES_EL_TYPE;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_ARRAY_LIST:
        return STRING_VALUE_ARRAY_LIST_EL_TYPE;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_VARIANT:
        return STRING_VALUE_VARIANT_EL_TYPE;
    default:
        return STRING_VALUE_UNDEFINED_EL_TYPE;
    }
}
