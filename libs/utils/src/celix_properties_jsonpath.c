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
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "celix_properties.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "celix_err.h"
#include "celix_json_utils_private.h"
#include "celix_stdlib_cleanup.h"

#define CELIX_JSONPATH_MAX_SEGMENTS 256
#define CELIX_JSONPATH_MAX_RESULTS 100000
#define CELIX_JSONPATH_MAX_DEPTH 256

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

typedef struct {
    celix_path_node_t* nodes;
    size_t size;
    size_t capacity;
} celix_path_node_list_t;

typedef enum { CELIX_PATH_NAME, CELIX_PATH_INDEX, CELIX_PATH_WILDCARD, CELIX_PATH_SLICE } celix_path_selector_type_e;

typedef struct {
    celix_path_selector_type_e type;
    char* name;
    int64_t index;
    int64_t start;
    int64_t end;
    int64_t step;
    bool hasStart;
    bool hasEnd;
} celix_path_selector_t;

typedef struct {
    bool descendant;
    celix_path_selector_t* selectors;
    size_t size;
    size_t capacity;
} celix_path_segment_t;

typedef struct {
    celix_path_segment_t* segments;
    size_t size;
    size_t capacity;
} celix_path_query_t;

static void celix_properties_destroyQuery(celix_path_query_t* query) {
    for (size_t i = 0; i < query->size; ++i) {
        for (size_t j = 0; j < query->segments[i].size; ++j) {
            free(query->segments[i].selectors[j].name);
        }
        free(query->segments[i].selectors);
    }
    free(query->segments);
}

static celix_status_t celix_properties_pathError(const char* message) {
    celix_err_pushf("Invalid or unsupported JSONPath: %s.", message);
    return CELIX_ILLEGAL_ARGUMENT;
}

static celix_status_t celix_properties_addNode(celix_path_node_list_t* list, celix_path_node_t node) {
    if (list->size >= CELIX_JSONPATH_MAX_RESULTS) {
        celix_err_push("JSONPath result limit exceeded.");
        return CELIX_ILLEGAL_ARGUMENT;
    }
    if (list->size == list->capacity) {
        size_t capacity = list->capacity == 0 ? 8 : list->capacity * 2;
        celix_path_node_t* nodes = realloc(list->nodes, capacity * sizeof(*nodes));
        if (!nodes) {
            celix_err_push("Cannot allocate JSONPath nodelist.");
            return CELIX_ENOMEM;
        }
        list->nodes = nodes;
        list->capacity = capacity;
    }
    list->nodes[list->size++] = node;
    return CELIX_SUCCESS;
}

static celix_status_t
celix_properties_addSegment(celix_path_query_t* query, bool descendant, celix_path_segment_t** out) {
    if (query->size >= CELIX_JSONPATH_MAX_SEGMENTS) {
        return celix_properties_pathError("segment limit exceeded");
    }
    if (query->size == query->capacity) {
        size_t capacity = query->capacity == 0 ? 8 : query->capacity * 2;
        celix_path_segment_t* segments = realloc(query->segments, capacity * sizeof(*segments));
        if (!segments) {
            celix_err_push("Cannot allocate JSONPath segments.");
            return CELIX_ENOMEM;
        }
        query->segments = segments;
        query->capacity = capacity;
    }
    celix_path_segment_t* segment = &query->segments[query->size++];
    memset(segment, 0, sizeof(*segment));
    segment->descendant = descendant;
    *out = segment;
    return CELIX_SUCCESS;
}

static celix_status_t celix_properties_addSelector(celix_path_segment_t* segment, celix_path_selector_t selector) {
    if (segment->size == segment->capacity) {
        size_t capacity = segment->capacity == 0 ? 4 : segment->capacity * 2;
        celix_path_selector_t* selectors = realloc(segment->selectors, capacity * sizeof(*selectors));
        if (!selectors) {
            free(selector.name);
            celix_err_push("Cannot allocate JSONPath selectors.");
            return CELIX_ENOMEM;
        }
        segment->selectors = selectors;
        segment->capacity = capacity;
    }
    segment->selectors[segment->size++] = selector;
    return CELIX_SUCCESS;
}

static void celix_properties_skipWhitespace(const char** cursor) {
    while (**cursor == ' ' || **cursor == '\t' || **cursor == '\n' || **cursor == '\r') {
        ++*cursor;
    }
}

static int celix_properties_hexValue(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static bool celix_properties_parseHex4(const char** cursor, uint32_t* value) {
    *value = 0;
    for (int i = 0; i < 4; ++i) {
        if ((*cursor)[i] == '\0')
            return false;
        int digit = celix_properties_hexValue((*cursor)[i]);
        if (digit < 0)
            return false;
        *value = (*value << 4) | (uint32_t)digit;
    }
    *cursor += 4;
    return true;
}

static bool celix_properties_appendByte(char** buffer, size_t* size, size_t* capacity, unsigned char byte) {
    if (*size + 1 >= *capacity) {
        size_t newCapacity = *capacity == 0 ? 16 : *capacity * 2;
        char* newBuffer = realloc(*buffer, newCapacity);
        if (!newBuffer)
            return false;
        *buffer = newBuffer;
        *capacity = newCapacity;
    }
    (*buffer)[(*size)++] = (char)byte;
    return true;
}

static bool celix_properties_appendCodepoint(char** buffer, size_t* size, size_t* capacity, uint32_t cp) {
    if (cp <= 0x7f)
        return celix_properties_appendByte(buffer, size, capacity, (unsigned char)cp);
    if (cp <= 0x7ff) {
        return celix_properties_appendByte(buffer, size, capacity, 0xc0 | (cp >> 6)) &&
               celix_properties_appendByte(buffer, size, capacity, 0x80 | (cp & 0x3f));
    }
    if (cp <= 0xffff) {
        return celix_properties_appendByte(buffer, size, capacity, 0xe0 | (cp >> 12)) &&
               celix_properties_appendByte(buffer, size, capacity, 0x80 | ((cp >> 6) & 0x3f)) &&
               celix_properties_appendByte(buffer, size, capacity, 0x80 | (cp & 0x3f));
    }
    return celix_properties_appendByte(buffer, size, capacity, 0xf0 | (cp >> 18)) &&
           celix_properties_appendByte(buffer, size, capacity, 0x80 | ((cp >> 12) & 0x3f)) &&
           celix_properties_appendByte(buffer, size, capacity, 0x80 | ((cp >> 6) & 0x3f)) &&
           celix_properties_appendByte(buffer, size, capacity, 0x80 | (cp & 0x3f));
}

static celix_status_t celix_properties_parseQuotedName(const char** cursor, char** out) {
    char quote = *(*cursor)++;
    char* buffer = NULL;
    size_t size = 0;
    size_t capacity = 0;
    while (**cursor && **cursor != quote) {
        unsigned char ch = (unsigned char)*(*cursor)++;
        if (ch < 0x20) {
            free(buffer);
            return celix_properties_pathError("unescaped control character in name selector");
        }
        if (ch == '\\') {
            char escaped = *(*cursor)++;
            if (!escaped) {
                free(buffer);
                return celix_properties_pathError("unterminated escape in name selector");
            }
            switch (escaped) {
            case '"':
                ch = '"';
                break;
            case '\'':
                ch = '\'';
                break;
            case '\\':
                ch = '\\';
                break;
            case '/':
                ch = '/';
                break;
            case 'b':
                ch = '\b';
                break;
            case 'f':
                ch = '\f';
                break;
            case 'n':
                ch = '\n';
                break;
            case 'r':
                ch = '\r';
                break;
            case 't':
                ch = '\t';
                break;
            case 'u': {
                uint32_t cp;
                if (!celix_properties_parseHex4(cursor, &cp)) {
                    free(buffer);
                    return celix_properties_pathError("invalid Unicode escape in name selector");
                }
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    if ((*cursor)[0] != '\\' || (*cursor)[1] != 'u') {
                        free(buffer);
                        return celix_properties_pathError("missing low surrogate in name selector");
                    }
                    *cursor += 2;
                    uint32_t low;
                    if (!celix_properties_parseHex4(cursor, &low) || low < 0xdc00 || low > 0xdfff) {
                        free(buffer);
                        return celix_properties_pathError("invalid low surrogate in name selector");
                    }
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                } else if (cp >= 0xdc00 && cp <= 0xdfff) {
                    free(buffer);
                    return celix_properties_pathError("unexpected low surrogate in name selector");
                }
                if (!celix_properties_appendCodepoint(&buffer, &size, &capacity, cp)) {
                    free(buffer);
                    celix_err_push("Cannot allocate JSONPath member name.");
                    return CELIX_ENOMEM;
                }
                continue;
            }
            default:
                free(buffer);
                return celix_properties_pathError("invalid escape in name selector");
            }
        }
        if (!celix_properties_appendByte(&buffer, &size, &capacity, ch)) {
            free(buffer);
            celix_err_push("Cannot allocate JSONPath member name.");
            return CELIX_ENOMEM;
        }
    }
    if (**cursor != quote) {
        free(buffer);
        return celix_properties_pathError("unterminated name selector");
    }
    ++*cursor;
    if (!celix_properties_appendByte(&buffer, &size, &capacity, '\0')) {
        free(buffer);
        celix_err_push("Cannot allocate JSONPath member name.");
        return CELIX_ENOMEM;
    }
    if (!celix_utils_isValidUtf8(buffer)) {
        free(buffer);
        return celix_properties_pathError("invalid UTF-8 in name selector");
    }
    *out = buffer;
    return CELIX_SUCCESS;
}

static celix_status_t celix_properties_parseIntegerToken(const char* begin, const char* end, int64_t* value) {
    while (begin < end && isspace((unsigned char)*begin))
        ++begin;
    while (end > begin && isspace((unsigned char)end[-1]))
        --end;
    if (begin == end)
        return CELIX_ILLEGAL_ARGUMENT;
    errno = 0;
    char* parsedEnd = NULL;
    celix_autofree char* token = strndup(begin, (size_t)(end - begin));
    if (!token) {
        celix_err_push("Cannot allocate JSONPath integer token.");
        return CELIX_ENOMEM;
    }
    long long parsed = strtoll(token, &parsedEnd, 10);
    if (errno == ERANGE || parsedEnd == token || *parsedEnd != '\0')
        return CELIX_ILLEGAL_ARGUMENT;
    *value = (int64_t)parsed;
    return CELIX_SUCCESS;
}

static celix_status_t celix_properties_parseNumericSelector(const char** cursor, celix_path_selector_t* selector) {
    const char* begin = *cursor;
    const char* end = begin;
    while (*end && *end != ',' && *end != ']')
        ++end;
    const char* colon1 = memchr(begin, ':', (size_t)(end - begin));
    if (!colon1) {
        celix_status_t status = celix_properties_parseIntegerToken(begin, end, &selector->index);
        if (status == CELIX_ILLEGAL_ARGUMENT) {
            return celix_properties_pathError("expected an array index, slice, name, or wildcard");
        }
        if (status != CELIX_SUCCESS)
            return status;
        selector->type = CELIX_PATH_INDEX;
        *cursor = end;
        return CELIX_SUCCESS;
    }
    selector->type = CELIX_PATH_SLICE;
    selector->step = 1;
    const char* colon2 = memchr(colon1 + 1, ':', (size_t)(end - colon1 - 1));
    if (colon2 && memchr(colon2 + 1, ':', (size_t)(end - colon2 - 1))) {
        return celix_properties_pathError("slice contains too many colons");
    }
    const char* startEnd = colon1;
    const char* endBegin = colon1 + 1;
    const char* endEnd = colon2 ? colon2 : end;
    const char* stepBegin = colon2 ? colon2 + 1 : end;
    const char* trimmed = begin;
    while (trimmed < startEnd && isspace((unsigned char)*trimmed))
        ++trimmed;
    if (trimmed < startEnd) {
        celix_status_t status = celix_properties_parseIntegerToken(begin, startEnd, &selector->start);
        if (status == CELIX_ILLEGAL_ARGUMENT)
            return celix_properties_pathError("invalid slice start");
        if (status != CELIX_SUCCESS)
            return status;
        selector->hasStart = true;
    }
    trimmed = endBegin;
    while (trimmed < endEnd && isspace((unsigned char)*trimmed))
        ++trimmed;
    if (trimmed < endEnd) {
        celix_status_t status = celix_properties_parseIntegerToken(endBegin, endEnd, &selector->end);
        if (status == CELIX_ILLEGAL_ARGUMENT)
            return celix_properties_pathError("invalid slice end");
        if (status != CELIX_SUCCESS)
            return status;
        selector->hasEnd = true;
    }
    trimmed = stepBegin;
    while (trimmed < end && isspace((unsigned char)*trimmed))
        ++trimmed;
    if (trimmed < end) {
        celix_status_t status = celix_properties_parseIntegerToken(stepBegin, end, &selector->step);
        if (status == CELIX_ILLEGAL_ARGUMENT)
            return celix_properties_pathError("invalid slice step");
        if (status != CELIX_SUCCESS)
            return status;
    }
    if (selector->step == 0)
        return celix_properties_pathError("slice step cannot be zero");
    *cursor = end;
    return CELIX_SUCCESS;
}

static celix_status_t celix_properties_parseBracket(const char** cursor, celix_path_segment_t* segment) {
    ++*cursor;
    celix_properties_skipWhitespace(cursor);
    if (**cursor == ']')
        return celix_properties_pathError("selector list cannot be empty");
    while (**cursor) {
        celix_path_selector_t selector = {0};
        celix_status_t status;
        if (**cursor == '\'' || **cursor == '"') {
            selector.type = CELIX_PATH_NAME;
            status = celix_properties_parseQuotedName(cursor, &selector.name);
        } else if (**cursor == '*') {
            selector.type = CELIX_PATH_WILDCARD;
            ++*cursor;
            status = CELIX_SUCCESS;
        } else if (**cursor == '?' || **cursor == '@') {
            return celix_properties_pathError("filters and current-node selectors are not supported");
        } else {
            status = celix_properties_parseNumericSelector(cursor, &selector);
        }
        if (status != CELIX_SUCCESS)
            return status;
        status = celix_properties_addSelector(segment, selector);
        if (status != CELIX_SUCCESS)
            return status;
        celix_properties_skipWhitespace(cursor);
        if (**cursor == ']') {
            ++*cursor;
            return CELIX_SUCCESS;
        }
        if (**cursor != ',')
            return celix_properties_pathError("expected ',' or ']' in selector list");
        ++*cursor;
        celix_properties_skipWhitespace(cursor);
        if (**cursor == ']')
            return celix_properties_pathError("selector missing after comma");
    }
    return celix_properties_pathError("unterminated selector list");
}

static bool celix_properties_isNameFirst(unsigned char ch) { return ch == '_' || isalpha(ch) || ch >= 0x80; }

static bool celix_properties_isNameChar(unsigned char ch) { return celix_properties_isNameFirst(ch) || isdigit(ch); }

static celix_status_t celix_properties_parseDotSelector(const char** cursor, celix_path_segment_t* segment) {
    celix_path_selector_t selector = {0};
    if (**cursor == '*') {
        selector.type = CELIX_PATH_WILDCARD;
        ++*cursor;
    } else {
        const char* begin = *cursor;
        if (!celix_properties_isNameFirst((unsigned char)**cursor)) {
            return celix_properties_pathError("invalid shorthand member name");
        }
        ++*cursor;
        while (celix_properties_isNameChar((unsigned char)**cursor))
            ++*cursor;
        selector.type = CELIX_PATH_NAME;
        selector.name = strndup(begin, (size_t)(*cursor - begin));
        if (!selector.name) {
            celix_err_push("Cannot allocate JSONPath shorthand member name.");
            return CELIX_ENOMEM;
        }
        if (!celix_utils_isValidUtf8(selector.name)) {
            free(selector.name);
            return celix_properties_pathError("invalid UTF-8 shorthand member name");
        }
    }
    return celix_properties_addSelector(segment, selector);
}

static celix_status_t celix_properties_parsePath(const char* path, celix_path_query_t* query) {
    if (!path || path[0] != '$')
        return celix_properties_pathError("path must start with '$'");
    const char* cursor = path + 1;
    while (*cursor) {
        bool descendant = false;
        if (*cursor == '.') {
            ++cursor;
            if (*cursor == '.') {
                descendant = true;
                ++cursor;
            }
        } else if (*cursor != '[') {
            return celix_properties_pathError("expected a child or descendant segment");
        }
        celix_path_segment_t* segment = NULL;
        celix_status_t status = celix_properties_addSegment(query, descendant, &segment);
        if (status != CELIX_SUCCESS)
            return status;
        status = *cursor == '[' ? celix_properties_parseBracket(&cursor, segment)
                                : celix_properties_parseDotSelector(&cursor, segment);
        if (status != CELIX_SUCCESS)
            return status;
    }
    return CELIX_SUCCESS;
}

static bool celix_properties_nodeForEntry(const celix_properties_entry_t* entry, celix_path_node_t* node) {
    if (!entry)
        return false;
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
        default:
            return false;
        }
        return true;
    }
    default:
        return false;
    }
}

static celix_status_t celix_properties_selectChildren(celix_path_node_t node, celix_path_node_list_t* out) {
    if (node.type == CELIX_PATH_NODE_PROPERTIES) {
        CELIX_PROPERTIES_ITERATE(node.value.propertiesValue, iter) {
            celix_path_node_t child;
            if (celix_properties_nodeForEntry(&iter.entry, &child)) {
                celix_status_t status = celix_properties_addNode(out, child);
                if (status != CELIX_SUCCESS)
                    return status;
            }
        }
    } else if (node.type == CELIX_PATH_NODE_ARRAY) {
        int size = celix_arrayList_size(node.value.arrayValue);
        for (int i = 0; i < size; ++i) {
            celix_path_node_t child;
            if (celix_properties_nodeForArrayEntry(node.value.arrayValue, i, &child)) {
                celix_status_t status = celix_properties_addNode(out, child);
                if (status != CELIX_SUCCESS)
                    return status;
            }
        }
    }
    return CELIX_SUCCESS;
}

static celix_status_t celix_properties_applySlice(celix_path_node_t node,
                                                  const celix_path_selector_t* selector,
                                                  celix_path_node_list_t* out) {
    if (node.type != CELIX_PATH_NODE_ARRAY)
        return CELIX_SUCCESS;
    int64_t size = celix_arrayList_size(node.value.arrayValue);
    int64_t start;
    int64_t end;
    if (selector->step > 0) {
        start = selector->hasStart ? selector->start : 0;
        end = selector->hasEnd ? selector->end : size;
        if (start < 0)
            start += size;
        if (end < 0)
            end += size;
        if (start < 0)
            start = 0;
        if (start > size)
            start = size;
        if (end < 0)
            end = 0;
        if (end > size)
            end = size;
        for (int64_t i = start; i < end;) {
            celix_path_node_t selected;
            if (celix_properties_nodeForArrayEntry(node.value.arrayValue, (int)i, &selected)) {
                celix_status_t status = celix_properties_addNode(out, selected);
                if (status != CELIX_SUCCESS)
                    return status;
            }
            if (selector->step > INT64_MAX - i)
                break;
            i += selector->step;
        }
    } else {
        start = selector->hasStart ? selector->start : size - 1;
        end = selector->hasEnd ? selector->end : -1;
        if (start < 0)
            start += size;
        if (selector->hasEnd && end < 0)
            end += size;
        if (start < -1)
            start = -1;
        if (start >= size)
            start = size - 1;
        if (end < -1)
            end = -1;
        if (end >= size)
            end = size - 1;
        for (int64_t i = start; i > end && i >= 0;) {
            celix_path_node_t selected;
            if (celix_properties_nodeForArrayEntry(node.value.arrayValue, (int)i, &selected)) {
                celix_status_t status = celix_properties_addNode(out, selected);
                if (status != CELIX_SUCCESS)
                    return status;
            }
            if (i < INT64_MIN - selector->step)
                break;
            i += selector->step;
        }
    }
    return CELIX_SUCCESS;
}

static celix_status_t celix_properties_applySegment(celix_path_node_t node,
                                                    const celix_path_segment_t* segment,
                                                    celix_path_node_list_t* out) {
    for (size_t i = 0; i < segment->size; ++i) {
        const celix_path_selector_t* selector = &segment->selectors[i];
        if (selector->type == CELIX_PATH_NAME) {
            if (node.type == CELIX_PATH_NODE_PROPERTIES) {
                celix_path_node_t selected;
                if (celix_properties_nodeForEntry(celix_properties_getEntry(node.value.propertiesValue, selector->name),
                                                  &selected)) {
                    celix_status_t status = celix_properties_addNode(out, selected);
                    if (status != CELIX_SUCCESS)
                        return status;
                }
            }
        } else if (selector->type == CELIX_PATH_INDEX) {
            if (node.type == CELIX_PATH_NODE_ARRAY) {
                int64_t size = celix_arrayList_size(node.value.arrayValue);
                int64_t index = selector->index;
                if (index < 0 && index >= -size)
                    index += size;
                if (index >= 0 && index < size) {
                    celix_path_node_t selected;
                    if (celix_properties_nodeForArrayEntry(node.value.arrayValue, (int)index, &selected)) {
                        celix_status_t status = celix_properties_addNode(out, selected);
                        if (status != CELIX_SUCCESS)
                            return status;
                    }
                }
            }
        } else if (selector->type == CELIX_PATH_WILDCARD) {
            celix_status_t status = celix_properties_selectChildren(node, out);
            if (status != CELIX_SUCCESS)
                return status;
        } else {
            celix_status_t status = celix_properties_applySlice(node, selector, out);
            if (status != CELIX_SUCCESS)
                return status;
        }
    }
    return CELIX_SUCCESS;
}

static celix_status_t celix_properties_applyDescendant(celix_path_node_t node,
                                                       const celix_path_segment_t* segment,
                                                       size_t depth,
                                                       celix_path_node_list_t* out) {
    if (depth > CELIX_JSONPATH_MAX_DEPTH)
        return celix_properties_pathError("descendant nesting limit exceeded");
    celix_status_t status = celix_properties_applySegment(node, segment, out);
    if (status != CELIX_SUCCESS)
        return status;
    celix_path_node_list_t children = {0};
    status = celix_properties_selectChildren(node, &children);
    for (size_t i = 0; status == CELIX_SUCCESS && i < children.size; ++i) {
        status = celix_properties_applyDescendant(children.nodes[i], segment, depth + 1, out);
    }
    free(children.nodes);
    return status;
}

static celix_status_t celix_properties_evaluate(const celix_properties_t* properties,
                                                const celix_path_query_t* query,
                                                celix_path_node_list_t* result) {
    celix_path_node_list_t current = {0};
    celix_path_node_t root = {.type = CELIX_PATH_NODE_PROPERTIES, .value.propertiesValue = properties};
    celix_status_t status = celix_properties_addNode(&current, root);
    for (size_t segmentIndex = 0; status == CELIX_SUCCESS && segmentIndex < query->size; ++segmentIndex) {
        celix_path_node_list_t next = {0};
        const celix_path_segment_t* segment = &query->segments[segmentIndex];
        for (size_t i = 0; status == CELIX_SUCCESS && i < current.size; ++i) {
            status = segment->descendant ? celix_properties_applyDescendant(current.nodes[i], segment, 0, &next)
                                         : celix_properties_applySegment(current.nodes[i], segment, &next);
        }
        free(current.nodes);
        current = next;
    }
    if (status != CELIX_SUCCESS) {
        free(current.nodes);
        return status;
    }
    *result = current;
    return CELIX_SUCCESS;
}

static celix_status_t
celix_properties_resolvePath(const celix_properties_t* properties, const char* path, celix_path_node_list_t* result) {
    celix_path_query_t query = {0};
    celix_status_t status = celix_properties_parsePath(path, &query);
    if (status == CELIX_SUCCESS)
        status = celix_properties_evaluate(properties, &query, result);
    celix_properties_destroyQuery(&query);
    return status;
}

bool celix_properties_checkPath(const char* path) {
    celix_path_query_t query = {0};
    celix_status_t status = celix_properties_parsePath(path, &query);
    celix_properties_destroyQuery(&query);
    return status == CELIX_SUCCESS;
}

#define CELIX_PATH_GETTER(NAME, TYPE, NODE_TYPE, FIELD)                                                                \
    TYPE celix_properties_get##NAME##ByPath(const celix_properties_t* props, const char* path, TYPE fallback) {        \
        celix_path_node_list_t nodes = {0};                                                                            \
        if (celix_properties_resolvePath(props, path, &nodes) == CELIX_SUCCESS) {                                      \
            for (size_t i = 0; i < nodes.size; ++i) {                                                                  \
                if (nodes.nodes[i].type == NODE_TYPE) {                                                                \
                    TYPE value = nodes.nodes[i].value.FIELD;                                                           \
                    free(nodes.nodes);                                                                                 \
                    return value;                                                                                      \
                }                                                                                                      \
            }                                                                                                          \
        }                                                                                                              \
        free(nodes.nodes);                                                                                             \
        return fallback;                                                                                               \
    }
CELIX_PATH_GETTER(String, const char*, CELIX_PATH_NODE_STRING, stringValue)
CELIX_PATH_GETTER(Long, long, CELIX_PATH_NODE_LONG, longValue)
CELIX_PATH_GETTER(Double, double, CELIX_PATH_NODE_DOUBLE, doubleValue)
CELIX_PATH_GETTER(Bool, bool, CELIX_PATH_NODE_BOOL, boolValue)
CELIX_PATH_GETTER(Version, const celix_version_t*, CELIX_PATH_NODE_VERSION, versionValue)
CELIX_PATH_GETTER(Properties, const celix_properties_t*, CELIX_PATH_NODE_PROPERTIES, propertiesValue)
CELIX_PATH_GETTER(ArrayList, const celix_array_list_t*, CELIX_PATH_NODE_ARRAY, arrayValue)

static bool celix_properties_hasTypedPath(const celix_properties_t* props, const char* path, int type) {
    celix_path_node_list_t nodes = {0};
    bool found = false;
    if (celix_properties_resolvePath(props, path, &nodes) == CELIX_SUCCESS) {
        for (size_t i = 0; i < nodes.size && !found; ++i)
            found = type < 0 || nodes.nodes[i].type == type;
    }
    free(nodes.nodes);
    return found;
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

static celix_status_t celix_properties_addTypedResult(celix_array_list_t* result,
                                                      celix_array_list_element_type_t type,
                                                      celix_path_node_t node) {
    if (type == CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING && node.type == CELIX_PATH_NODE_STRING)
        return celix_arrayList_addString(result, node.value.stringValue);
    if (type == CELIX_ARRAY_LIST_ELEMENT_TYPE_LONG && node.type == CELIX_PATH_NODE_LONG)
        return celix_arrayList_addLong(result, node.value.longValue);
    if (type == CELIX_ARRAY_LIST_ELEMENT_TYPE_DOUBLE && node.type == CELIX_PATH_NODE_DOUBLE)
        return celix_arrayList_addDouble(result, node.value.doubleValue);
    if (type == CELIX_ARRAY_LIST_ELEMENT_TYPE_BOOL && node.type == CELIX_PATH_NODE_BOOL)
        return celix_arrayList_addBool(result, node.value.boolValue);
    if (type == CELIX_ARRAY_LIST_ELEMENT_TYPE_VERSION && node.type == CELIX_PATH_NODE_VERSION)
        return celix_arrayList_addVersion(result, node.value.versionValue);
    if (type == CELIX_ARRAY_LIST_ELEMENT_TYPE_PROPERTIES && node.type == CELIX_PATH_NODE_PROPERTIES)
        return celix_arrayList_addProperties(result, node.value.propertiesValue);
    if (type == CELIX_ARRAY_LIST_ELEMENT_TYPE_ARRAY_LIST && node.type == CELIX_PATH_NODE_ARRAY)
        return celix_arrayList_addArrayList(result, node.value.arrayValue);
    return CELIX_SUCCESS;
}

static celix_array_list_t*
celix_properties_allByPath(const celix_properties_t* props, const char* path, celix_array_list_element_type_t type) {
    celix_array_list_create_options_t opts = CELIX_EMPTY_ARRAY_LIST_CREATE_OPTIONS;
    opts.elementType = type;
    celix_array_list_t* result = celix_arrayList_createWithOptions(&opts);
    if (!result)
        return NULL;
    celix_path_node_list_t nodes = {0};
    celix_status_t status = celix_properties_resolvePath(props, path, &nodes);
    if (status == CELIX_ILLEGAL_ARGUMENT) {
        nodes.size = 0;
        status = CELIX_SUCCESS;
    }
    for (size_t i = 0; status == CELIX_SUCCESS && i < nodes.size; ++i) {
        status = celix_properties_addTypedResult(result, type, nodes.nodes[i]);
    }
    free(nodes.nodes);
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

static celix_status_t celix_properties_addVariantResult(celix_array_list_t* result, celix_path_node_t node) {
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
    return celix_arrayList_addVariant(result, &value);
}

celix_array_list_t* celix_properties_getAllValuesByPath(const celix_properties_t* props, const char* path) {
    celix_array_list_t* result = celix_arrayList_createVariantArray();
    if (!result)
        return NULL;
    celix_path_node_list_t nodes = {0};
    celix_status_t status = celix_properties_resolvePath(props, path, &nodes);
    if (status == CELIX_ILLEGAL_ARGUMENT) {
        nodes.size = 0;
        status = CELIX_SUCCESS;
    }
    for (size_t i = 0; status == CELIX_SUCCESS && i < nodes.size; ++i) {
        status = celix_properties_addVariantResult(result, nodes.nodes[i]);
    }
    free(nodes.nodes);
    if (status != CELIX_SUCCESS) {
        celix_arrayList_destroy(result);
        return NULL;
    }
    return result;
}
