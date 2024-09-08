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

#ifndef ETCDLIB_PRIVATE_H_
#define ETCDLIB_PRIVATE_H_

#include <stdarg.h>
#include <stddef.h>
#include <jansson.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Internal error flags, used to distinguish between curl, curl_multi and etcdlib errors */
#define ETCDLIB_INTERNAL_CURLCODE_FLAG  0x80000000
#define ETCDLIB_INTERNAL_CURLMCODE_FLAG 0x40000000
#define ETCDLIB_INTERNAL_HTTPCODE_FLAG  0x20000000

typedef struct {
    char *memory;
    char *header;
    size_t memorySize;
    size_t headerSize;
} etcdlib_reply_data_t;

/**
 * @brief Extract HTTP return codes from the provided etcdlib_status_t
 */
bool etcdlib_isStatusHttpError(etcdlib_status_t status);

/**
 * @brief Check if the provided status contains an HTTP error return code
 */
int etcdlib_getHttpCodeFromStatus(etcdlib_status_t status);

/**
 * @brief Parse and check the provided etcd reply.
 *
 * Will log an error message using logInvalidResponseErrorCallback if the content of the reply is not as expected.
 * Invalid replies are:
 *   - Reply is not a JSON object
 *   - Reply contains an error field
 *  - Reply does not contain a node/value field (if provided)
 *   - Reply does not contain the expected action (if provided)
 *
 * @param[in] etcdlib The etcdlib instance
 * @param[in] reply The etcd reply to parse
 * @param[in] expectedAction The optional expected action of the reply. If provided, checks "action" in the reply.
 * @param[out] jsonRootOut The root JSON object of the reply.
 * @param[out] valueOut The optional value JSON object of the reply. If provided, extracts "node.value" from the reply.
 */
etcdlib_status_t etcdlib_parseEtcdReply(const etcdlib_t* etcdlib,
                                        const etcdlib_reply_data_t* reply,
                                        const char* expectedAction,
                                        json_t** jsonRootOut,
                                        json_t** nodeOut,
                                        const char** valueOut,
                                        long* indexOut);

/**
 * @brief Util function to create an etcd url and if possible use the local buffer to create the url.
 * If the localBuf is too small, a new buffer will be allocated on *heapBuf.
 *
 * @param[in] etcdlib The etcdlib instance
 * @param[in] localBuf The local buffer to use for the url.
 * @param[in] localBufSize The size of the local buffer.
 * @param[out] heapBuf The buffer that is allocated when the local buffer is too small.
 * @param[in] urlFmt The url format string.
 * @param[in] ... The arguments for the url format string.
 * @return The url that can be used or NULL when out of memory.
 */
const char* etcdlib_createUrl(char* localBuf, size_t localBufSize, char** heapBuf, const char* urlFmt, va_list args);

#ifdef __cplusplus
}
#endif

#endif /* ETCDLIB_PRIVATE_H_ */