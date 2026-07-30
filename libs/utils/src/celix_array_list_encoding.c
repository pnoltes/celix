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
#include "celix_array_list_encoding.h"
#include "celix_array_list_encoding_private.h"
#include "celix_err.h"
#include "celix_json_utils_private.h"
#include "celix_properties_private.h"
#include "celix_stdio_cleanup.h"
#include "celix_stdlib_cleanup.h"

#include <assert.h>
#include <errno.h>
#include <jansson.h>
#include <math.h>
#include <string.h>

static celix_array_list_element_type_t celix_arrayList_determineArrayType(const json_t* jsonArray) {
    if (json_array_size(jsonArray) == 0) {
        return CELIX_ARRAY_LIST_ELEMENT_TYPE_VARIANT;
    }

    json_type type = json_typeof(json_array_get(jsonArray, 0));
    bool numeric = type == JSON_INTEGER || type == JSON_REAL;
    bool hasReal = type == JSON_REAL;
    size_t index;
    json_t* value;
    json_array_foreach(jsonArray, index, value) {
        json_type valueType = json_typeof(value);
        if (numeric && (valueType == JSON_INTEGER || valueType == JSON_REAL)) {
            hasReal = hasReal || valueType == JSON_REAL;
        } else if ((type == JSON_TRUE || type == JSON_FALSE) && json_is_boolean(value)) {
            // Both JSON boolean types belong to a homogeneous boolean array.
        } else if (valueType != type) {
            return CELIX_ARRAY_LIST_ELEMENT_TYPE_VARIANT;
        }
    }

    if (numeric) {
        return hasReal ? CELIX_ARRAY_LIST_ELEMENT_TYPE_DOUBLE : CELIX_ARRAY_LIST_ELEMENT_TYPE_LONG;
    }
    switch (type) {
    case JSON_STRING:
        return CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING;
    case JSON_TRUE:
    case JSON_FALSE:
        return CELIX_ARRAY_LIST_ELEMENT_TYPE_BOOL;
    case JSON_OBJECT:
        return CELIX_ARRAY_LIST_ELEMENT_TYPE_PROPERTIES;
    case JSON_ARRAY:
        return CELIX_ARRAY_LIST_ELEMENT_TYPE_ARRAY_LIST;
    default:
        return CELIX_ARRAY_LIST_ELEMENT_TYPE_VARIANT;
    }
}

static celix_status_t
celix_arrayList_addJsonValueAsVariant(celix_array_list_t* array, const json_t* value, int decodeFlags) {
    celix_array_list_variant_t variant = {0};
    celix_autoptr(celix_properties_t) properties = NULL;
    celix_autoptr(celix_array_list_t) nested = NULL;
    celix_status_t status = CELIX_SUCCESS;
    if (json_is_null(value)) {
        variant.type = CELIX_ARRAY_LIST_VARIANT_TYPE_NULL;
    } else if (json_is_string(value)) {
        variant.type = CELIX_ARRAY_LIST_VARIANT_TYPE_STRING;
        variant.value.stringValue = json_string_value(value);
    } else if (json_is_integer(value)) {
        variant.type = CELIX_ARRAY_LIST_VARIANT_TYPE_LONG;
        variant.value.longValue = (long)json_integer_value(value);
    } else if (json_is_real(value)) {
        variant.type = CELIX_ARRAY_LIST_VARIANT_TYPE_DOUBLE;
        variant.value.doubleValue = json_real_value(value);
    } else if (json_is_boolean(value)) {
        variant.type = CELIX_ARRAY_LIST_VARIANT_TYPE_BOOL;
        variant.value.boolValue = json_boolean_value(value);
    } else if (json_is_object(value)) {
        status = celix_properties_decodeFromJson(value, 0, &properties);
        variant.type = CELIX_ARRAY_LIST_VARIANT_TYPE_PROPERTIES;
        variant.value.propertiesValue = properties;
    } else if (json_is_array(value)) {
        status = celix_arrayList_decodeFromJson(value, decodeFlags, &nested);
        variant.type = CELIX_ARRAY_LIST_VARIANT_TYPE_ARRAY_LIST;
        variant.value.arrayListValue = nested;
    }
    return CELIX_DO_IF(status, celix_arrayList_addVariant(array, &variant));
}

celix_status_t celix_arrayList_decodeFromJson(const json_t* jsonArray, int decodeFlags, celix_array_list_t** out) {
    assert(jsonArray != NULL);
    assert(out != NULL);
    *out = NULL;
    if (!json_is_array(jsonArray)) {
        celix_err_push("Expected a json array.");
        return CELIX_ILLEGAL_ARGUMENT;
    }
    if (json_array_size(jsonArray) == 0 && (decodeFlags & CELIX_ARRAY_LIST_DECODE_ERROR_ON_EMPTY_ARRAYS)) {
        celix_err_push("Expected a non-empty json array.");
        return CELIX_ILLEGAL_ARGUMENT;
    }

    celix_array_list_element_type_t elType = celix_arrayList_determineArrayType(jsonArray);
    celix_array_list_create_options_t opts = CELIX_EMPTY_ARRAY_LIST_CREATE_OPTIONS;
    opts.elementType = elType;
    celix_autoptr(celix_array_list_t) array = celix_arrayList_createWithOptions(&opts);
    if (!array) {
        return ENOMEM;
    }

    size_t index;
    json_t* value;
    json_array_foreach(jsonArray, index, value) {
        celix_status_t status = CELIX_SUCCESS;
        switch (elType) {
        case CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING:
            status = celix_arrayList_addString(array, json_string_value(value));
            break;
        case CELIX_ARRAY_LIST_ELEMENT_TYPE_LONG:
            status = celix_arrayList_addLong(array, (long)json_integer_value(value));
            break;
        case CELIX_ARRAY_LIST_ELEMENT_TYPE_DOUBLE:
            status = celix_arrayList_addDouble(array, json_number_value(value));
            break;
        case CELIX_ARRAY_LIST_ELEMENT_TYPE_BOOL:
            status = celix_arrayList_addBool(array, json_boolean_value(value));
            break;
        case CELIX_ARRAY_LIST_ELEMENT_TYPE_PROPERTIES: {
            celix_properties_t* properties = NULL;
            status = celix_properties_decodeFromJson(value, 0, &properties);
            status = CELIX_DO_IF(status, celix_arrayList_assignProperties(array, properties));
            break;
        }
        case CELIX_ARRAY_LIST_ELEMENT_TYPE_ARRAY_LIST: {
            celix_array_list_t* nested = NULL;
            status = celix_arrayList_decodeFromJson(value, decodeFlags, &nested);
            status = CELIX_DO_IF(status, celix_arrayList_assignArrayList(array, nested));
            break;
        }
        case CELIX_ARRAY_LIST_ELEMENT_TYPE_VARIANT:
            status = celix_arrayList_addJsonValueAsVariant(array, value, decodeFlags);
            break;
        default:
            celix_err_pushf("Unexpected array list element type %d.", elType);
            return CELIX_ILLEGAL_ARGUMENT;
        }
        if (status != CELIX_SUCCESS) {
            return status;
        }
    }
    *out = celix_steal_ptr(array);
    return CELIX_SUCCESS;
}
celix_status_t celix_arrayList_loadFromStream(FILE* stream, int decodeFlags, celix_array_list_t** out) {
    if (!stream || !out) {
        celix_err_push("Invalid arguments.");
        return CELIX_ILLEGAL_ARGUMENT;
    }
    json_error_t jsonError;
    size_t jsonFlags = 0;
    json_auto_t* root = json_loadf(stream, jsonFlags, &jsonError);
    if (!root) {
        celix_err_pushf("Failed to parse json from %s:%i:%i: %s.",
                        jsonError.source,
                        jsonError.line,
                        jsonError.column,
                        jsonError.text);
        return celix_utils_jsonErrorToStatus(json_error_code(&jsonError));
    }
    return celix_arrayList_decodeFromJson(root, decodeFlags, out);
}

celix_status_t celix_arrayList_load(const char* filename, int decodeFlags, celix_array_list_t** out) {
    if (!filename || !out) {
        celix_err_push("Invalid arguments.");
        return CELIX_ILLEGAL_ARGUMENT;
    }
    celix_autoptr(FILE) stream = fopen(filename, "r");
    if (!stream) {
        celix_err_pushf("Failed to open file %s.", filename);
        return CELIX_FILE_IO_EXCEPTION;
    }
    return celix_arrayList_loadFromStream(stream, decodeFlags, out);
}

celix_status_t celix_arrayList_loadFromString(const char* input, int decodeFlags, celix_array_list_t** out) {
    if (!input || !out) {
        celix_err_push("Invalid arguments.");
        return CELIX_ILLEGAL_ARGUMENT;
    }
    celix_autoptr(FILE) stream = fmemopen((void*)input, strlen(input), "r");
    if (!stream) {
        celix_err_pushf("Failed to open memstream.");
        return ENOMEM;
    }
    return celix_arrayList_loadFromStream(stream, decodeFlags, out);
}

static celix_status_t
celix_arrayList_variantValueToJson(const celix_array_list_variant_t* variant, int flags, json_t** out) {
    *out = NULL;
    switch (variant->type) {
    case CELIX_ARRAY_LIST_VARIANT_TYPE_NULL:
        *out = json_null();
        break;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_STRING:
        *out = json_string(variant->value.stringValue);
        break;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_LONG:
        *out = json_integer(variant->value.longValue);
        break;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_DOUBLE:
        if (isnan(variant->value.doubleValue) || isinf(variant->value.doubleValue)) {
            celix_err_push("Invalid NaN or Inf.");
            return CELIX_ILLEGAL_ARGUMENT;
        }
        *out = json_real(variant->value.doubleValue);
        break;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_BOOL:
        *out = json_boolean(variant->value.boolValue);
        break;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_VERSION:
        return celix_utils_versionToJson(variant->value.versionValue, out);
    case CELIX_ARRAY_LIST_VARIANT_TYPE_PROPERTIES:
        return celix_properties_encodeToJson(variant->value.propertiesValue, 0, out);
    case CELIX_ARRAY_LIST_VARIANT_TYPE_ARRAY_LIST:
        return celix_arrayList_encodeToJson(variant->value.arrayListValue, flags, out);
    }
    if (!*out) {
        celix_err_push("Failed to create json value.");
        return ENOMEM;
    }
    return CELIX_SUCCESS;
}

static celix_status_t celix_arrayList_elementEntryValueToJson(celix_array_list_element_type_t elType,
                                                              celix_array_list_entry_t entry,
                                                              int flags,
                                                              json_t** out) {
    *out = NULL;
    switch (elType) {
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING:
        *out = json_string(entry.stringVal);
        break;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_LONG:
        *out = json_integer(entry.longVal);
        break;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_DOUBLE:
        if (isnan(entry.doubleVal) || isinf(entry.doubleVal)) {
            celix_err_push("Invalid NaN or Inf.");
            return CELIX_ILLEGAL_ARGUMENT;
        }
        *out = json_real(entry.doubleVal);
        break;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_BOOL:
        *out = json_boolean(entry.boolVal);
        break;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_VERSION:
        return celix_utils_versionToJson(entry.versionVal, out);
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_PROPERTIES:
        return celix_properties_encodeToJson(entry.propertiesVal, 0, out);
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_ARRAY_LIST:
        return celix_arrayList_encodeToJson(entry.arrayListVal, flags, out);
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_VARIANT:
        return celix_arrayList_variantValueToJson(entry.variantVal, flags, out);
    default:
        celix_err_pushf("Invalid array list element type %d.", elType);
        return CELIX_ILLEGAL_ARGUMENT;
    }
    if (!*out) {
        celix_err_push("Failed to create json value.");
        return ENOMEM;
    }
    return CELIX_SUCCESS;
}

celix_status_t celix_arrayList_encodeToJson(const celix_array_list_t* list, int encodeFlags, json_t** out) {
    assert(list != NULL);
    assert(out != NULL);
    *out = NULL;
    json_auto_t* array = json_array();
    if (!array) {
        celix_err_push("Failed to create json array.");
        return ENOMEM;
    }

    int size = celix_arrayList_size(list);
    celix_array_list_element_type_t elType = celix_arrayList_getElementType(list);
    for (int i = 0; i < size; ++i) {
        celix_array_list_entry_t arrayEntry = celix_arrayList_getEntry(list, i);
        json_t* jsonValue = NULL;
        celix_status_t status = celix_arrayList_elementEntryValueToJson(elType, arrayEntry, encodeFlags, &jsonValue);
        if (status != CELIX_SUCCESS) {
            celix_err_pushf("Failed to encode array element(%d).", i);
            return status;
        }
        if (json_array_append_new(array, jsonValue) != 0) {
            celix_err_push("Failed to append json value to array.");
            return ENOMEM;
        }
    }

    if (json_array_size(array) == 0 && (encodeFlags & CELIX_ARRAY_LIST_ENCODE_ERROR_ON_EMPTY_ARRAYS)) {
        celix_err_push("Invalid empty array.");
        return CELIX_ILLEGAL_ARGUMENT;
    }

    *out = celix_steal_ptr(array);
    return CELIX_SUCCESS;
}
celix_status_t celix_arrayList_saveToStream(const celix_array_list_t* list, int encodeFlags, FILE* stream) {
    if (!list || !stream) {
        celix_err_push("Invalid arguments.");
        return CELIX_ILLEGAL_ARGUMENT;
    }
    json_auto_t* array = NULL;
    celix_status_t status = celix_arrayList_encodeToJson(list, encodeFlags, &array);
    if (status != CELIX_SUCCESS) {
        return status;
    }
    size_t jsonFlags = JSON_COMPACT;
    if (encodeFlags & CELIX_ARRAY_LIST_ENCODE_PRETTY) {
        jsonFlags = JSON_INDENT(2);
    }
    int rc = json_dumpf(array, stream, jsonFlags);
    if (rc != 0) {
        celix_err_push("Failed to write json to stream.");
        return CELIX_FILE_IO_EXCEPTION;
    }
    return CELIX_SUCCESS;
}

celix_status_t celix_arrayList_save(const celix_array_list_t* list, int encodeFlags, const char* filename) {
    if (!list || !filename) {
        celix_err_push("Invalid arguments.");
        return CELIX_ILLEGAL_ARGUMENT;
    }
    FILE* stream = fopen(filename, "w");
    if (!stream) {
        celix_err_pushf("Failed to open file %s.", filename);
        return CELIX_FILE_IO_EXCEPTION;
    }
    celix_status_t status = celix_arrayList_saveToStream(list, encodeFlags, stream);
    int rc = fclose(stream);
    if (rc != 0) {
        celix_err_pushf("Failed to close file %s: %s.", filename, strerror(errno));
        return CELIX_FILE_IO_EXCEPTION;
    }
    return status;
}

celix_status_t celix_arrayList_saveToString(const celix_array_list_t* list, int encodeFlags, char** out) {
    if (!list || !out) {
        celix_err_push("Invalid arguments.");
        return CELIX_ILLEGAL_ARGUMENT;
    }
    celix_autofree char* buffer = NULL;
    size_t size = 0;
    FILE* stream = open_memstream(&buffer, &size);
    if (!stream) {
        celix_err_push("Failed to open memstream.");
        return ENOMEM;
    }

    celix_status_t status = celix_arrayList_saveToStream(list, encodeFlags, stream);
    (void)fclose(stream);
    if (!buffer || status != CELIX_SUCCESS) {
        celix_err_push("Failed to write json to memstream.");
        if (!buffer || status == CELIX_FILE_IO_EXCEPTION) {
            return ENOMEM; // Using memstream as stream, return ENOMEM instead of CELIX_FILE_IO_EXCEPTION
        }
        return status;
    }
    *out = celix_steal_ptr(buffer);
    return CELIX_SUCCESS;
}