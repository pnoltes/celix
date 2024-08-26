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

#ifndef ETCDLIB_H_
#define ETCDLIB_H_

#include "etcdlib_export.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * If set etcdlib will _not_ initialize curl
 * using curl_global_init. Note that
 * curl_global_init can be called multiple
 * times, but is _not_ thread-safe.
 */
#define ETCDLIB_NO_CURL_INITIALIZATION (1)

#define ETCDLIB_ACTION_CREATE "create"
#define ETCDLIB_ACTION_GET "get"
#define ETCDLIB_ACTION_SET "set"
#define ETCDLIB_ACTION_UPDATE "update"
#define ETCDLIB_ACTION_DELETE "delete"
#define ETCDLIB_ACTION_EXPIRE "expire"

/**
 * @brief Return codes for the etcdlib functions
 *
 * Note that other error codes can be returned as well, in that case the etcdlib_strerror function can be used to get
 * the error string. This can includes curl error strings.
 */
#define ETCDLIB_RC_OK 0
#define ETCDLIB_RC_TIMEOUT 3
#define ETCDLIB_RC_EVENT_CLEARED 4
#define ETCDLIB_RC_INVALID_RESPONSE_CONTENT 5
#define ETCDLIB_RC_ENOMEM 6

/**
 * @brief Opaque struct for etcdlib_t
 */
typedef struct etcdlib etcdlib_t; // opaque struct

typedef int etcdlib_status_t;

//TODO doc
#define etcdlib_autoptr_t etcdlib_t* __attribute__((cleanup(etcdlib_cleanup)))

/**
 * @brief Transfer the ownership of the pointer from the
 * referenced variable to the "caller" of the macro.
 */
#ifdef __cplusplus
#define etcdlib_steal_ptr(p) \
    ({ auto __ptr = (p); (p) = NULL; __ptr; })
#else
#define etcdlib_steal_ptr(p) \
    ({ __auto_type __ptr = (p); (p) = NULL; __ptr; })
#endif

typedef void (*etcdlib_key_value_callback)(const char* key, const char* value, void* arg);

/**
 * @brief Creates the ETCD-LIB  with the server/port where Etcd can be reached.
 *
 * Note will print to stderr if creation fails.
 *
 * @param[in] server String containing the IP-number of the server.
 * @param[in] port Port number of the server.
 * @param[in] flags bitwise flags to control etcdlib initialization.
 * @return Pointer to the etcdlib_t struct needed by subsequent api calls
 */
ETCDLIB_EXPORT etcdlib_t* etcdlib_create(const char* server, int port, int flags);

/**
 * @brief ETCD-LIB create options
 */
typedef struct etcdlib_create_options {
    const char* server;  /**< The server where Etcd can be reached. If NULL defaults to "localhost" */
    int port;            /**< The port where Etcd can be reached. If <= 0 defaults to  2379. */
    bool initializeCurl; /**< If true curl is initialized (globally), if false curl is not initialized. */
    bool useMultiCurl;   /**< If true curl is used in multi mode, if false curl is used in single mode.
                            Multi mode performs better, but also uses more resources. */
                            //TODO options for max nr of completed curl entries (multi mode)
    //TODO max nr of parallel curl connections (multi mode)
} etcdlib_create_options_t;

#define ETCDLIB_EMPTY_CREATE_OPTIONS \
    { NULL, 0, false, false }

/**
 * @brief Creates the ETCD-LIB with the provided options.
 * @param[in] options The options to create the etcdlib. Can't be NULL and only used during this call.
 * @param[out] etcdlib The created etcdlib. Can't be NULL.
 * @return A status code indicating success or failure.
 */
ETCDLIB_EXPORT etcdlib_status_t etcdlib_createWithOptions(const etcdlib_create_options_t* options,
                                                           etcdlib_t** etcdlib);

/**
 * @brief Destroys the ETCD-LIB.  with the server/port where Etcd can be reached.
 * @param etcdlib_t* The ETCD-LIB instance.
 */
ETCDLIB_EXPORT void etcdlib_destroy(etcdlib_t* etcdlib);

/**
 * @brief Cleanup function for etcdlib_t* autoptr (etcdlib_autoptr_t).
 */
ETCDLIB_EXPORT void etcdlib_cleanup(etcdlib_t** etcdlib);

/**
 * @brief Get the error string for the given etcdlib error code
 * @param status The status code.
 * @return The error string.
 */
const char* etcdlib_strerror(etcdlib_status_t status);

/**
 * Returns the configured etcd host for etcdlib.
 */
ETCDLIB_EXPORT const char* etcdlib_host(etcdlib_t* etcdlib);

/**
 * Returns the configured etcd port for etcdlib.
 */
ETCDLIB_EXPORT int etcdlib_port(etcdlib_t* etcdlib);

/**
 * @brief Retrieve a single value from Etcd.
 * @param[in] etcdlib The ETCD-LIB instance (contains hostname and port info).
 * @param[in] key The Etcd-key (Note: a leading '/' should be avoided).
 * @param[out] value The allocated memory contains the Etcd-value. The caller is responsible for freeing this memory.
 * @param[out] index If not NULL, the Etcd-index of the last modified value (etcd wide) or -1 if an etcd index header
 * was not found in the HTTP response.
 * @return 0 on success, non-zero otherwise
 */
ETCDLIB_EXPORT etcdlib_status_t etcdlib_get(etcdlib_t* etcdlib, const char* key, char** value, long long* index);

/**
 * @brief Retrieve the contents of a directory. For every found key/value pair the given callback function is called.
 * @param[in] etcdlib The ETCD-LIB instance (contains hostname and port info).
 * @param[in] directory The Etcd-directory which has to be searched for keys
 * @param[in] callback Callback function which is called for every found key
 * @param[in] arg Argument is passed to the callback function
 * @param[out] index If not NULL, the Etcd-index of the last modified value (etcd wide).
 * @return 0 on success, non-zero otherwise
 */
ETCDLIB_EXPORT etcdlib_status_t etcdlib_get_directory(
    etcdlib_t* etcdlib, const char* directory, etcdlib_key_value_callback callback, void* arg, long long* index);

/**
 * @brief Setting an Etcd-key/value
 * @param[in] etcdlib The ETCD-LIB instance (contains hostname and port info).
 * @param[in] key The Etcd-key (Note: a leading '/' should be avoided)
 * @param[in] value The Etcd-value
 * @param[in] ttl If non-zero, this is used as the TTL value
 * @param[in] prevExist If true, the value is only set when the key already exists, if false it is always set
 * @return 0 on success, non-zero otherwise
 */
ETCDLIB_EXPORT etcdlib_status_t etcdlib_set(etcdlib_t* etcdlib, const char* key, const char* value, int ttl, bool prevExist);

/**
 * @brief Refresh the ttl of an existing key.
 * @param[in] etcdlib The ETCD-LIB instance (contains hostname and port info).
 * @param[in] key the etcd key to refresh.
 * @param[in] ttl the ttl value to use.
 * @return 0 on success, non-zero otherwise.
 */
ETCDLIB_EXPORT etcdlib_status_t etcdlib_refresh(etcdlib_t* etcdlib, const char* key, int ttl);

/**
 * @brief Setting an Etcd-key/value and checks if there is a different previous value
 * @param etcdlib The ETCD-LIB instance (contains hostname and port info).
 * @param key The Etcd-key (Note: a leading '/' should be avoided)
 * @param value The Etcd-value
 * @param ttl If non-zero this is used as the TTL value
 * @param alwaysWrite If true the value is written, if false only when the given value is equal to the value in
 * etcd.
 * @return 0 on success, non zero otherwise
 */
//TODO maybe remove or make this a atomic on etcd server side
//ETCDLIB_EXPORT etcdlib_status_t
//etcdlib_set_with_check(etcdlib_t* etcdlib, const char* key, const char* value, int ttl, bool alwaysWrite);

/**
 * @brief Deleting an Etcd-key
 * @param[in] etcdlib The ETCD-LIB instance (contains hostname and port info).
 * @param[in] key The Etcd-key (Note: a leading '/' should be avoided)
 * @return 0 on success, non-zero otherwise
 */
ETCDLIB_EXPORT etcdlib_status_t etcdlib_del(etcdlib_t* etcdlib, const char* key);

/**
 * @brief Watching an etcd directory for changes
 * @param[in] etcdlib The ETCD-LIB instance (contains hostname and port info).
 * @param[in] key The Etcd-key (Note: a leading '/' should be avoided)
 * @param[in] index The Etcd-index which the watch has to be started on.
 * @param[out] action If not NULL, memory is allocated and contains the action-string. The caller is responsible for
 * freeing the memory.
 * @param[out] prevValue If not NULL, memory is allocated and contains the previous value. The caller is responsible
 * for freeing the memory.
 * @param[out] value If not NULL, memory is allocated and contains the new value. The caller is responsible for
 * freeing the memory.
 * @param[out] rkey If not NULL, memory is allocated and contains the updated key. The caller is responsible for
 * freeing the memory.
 * @param[out] modifiedIndex If not NULL, the index of the modification is written.
 * @return ETCDLIB_RC_OK (0) on success, non-zero otherwise. Note that a timeout is signified by a ETCDLIB_RC_TIMEOUT
 * return code.
 */
ETCDLIB_EXPORT etcdlib_status_t etcdlib_watch(etcdlib_t* etcdlib,
                                              const char* key,
                                              long long index,
                                              char** action,
                                              char** prevValue,
                                              char** value,
                                              char** rkey,
                                              long long* modifiedIndex);

#ifdef __cplusplus
}
#endif

#endif /*ETCDLIB_H_ */
