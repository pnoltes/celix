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

#include <stddef.h>

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


#ifdef __cplusplus
}
#endif

#endif /* ETCDLIB_PRIVATE_H_ */