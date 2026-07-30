/*
 * Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
 * See the NOTICE file distributed with this work for additional information regarding copyright ownership.
 * Licensed under the Apache License, Version 2.0.
 */

#include "celix_properties.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "celix_err.h"
#include "celix_stdlib_cleanup.h"

typedef enum {
    CELIX_PATH_NODE_NULL,
    CELIX_PATH_NODE_STRING,
    CELIX_PATH_NODE_LONG,
    CELIX_PATH_NODE_DOUBLE,
    CELIX_PATH_NODE_BOOL,
    CELIX_PATH_NODE_VERSION,
    CELIX_PATH_NODE_PROPERTIES,
    CELIX_PATH_NODE_ARRAY
} celix_path_node_type_e;

typedef struct {
    celix_path_node_type_e type;
    union {
        const char* stringValue;
        long longValue;
        double doubleValue;
        bool boolValue;
        const celix_version_t* versionValue;
        const celix_properties_t* propertiesValue;
        const celix_array_list_t* arrayValue;
    } value;
} celix_path_node_t;

static bool celix_properties_nodeForEntry(const celix_properties_entry_t* entry, celix_path_node_t* node) {
    if (!entry) {
        return false;
    }
    switch (entry->valueType) {
    case CELIX_PROPERTIES_VALUE_TYPE_NULL:
        node->type = CELIX_PATH_NODE_NULL;
        break;
    case CELIX_PROPERTIES_VALUE_TYPE_STRING:
        node->type = CELIX_PATH_NODE_STRING;
        node->value.stringValue = entry->typed.strValue;
        break;
    case CELIX_PROPERTIES_VALUE_TYPE_LONG:
        node->type = CELIX_PATH_NODE_LONG;
        node->value.longValue = entry->typed.longValue;
        break;
    case CELIX_PROPERTIES_VALUE_TYPE_DOUBLE:
        node->type = CELIX_PATH_NODE_DOUBLE;
        node->value.doubleValue = entry->typed.doubleValue;
        break;
    case CELIX_PROPERTIES_VALUE_TYPE_BOOL:
        node->type = CELIX_PATH_NODE_BOOL;
        node->value.boolValue = entry->typed.boolValue;
        break;
    case CELIX_PROPERTIES_VALUE_TYPE_VERSION:
        node->type = CELIX_PATH_NODE_VERSION;
        node->value.versionValue = entry->typed.versionValue;
        break;
    case CELIX_PROPERTIES_VALUE_TYPE_PROPERTIES:
        node->type = CELIX_PATH_NODE_PROPERTIES;
        node->value.propertiesValue = entry->typed.propertiesValue;
        break;
    case CELIX_PROPERTIES_VALUE_TYPE_ARRAY_LIST:
        node->type = CELIX_PATH_NODE_ARRAY;
        node->value.arrayValue = entry->typed.arrayValue;
        break;
    default:
        return false;
    }
    return true;
}

static bool celix_properties_nodeForArrayEntry(const celix_array_list_t* list, int index, celix_path_node_t* node) {
    switch (celix_arrayList_getElementType(list)) {
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING:
        node->type = CELIX_PATH_NODE_STRING;
        node->value.stringValue = celix_arrayList_getString(list, index);
        return true;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_LONG:
        node->type = CELIX_PATH_NODE_LONG;
        node->value.longValue = celix_arrayList_getLong(list, index);
        return true;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_DOUBLE:
        node->type = CELIX_PATH_NODE_DOUBLE;
        node->value.doubleValue = celix_arrayList_getDouble(list, index);
        return true;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_BOOL:
        node->type = CELIX_PATH_NODE_BOOL;
        node->value.boolValue = celix_arrayList_getBool(list, index);
        return true;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_VERSION:
        node->type = CELIX_PATH_NODE_VERSION;
        node->value.versionValue = celix_arrayList_getVersion(list, index);
        return true;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_PROPERTIES:
        node->type = CELIX_PATH_NODE_PROPERTIES;
        node->value.propertiesValue = celix_arrayList_getProperties(list, index);
        return true;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_ARRAY_LIST:
        node->type = CELIX_PATH_NODE_ARRAY;
        node->value.arrayValue = celix_arrayList_getArrayList(list, index);
        return true;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_VARIANT: {
        const celix_array_list_variant_t* value = celix_arrayList_getVariant(list, index);
        switch (value->type) {
        case CELIX_ARRAY_LIST_VARIANT_TYPE_NULL:
            node->type = CELIX_PATH_NODE_NULL;
            break;
        case CELIX_ARRAY_LIST_VARIANT_TYPE_STRING:
            node->type = CELIX_PATH_NODE_STRING;
            node->value.stringValue = value->value.stringValue;
            break;
        case CELIX_ARRAY_LIST_VARIANT_TYPE_LONG:
            node->type = CELIX_PATH_NODE_LONG;
            node->value.longValue = value->value.longValue;
            break;
        case CELIX_ARRAY_LIST_VARIANT_TYPE_DOUBLE:
            node->type = CELIX_PATH_NODE_DOUBLE;
            node->value.doubleValue = value->value.doubleValue;
            break;
        case CELIX_ARRAY_LIST_VARIANT_TYPE_BOOL:
            node->type = CELIX_PATH_NODE_BOOL;
            node->value.boolValue = value->value.boolValue;
            break;
        case CELIX_ARRAY_LIST_VARIANT_TYPE_VERSION:
            node->type = CELIX_PATH_NODE_VERSION;
            node->value.versionValue = value->value.versionValue;
            break;
        case CELIX_ARRAY_LIST_VARIANT_TYPE_PROPERTIES:
            node->type = CELIX_PATH_NODE_PROPERTIES;
            node->value.propertiesValue = value->value.propertiesValue;
            break;
        case CELIX_ARRAY_LIST_VARIANT_TYPE_ARRAY_LIST:
            node->type = CELIX_PATH_NODE_ARRAY;
            node->value.arrayValue = value->value.arrayListValue;
            break;
        }
        return true;
    }
    default:
        return false;
    }
}

static bool celix_properties_pathError(const char* message) {
    celix_err_pushf("Invalid or unsupported JSONPath: %s.", message);
    return false;
}

static bool celix_properties_resolvePath(const celix_properties_t* properties,
                                         const char* path,
                                         bool evaluate,
                                         celix_path_node_t* result) {
    if (!path || path[0] != '$') {
        return celix_properties_pathError("path must start with '$'");
    }
    celix_path_node_t node = {.type = CELIX_PATH_NODE_PROPERTIES, .value.propertiesValue = properties};
    const char* cursor = path + 1;
    while (*cursor) {
        if (*cursor == '.') {
            ++cursor;
            if (*cursor == '.' || *cursor == '*' || *cursor == '\0') {
                return celix_properties_pathError("descendants and wildcards are not supported by this subset");
            }
            const char* begin = cursor;
            while (*cursor && *cursor != '.' && *cursor != '[') {
                ++cursor;
            }
            celix_autofree char* name = strndup(begin, (size_t)(cursor - begin));
            if (!name) {
                celix_err_push("Cannot allocate JSONPath name.");
                return false;
            }
            if (evaluate) {
                if (node.type != CELIX_PATH_NODE_PROPERTIES ||
                    !celix_properties_nodeForEntry(celix_properties_getEntry(node.value.propertiesValue, name),
                                                   &node)) {
                    return false;
                }
            }
        } else if (*cursor == '[') {
            ++cursor;
            if (*cursor == '\'' || *cursor == '"') {
                char quote = *cursor++;
                const char* begin = cursor;
                while (*cursor && *cursor != quote) {
                    if (*cursor == '\\' && cursor[1]) {
                        cursor += 2;
                    } else {
                        ++cursor;
                    }
                }
                if (*cursor != quote) {
                    return celix_properties_pathError("unterminated name selector");
                }
                celix_autofree char* name = strndup(begin, (size_t)(cursor - begin));
                ++cursor;
                if (*cursor++ != ']') {
                    return celix_properties_pathError("missing closing bracket");
                }
                if (evaluate && (node.type != CELIX_PATH_NODE_PROPERTIES ||
                                 !celix_properties_nodeForEntry(
                                     celix_properties_getEntry(node.value.propertiesValue, name), &node))) {
                    return false;
                }
            } else {
                char* end = NULL;
                errno = 0;
                long index = strtol(cursor, &end, 10);
                if (end == cursor || errno == ERANGE || *end != ']') {
                    return celix_properties_pathError("expected an array index");
                }
                cursor = end + 1;
                if (evaluate) {
                    if (node.type != CELIX_PATH_NODE_ARRAY) {
                        return false;
                    }
                    int size = celix_arrayList_size(node.value.arrayValue);
                    if (index < 0) {
                        index += size;
                    }
                    if (index < 0 || index >= size ||
                        !celix_properties_nodeForArrayEntry(node.value.arrayValue, (int)index, &node)) {
                        return false;
                    }
                }
            }
        } else {
            return celix_properties_pathError("expected child or index selector");
        }
    }
    if (result) {
        *result = node;
    }
    return true;
}

bool celix_properties_checkPath(const char* path) { return celix_properties_resolvePath(NULL, path, false, NULL); }

#define CELIX_PATH_GETTER(NAME, TYPE, NODE_TYPE, FIELD)                                                                \
    TYPE celix_properties_get##NAME##ByPath(const celix_properties_t* props, const char* path, TYPE fallback) {        \
        celix_path_node_t node;                                                                                        \
        return celix_properties_resolvePath(props, path, true, &node) && node.type == NODE_TYPE ? node.value.FIELD     \
                                                                                                : fallback;            \
    }
CELIX_PATH_GETTER(String, const char*, CELIX_PATH_NODE_STRING, stringValue)
CELIX_PATH_GETTER(Long, long, CELIX_PATH_NODE_LONG, longValue)
CELIX_PATH_GETTER(Double, double, CELIX_PATH_NODE_DOUBLE, doubleValue)
CELIX_PATH_GETTER(Bool, bool, CELIX_PATH_NODE_BOOL, boolValue)
CELIX_PATH_GETTER(Version, const celix_version_t*, CELIX_PATH_NODE_VERSION, versionValue)
CELIX_PATH_GETTER(Properties, const celix_properties_t*, CELIX_PATH_NODE_PROPERTIES, propertiesValue)
CELIX_PATH_GETTER(ArrayList, const celix_array_list_t*, CELIX_PATH_NODE_ARRAY, arrayValue)

static bool celix_properties_hasTypedPath(const celix_properties_t* props, const char* path, int type) {
    celix_path_node_t node;
    return celix_properties_resolvePath(props, path, true, &node) && (type < 0 || node.type == type);
}
bool celix_properties_hasPath(const celix_properties_t* p, const char* s) {
    return celix_properties_hasTypedPath(p, s, -1);
}
#define CELIX_PATH_HAS(NAME, TYPE)                                                                                     \
    bool celix_properties_has##NAME##Path(const celix_properties_t* p, const char* s) {                                \
        return celix_properties_hasTypedPath(p, s, TYPE);                                                              \
    }
CELIX_PATH_HAS(String, CELIX_PATH_NODE_STRING)
CELIX_PATH_HAS(Long, CELIX_PATH_NODE_LONG)
CELIX_PATH_HAS(Double, CELIX_PATH_NODE_DOUBLE)
CELIX_PATH_HAS(Bool, CELIX_PATH_NODE_BOOL)
CELIX_PATH_HAS(Version, CELIX_PATH_NODE_VERSION)
CELIX_PATH_HAS(Properties, CELIX_PATH_NODE_PROPERTIES)
CELIX_PATH_HAS(ArrayList, CELIX_PATH_NODE_ARRAY)
CELIX_PATH_HAS(Null, CELIX_PATH_NODE_NULL)

static celix_array_list_t*
celix_properties_allByPath(const celix_properties_t* props, const char* path, celix_array_list_element_type_t type) {
    celix_array_list_create_options_t opts = CELIX_EMPTY_ARRAY_LIST_CREATE_OPTIONS;
    opts.elementType = type;
    celix_array_list_t* result = celix_arrayList_createWithOptions(&opts);
    if (!result) {
        return NULL;
    }
    celix_path_node_t node;
    if (!celix_properties_resolvePath(props, path, true, &node)) {
        return result;
    }
    celix_status_t status = CELIX_SUCCESS;
    if (type == CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING && node.type == CELIX_PATH_NODE_STRING)
        status = celix_arrayList_addString(result, node.value.stringValue);
    else if (type == CELIX_ARRAY_LIST_ELEMENT_TYPE_LONG && node.type == CELIX_PATH_NODE_LONG)
        status = celix_arrayList_addLong(result, node.value.longValue);
    else if (type == CELIX_ARRAY_LIST_ELEMENT_TYPE_DOUBLE && node.type == CELIX_PATH_NODE_DOUBLE)
        status = celix_arrayList_addDouble(result, node.value.doubleValue);
    else if (type == CELIX_ARRAY_LIST_ELEMENT_TYPE_BOOL && node.type == CELIX_PATH_NODE_BOOL)
        status = celix_arrayList_addBool(result, node.value.boolValue);
    else if (type == CELIX_ARRAY_LIST_ELEMENT_TYPE_VERSION && node.type == CELIX_PATH_NODE_VERSION)
        status = celix_arrayList_addVersion(result, node.value.versionValue);
    else if (type == CELIX_ARRAY_LIST_ELEMENT_TYPE_PROPERTIES && node.type == CELIX_PATH_NODE_PROPERTIES)
        status = celix_arrayList_addProperties(result, node.value.propertiesValue);
    else if (type == CELIX_ARRAY_LIST_ELEMENT_TYPE_ARRAY_LIST && node.type == CELIX_PATH_NODE_ARRAY)
        status = celix_arrayList_addArrayList(result, node.value.arrayValue);
    if (status != CELIX_SUCCESS) {
        celix_arrayList_destroy(result);
        return NULL;
    }
    return result;
}
#define CELIX_PATH_ALL(NAME, TYPE)                                                                                     \
    celix_array_list_t* celix_properties_getAll##NAME##ByPath(const celix_properties_t* p, const char* s) {            \
        return celix_properties_allByPath(p, s, TYPE);                                                                 \
    }
CELIX_PATH_ALL(Strings, CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING)
CELIX_PATH_ALL(Longs, CELIX_ARRAY_LIST_ELEMENT_TYPE_LONG)
CELIX_PATH_ALL(Doubles, CELIX_ARRAY_LIST_ELEMENT_TYPE_DOUBLE)
CELIX_PATH_ALL(Bools, CELIX_ARRAY_LIST_ELEMENT_TYPE_BOOL)
CELIX_PATH_ALL(Versions, CELIX_ARRAY_LIST_ELEMENT_TYPE_VERSION)
CELIX_PATH_ALL(Properties, CELIX_ARRAY_LIST_ELEMENT_TYPE_PROPERTIES)
CELIX_PATH_ALL(ArrayLists, CELIX_ARRAY_LIST_ELEMENT_TYPE_ARRAY_LIST)

celix_array_list_t* celix_properties_getAllValuesByPath(const celix_properties_t* props, const char* path) {
    celix_array_list_t* result = celix_arrayList_createVariantArray();
    if (!result)
        return NULL;
    celix_path_node_t node;
    if (!celix_properties_resolvePath(props, path, true, &node))
        return result;
    celix_array_list_variant_t value = {0};
    switch (node.type) {
    case CELIX_PATH_NODE_NULL:
        value.type = CELIX_ARRAY_LIST_VARIANT_TYPE_NULL;
        break;
    case CELIX_PATH_NODE_STRING:
        value.type = CELIX_ARRAY_LIST_VARIANT_TYPE_STRING;
        value.value.stringValue = node.value.stringValue;
        break;
    case CELIX_PATH_NODE_LONG:
        value.type = CELIX_ARRAY_LIST_VARIANT_TYPE_LONG;
        value.value.longValue = node.value.longValue;
        break;
    case CELIX_PATH_NODE_DOUBLE:
        value.type = CELIX_ARRAY_LIST_VARIANT_TYPE_DOUBLE;
        value.value.doubleValue = node.value.doubleValue;
        break;
    case CELIX_PATH_NODE_BOOL:
        value.type = CELIX_ARRAY_LIST_VARIANT_TYPE_BOOL;
        value.value.boolValue = node.value.boolValue;
        break;
    case CELIX_PATH_NODE_VERSION:
        value.type = CELIX_ARRAY_LIST_VARIANT_TYPE_VERSION;
        value.value.versionValue = node.value.versionValue;
        break;
    case CELIX_PATH_NODE_PROPERTIES:
        value.type = CELIX_ARRAY_LIST_VARIANT_TYPE_PROPERTIES;
        value.value.propertiesValue = node.value.propertiesValue;
        break;
    case CELIX_PATH_NODE_ARRAY:
        value.type = CELIX_ARRAY_LIST_VARIANT_TYPE_ARRAY_LIST;
        value.value.arrayListValue = node.value.arrayValue;
        break;
    }
    if (celix_arrayList_addVariant(result, &value) != CELIX_SUCCESS) {
        celix_arrayList_destroy(result);
        return NULL;
    }
    return result;
}
