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

#ifndef CELIX_CELIX_ARRAY_LIST_ENCODING_H
#define CELIX_CELIX_ARRAY_LIST_ENCODING_H

#include <stdio.h>

#include "celix_array_list.h"
#include "celix_errno.h"
#include "celix_utils_export.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name celix_array_list_t encoding flags
 * @{
 */

/**
 * @brief Flag to indicate that the encoding should be pretty printed. e.g. encoded with additional whitespaces,
 * newlines and indentation.
 *
 * If this flag is not set, the encoding will be compact. e.g. without additional whitespaces, newlines and indentation.
 */
#define CELIX_ARRAY_LIST_ENCODE_PRETTY 0x01

/**
 * @brief Deprecated compatibility flag. Empty arrays are valid JSON and are always encoded as `[]`.
 * @deprecated This flag is retained as a no-op for source compatibility.
 */
#define CELIX_ARRAY_LIST_ENCODE_ERROR_ON_EMPTY_ARRAYS 0x10

/**
 * @brief Deprecated compatibility flag. Non-finite numbers are never valid JSON and are always rejected.
 * @deprecated This flag is retained as a no-op for source compatibility.
 */
#define CELIX_ARRAY_LIST_ENCODE_ERROR_ON_NAN_INF 0x20

/** @brief Deprecated combination of compatibility flags. Non-finite values are rejected unconditionally. */
#define CELIX_ARRAY_LIST_ENCODE_STRICT                                                                                 \
    (CELIX_ARRAY_LIST_ENCODE_ERROR_ON_EMPTY_ARRAYS | CELIX_ARRAY_LIST_ENCODE_ERROR_ON_NAN_INF)

/** @} */ // End celix_array_list_t encoding flags

/**
 * @brief Encode the given celix_array_list_t as a JSON representation，and write it to the given stream.
 *
 * The caller is the owner of the stream and is responsible for closing it.
 *
 * If the return status is an error, an error message is logged to celix_err.
 *
 * Array list elements are encoded as follows:
 * - CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING: Encoded as a JSON string.
 * - CELIX_ARRAY_LIST_ELEMENT_TYPE_LONG: Encoded as a JSON integer.
 * - CELIX_ARRAY_LIST_ELEMENT_TYPE_DOUBLE: Encoded as a JSON real.
 * - CELIX_ARRAY_LIST_ELEMENT_TYPE_BOOL: Encoded as a JSON boolean.
 * - CELIX_ARRAY_LIST_ELEMENT_TYPE_VERSION: Encoded as a JSON string with a "version<" prefix and a ">" suffix
 * (e.g. "version<1.2.3>").
 * - CELIX_ARRAY_LIST_ELEMENT_TYPE_PROPERTIES: Encoded as a JSON object.
 * - CELIX_ARRAY_LIST_ELEMENT_TYPE_ARRAY_LIST: Encoded recursively as a JSON array.
 * - CELIX_ARRAY_LIST_ELEMENT_TYPE_VARIANT: Each element is encoded according to its variant tag.
 * - CELIX_ARRAY_LIST_ELEMENT_TYPE_UNDEFINED and CELIX_ARRAY_LIST_ELEMENT_TYPE_POINTER: Not supported and will result in
 * an error.
 *
 * @param list The celix_array_list_t to encode.
 * @param encodeFlags The encoding flags to use.
 * @param stream The stream to write the JSON representation to.
 * @return CELIX_SUCCESS if the encoding was successful.
 *        CELIX_ILLEGAL_ARGUMENT if the `list` or `stream` is NULL, or the provided list cannot be encoded to a JSON
 * representation. ENOMEM if there was not enough memory.
 */
CELIX_UTILS_EXPORT
celix_status_t celix_arrayList_saveToStream(const celix_array_list_t* list, int encodeFlags, FILE* stream);

/**
 * @brief Encode the given celix_array_list_t as a JSON representation, and write it to the given file.
 *
 * For more information about the encoding, see celix_arrayList_saveToStream.
 *
 * If the return status is an error, an error message is logged to celix_err.
 *
 * @param list The celix_array_list_t to encode.
 * @param encodeFlags The encoding flags to use.
 * @param filename The file to write the JSON representation to.
 * @return CELIX_SUCCESS if the encoding was successful.
 *       CELIX_ILLEGAL_ARGUMENT if the `list` or `filename` is NULL, or the provided list cannot be encoded to a JSON
 * representation. CELIX_FILE_IO_EXCEPTION if the file could not be opened for writing. ENOMEM if there was not enough
 * memory.
 */
CELIX_UTILS_EXPORT
celix_status_t celix_arrayList_save(const celix_array_list_t* list, int encodeFlags, const char* filename);

/**
 * @brief Encode the given celix_array_list_t as a JSON representation to a string.
 *
 * For more information about the encoding, see celix_arrayList_saveToStream.
 *
 * If the return status is an error, an error message is logged to celix_err.
 *
 * @param list The celix_array_list_t to encode.
 * @param encodeFlags The encoding flags to use.
 * @param out The resulting JSON string representation. The caller is responsible for freeing the returned string using
 * free.
 * @return CELIX_SUCCESS if the encoding was successful.
 *         CELIX_ILLEGAL_ARGUMENT if the `list` or `out` is NULL, or the provided list cannot be encoded to a JSON
 * representation. ENOMEM if there was not enough memory.
 */
CELIX_UTILS_EXPORT
celix_status_t celix_arrayList_saveToString(const celix_array_list_t* list, int encodeFlags, char** out);

/**
 * @name celix_array_list_t decoding flags
 * @{
 */

/**
 * @brief Deprecated compatibility flag. Empty arrays always decode to an empty variant array list.
 * @deprecated This flag is retained as a no-op for source compatibility.
 */
#define CELIX_ARRAY_LIST_DECODE_ERROR_ON_EMPTY_ARRAYS 0x01

/**
 * @brief Deprecated compatibility flag. Every JSON array shape is represented, using variants when needed.
 * @deprecated This flag is retained as a no-op for source compatibility.
 */
#define CELIX_ARRAY_LIST_DECODE_ERROR_ON_UNSUPPORTED_ARRAYS 0x02

/**
 * @brief Opt-in compatibility flag to decode tagged `version<...>` JSON strings as Celix versions.
 *
 * Without this flag every JSON string remains a string. This flag uses the same bit as the corresponding properties
 * flag so it can be propagated through recursive decoding.
 */
#define CELIX_ARRAY_LIST_DECODE_LEGACY_VERSION_STRINGS 0x40

/** @brief Deprecated combination of no-op compatibility flags. */
#define CELIX_ARRAY_LIST_DECODE_STRICT                                                                                 \
    (CELIX_ARRAY_LIST_DECODE_ERROR_ON_EMPTY_ARRAYS | CELIX_ARRAY_LIST_DECODE_ERROR_ON_UNSUPPORTED_ARRAYS)

/** @} */ // End celix_array_list_t decoding flags

/**
 * @brief Decode a celix_array_list_t from a JSON representation in the given stream.
 *

 * If the return status is an error, an error message is logged to celix_err.
 *
 * Array list elements are decoded as follows:
 * - JSON string: Decoded as a CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING.
 * - JSON integer: Decoded as a CELIX_ARRAY_LIST_ELEMENT_TYPE_LONG.
 * - JSON real: Decoded as a CELIX_ARRAY_LIST_ELEMENT_TYPE_DOUBLE.
 * - JSON boolean: Decoded as a CELIX_ARRAY_LIST_ELEMENT_TYPE_BOOL.
 * - JSON object: Decoded as CELIX_ARRAY_LIST_ELEMENT_TYPE_PROPERTIES.
 * - JSON array: Decoded recursively as CELIX_ARRAY_LIST_ELEMENT_TYPE_ARRAY_LIST.
 * - Mixed or null-containing arrays: Decoded as CELIX_ARRAY_LIST_ELEMENT_TYPE_VARIANT.
 *
 * JSON strings are not inferred as Celix versions unless CELIX_ARRAY_LIST_DECODE_LEGACY_VERSION_STRINGS is set. Mixed
 * integer/real arrays remain variants so integer precision and the JSON numeric type are preserved.
 *
 * @param stream The stream to read the JSON representation from.
 * @param decodeFlags The decoding flags to use.
 * @param out The decoded celix_array_list_t. The caller is responsible for destroying the returned celix_array_list_t
 using celix_arrayList_destroy.
 * @return CELIX_SUCCESS if the decoding was successful.
 *        CELIX_ILLEGAL_ARGUMENT if the `stream` or `out` is NULL, or the provided stream cannot be decoded to a
 celix_array_list_t.
 *        ENOMEM if there was not enough memory.
 */
CELIX_UTILS_EXPORT
celix_status_t celix_arrayList_loadFromStream(FILE* stream, int decodeFlags, celix_array_list_t** out);

/**
 * @brief Decode a celix_array_list_t from a JSON representation in the given file.
 *
 * For more information about the decoding, see celix_arrayList_loadFromStream.
 *
 * If the return status is an error, an error message is logged to celix_err.
 *
 * @param filename The file to read the JSON representation from.
 * @param decodeFlags The decoding flags to use.
 * @param out The decoded celix_array_list_t. The caller is responsible for destroying the returned celix_array_list_t
 * using celix_arrayList_destroy.
 * @return CELIX_SUCCESS if the decoding was successful.
 *        CELIX_ILLEGAL_ARGUMENT if the `filename` or `out` is NULL, or the provided file cannot be decoded to a
 * celix_array_list_t. CELIX_FILE_IO_EXCEPTION if the file could not be opened for reading. ENOMEM if there was not
 * enough memory.
 */
CELIX_UTILS_EXPORT
celix_status_t celix_arrayList_load(const char* filename, int decodeFlags, celix_array_list_t** out);

/**
 * @brief Decode a celix_array_list_t from a JSON representation in the given string.
 *
 * For more information about the decoding, see celix_arrayList_loadFromStream.
 *
 * If the return status is an error, an error message is logged to celix_err.
 *
 * @param input The JSON representation to decode.
 * @param decodeFlags The decoding flags to use.
 * @param out The decoded celix_array_list_t. The caller is responsible for destroying the returned celix_array_list_t
 * using celix_arrayList_destroy.
 * @return CELIX_SUCCESS if the decoding was successful.
 *        CELIX_ILLEGAL_ARGUMENT if the `input` or `out` is NULL, or the provided string cannot be decoded to a
 * celix_array_list_t. ENOMEM if there was not enough memory.
 */
CELIX_UTILS_EXPORT
celix_status_t celix_arrayList_loadFromString(const char* input, int decodeFlags, celix_array_list_t** out);

#ifdef __cplusplus
}
#endif

#endif // CELIX_CELIX_ARRAY_LIST_ENCODING_H
