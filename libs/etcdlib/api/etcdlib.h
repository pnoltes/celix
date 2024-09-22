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
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file etcdlib.h
 * @brief C API for etcdlib
 * The etcdlib API is thread-safe.
 */

/**
 * @brief Flags to control etcdlib curl initialization.
 *
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
#define ETCDLIB_ACTION_COMPARE_AND_SWAP "compareAndSwap"
#define ETCDLIB_ACTION_COMPARE_AND_DELETE "compareAndDelete"

/**
 * @brief Return codes for the etcdlib functions
 *
 * Note that other error codes can be returned as well, in that case the etcdlib_strerror function can be used to get
 * the error string. This can include curl error strings.
 */
#define ETCDLIB_RC_OK 0
#define ETCDLIB_RC_TIMEOUT 1
#define ETCDLIB_RC_NOT_FOUND 2
#define ETCDLIB_RC_EVENT_INDEX_CLEARED                                                                                 \
    3 /** Indicates that the event index is cleared and that a new get then watch is needed */
#define ETCDLIB_RC_INVALID_RESPONSE_CONTENT                                                                            \
    4 /** In case of an invalid response content, details are logged using the                                         \
         etcdlib_log_invalid_response_reply_callback. */
#define ETCDLIB_RC_ETCD_ERROR                                                                                          \
    5 /** In case of an etcdlib error, details are logged using the                                                    \
        etcdlib_log_invalid_response_error_callback. */
#define ETCDLIB_RC_ENOMEM 6
#define ETCDLIB_RC_STOPPING 7 /** The etcdlib instance is stopping */

/**
 * @brief Opaque struct for etcdlib_t
 */
typedef struct etcdlib etcdlib_t; // opaque struct

typedef int etcdlib_status_t;

/**
 * @brief Autoptr for etcdlib_t*.
 *
 * The autoptr will automatically cleanup the etcdlib_t* when it goes out of scope.
 */
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

typedef void (*etcdlib_key_value_callback)(const char* key, const char* value, void* data);

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
* @brief Log reply callback function
* @param[in] data The data passed to the callback function
* @param[in] reply The reply string. The reply string is null-terminated and only valid during the callback.
*/
typedef void etcdlib_log_reply_callback(void *data, const char *reply);

 /**
 * @brief Log invalid content reply callback function
 * @param[in] data The data passed to the callback function
 * @param[in] reply The reply string. The reply string is null-terminated and only valid during the callback.
 */
typedef void etcdlib_log_invalid_response_reply_callback(void *data, const char *reply);

/**
 * @brief Log error message callback function.
 * @param[in] data The data passed to the callback function
 * @param[in] errorFmt The error message format string.
 * @param[in] ... The arguments for the format string.
 */
typedef void etcdlib_log_error_message_callback(void* data, const char* errorFmt, ...)
    __attribute__((format(printf, 2, 3)));

/**
 * @brief the mode of the etcdlib.
 */
typedef enum etcdlib_mode {
    /**
     * @brief ETCD-LIB default mode.
     *
     * In this mode, connections are handled on the calling threads and the shared resources are protected by a mutex.
     *
     * Implementation detail: In this mode, etcdlib will use CURLM.
     */
    ETCDLIB_MODE_DEFAULT = 0,

    /**
     * @brief ETCD-LIB local thread mode. In this mode, a connection resource is created and reused for every thread
     * that calls the etcdlib functions. This mode can be faster than the default mode.
     *
     * This mode introduces a pthread_key_t and as such introduces a small overhead for every thread, only threads that
     * call etcdlib functions will create a connection resource. Different etcdlib instances will reuse the same
     * thread-based connection resource.
     *
     * Implementation detail: The connection thread resource is a CURL handle.
     */
    ETCDLIB_MODE_LOCAL_THREAD = 1,
} etcdlib_mode_t;

/**
 * @brief ETCD-LIB create options
 */
typedef struct etcdlib_create_options {
    bool useHttps;                   /**< If true, HTTPS os used. if false, HTTP is used. */
    const char* server;              /**< The server where Etcd can be reached. If NULL, defaults to "localhost" */
    unsigned int port;               /**< The port where Etcd can be reached. If 0, defaults to 2379. */
    unsigned int connectTimeoutInMs; /**< The connect timeout in milliseconds. If 0, defaults to 10000 milliseconds.
                           Note this only for the time it takes to connect to the server. */
    unsigned int timeoutInMs; /**< The timeout in milliseconds. If 0, defaults to 30000 milliseconds. This is the time
                   for the whole request, including the time it takes to connect to the server. */
    bool initializeCurl;      /**< If true curl is initialized (globally), if false curl is not initialized. */
    etcdlib_mode_t mode;      /**< The mode of the etcdlib. See etcdlib_mode_t for more information. */
    void* logInvalidResponseReplyData; /**< Data passed to the logInvalidResponseContentCallback */
    etcdlib_log_invalid_response_reply_callback*
        logInvalidResponseReplyCallback; /**< Callback function to log _all_ etcdlib encountered invalid response
                                              content replies. */
    void* logErrorMessageData;           /**< Data passed to the logInvalidResponseErrorCallback */
    etcdlib_log_error_message_callback*
        logErrorMessageCallback; /**< Callback function to log the error message when an invalid response
                                            occurs. An invalid response is when a reply is an expected HTTP 2xx reply,
                                            but the content is invalid (not JSON, not an expected etcd JSON reply).
                                            Etcdlib will log an error message describing issue if a
                                            logInvalidResponseErrorCallback is provided.  */
} etcdlib_create_options_t;

#define ETCDLIB_EMPTY_CREATE_OPTIONS                                                                                   \
    { false, NULL, 0, 0, 0, false, ETCDLIB_MODE_DEFAULT, NULL, NULL, NULL, NULL }

/**
 * @brief Creates the ETCD-LIB with the provided options.
 * @param[in] options The options to create the etcdlib. Can't be NULL and only used during this call.
 * @param[out] etcdlib The created etcdlib. Can't be NULL.
 * @return A status code indicating success or failure.
 */
ETCDLIB_EXPORT etcdlib_status_t etcdlib_createWithOptions(const etcdlib_create_options_t* options,
                                                           etcdlib_t** etcdlib);

/**
 * @brief Destroys the ETCD-LIB.
 *
 * If the mode is ETCDLIB_MODE_DEFAULT, this function will also wakeup existing calls to etcdlib_get, etcdlib_set,
 * etcdlib_delete, etcdlib_watch, etcdlib_getDir, etcdlib_createDir, etcdlib_refresh, etcdlib_refreshDir,
 * etcdlib_deleteDir and etcdlib_watchDir. If the mode is ETCDLIB_MODE_LOCAL_THREAD, this function will not wakeup
 * existing calls but wait for them to finish or timeout.
 *
 * @param[in] etcdlib The ETCD-LIB instance.
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
 * @return ETCDLIB_RC_NOT_FOUND if the key does not exist.
 */
ETCDLIB_EXPORT etcdlib_status_t etcdlib_get(etcdlib_t* etcdlib, const char* key, char** value, long* index);

/**
 * @brief Setting an Etcd-key/value
 * @param[in] etcdlib The ETCD-LIB instance (contains hostname and port info).
 * @param[in] key The Etcd-key (Note: a leading '/' should be avoided)
 * @param[in] value The Etcd-value
 * @param[in] ttl If non-zero, this is used as the TTL value
 * @param[in] prevExist If true, the value is only set when the key already exists, if false it is always set
 * @return 0 on success, non-zero otherwise
 */
ETCDLIB_EXPORT etcdlib_status_t
etcdlib_set(etcdlib_t* etcdlib, const char* key, const char* value, int ttl);

/**
 * @brief Refresh the ttl of an existing key. The key should not be a directory.
 * @param[in] etcdlib The ETCD-LIB instance (contains hostname and port info).
 * @param[in] key the etcd key to refresh.
 * @param[in] ttl the ttl value to use.
 * @return 0 on success, non-zero otherwise.
 * @return ETCDLIB_RC_NOT_FOUND if the key does not exist.
 */
ETCDLIB_EXPORT etcdlib_status_t etcdlib_refresh(etcdlib_t* etcdlib, const char* key, int ttl);

/**
 * @brief Deleting an Etcd-key
 * @param[in] etcdlib The ETCD-LIB instance (contains hostname and port info).
 * @param[in] key The Etcd-key (Note: a leading '/' should be avoided)
 * @return 0 on success, non-zero otherwise.
 * @return ETCDLIB_RC_NOT_FOUND if the key does not exist.
 */
ETCDLIB_EXPORT etcdlib_status_t etcdlib_delete(etcdlib_t* etcdlib, const char* key);

/**
 * @brief Watching an etcd entry changes.
 *
 * This call will block until a (watchable) event occurs in the watched entry.
 *
 * Watches should be done on the returned index of an etcdlib_get call + 1.
 * This way, the watch will only return changes after the last get call. Between etcdlib_watch calls, the returned
 * index + 1 should be used as the watchIndex; this enables skipping events (indexes) that are outside the watched
 * entry.
 * If > 1000 changes occur between the get and watch call, ETCD will return an
 * "index cleared event" and this will result in a ETCDLIB_RC_EVENT_INDEX_CLEARED return code.
 * When this happens, the watch should be restarted with an etcdlib_get call and use the returned index +1 for a new
 * etcdlib_watch call.
 *
 * A watch will return if:
 * - An event occurs in the watched directory, which can be a set, delete, expire, update,
 * compareAndSwap or compareAndDelete event.
 * - A timeout occurs (ETCDLIB_RC_TIMEOUT is returned).
 * - In case of useMultiCurl, when the etcdlib instance is destroyed (ETCDLIB_RC_STOPPED is then returned).
 *
 * A watch will return directly if:
 * - The server returns an unexpected HTTP code (ETCDLIB_RC_ETCD_ERROR is returned).
 * - the server returns an invalid response content (ETCDLIB_RC_INVALID_RESPONSE_CONTENT).
 * - The server is not unresolvable.
 *
 * The watch will not return if the entry is refreshed (ttl, and only ttl is updated).
 *
 * @param[in] etcdlib The ETCD-LIB instance (contains hostname and port info).
 * @param[in] key The Etcd-key (Note: a leading '/' should be avoided)
 * @param[in] watchIndex The modified index the watch watches for. < 0 means watching for the first change.
 * @param[out] event If not NULL, The event action that was performed on the key. Will be NULL if the action is not
 * recognized (not set, delete, expire, update, compareAndSwap or compareAndDelete).
 * The caller should not free the memory.
 * @param[out] modifiedValue If not NULL, memory is allocated and contains the modified value. The caller is responsible
 * for freeing the memory.
 * @param[out] previousValue If not NULL, memory is allocated and contains the previous value. The caller is responsible
 * for freeing the memory.
 * @param[out] modifiedIndex If not NULL, the modified index in the etcd node.modifiedIndex field returned by etcd.
 * @return 0 on success, non-zero otherwise. For useMultiCurl, ETCDLIB_RC_STOPPING is returned when the etcdlib instance
 * is destroyed.
 * @return ETCDLIB_RC_NOT_FOUND if the key does not exist.
 */
ETCDLIB_EXPORT etcdlib_status_t etcdlib_watch(etcdlib_t* etcdlib,
                                              const char* key,
                                              long watchIndex,
                                              const char** event,
                                              char** modifiedValue,
                                              char** previousValue,
                                              long* modifiedIndex);

/**
 * @brief Retrieve the contents of a dir.
 * For every found key/value pair, the given callback function is called.
 *
 * The callback will be called on the same thread as the etcdlib_getDir call.
 *
 * @param[in] etcdlib The ETCD-LIB instance (contains hostname and port info).
 * @param[in] dir The Etcd directory which has to be searched for keys
 * @param[in] callback Callback function which is called for every found key
 * @param[in] callbackData Argument is passed to the callback function
 * @param[out] index If not NULL, the Etcd-index of the last modified value (etcd wide).
 * @return 0 on success, non-zero otherwise
 * @return ETCDLIB_RC_NOT_FOUND if the key does not exist.
 */
ETCDLIB_EXPORT etcdlib_status_t etcdlib_getDir(
    etcdlib_t* etcdlib, const char* dir, etcdlib_key_value_callback callback, void* callbackData, long* index);

 /**
  * @brief Create an Etcd directory.
  * @param[in] etcdlib The ETCD-LIB instance (contains hostname and port info).
  * @param[in] dir The Etcd directory to create (Note: a leading '/' should be avoided).
  * @param[in] ttl If non-zero, this is used as the TTL value
  * @return 0 on success, non-zero otherwise
  */
ETCDLIB_EXPORT etcdlib_status_t etcdlib_createDir(etcdlib_t* etcdlib, const char* dir, int ttl);

 /**
  * @brief Refresh the ttl of an existing directory.
  * @param[in] etcdlib The ETCD-LIB instance (contains hostname and port info).
  * @param[in] dir the etcd directory to refresh.
  * @param[in] ttl the ttl value to use.
  * @return 0 on success, non-zero otherwise.
  * @return ETCDLIB_RC_NOT_FOUND if the key does not exist.
  */
ETCDLIB_EXPORT etcdlib_status_t etcdlib_refreshDir(etcdlib_t* etcdlib, const char* dir, int ttl);

 /**
  * @brief Deleting an Etcd directory.
  * @param[in] etcdlib The ETCD-LIB instance (contains hostname and port info).
  * @param[in] dir The Etcd directory to delete (Note: a leading '/' should be avoided).
  * @return 0 on success, non-zero otherwise.
  * @return ETCDLIB_RC_NOT_FOUND if the key does not exist.
  */
ETCDLIB_EXPORT etcdlib_status_t etcdlib_deleteDir(etcdlib_t* etcdlib, const char* dir);

/**
 * @brief Watching an etcd dir, recursively, for set changes.
 *
 * This call will block until a (watchable) event occurs in the watched directory.
 *
 * Watches should be done on the returned index of an etcdlib_getDir call + 1.
 * This way, the watch will only return changes after the last getDir call. Between etcdlib_watchDir calls, the returned
 * index + 1 should be used as the watchIndex; This enables skipping events (indexes) that are outside the watched
 * directory.
 * If > 1000 changes occur between the getDir and watch call, ETCD will return an
 * "index cleared event" and this will result in a ETCDLIB_RC_EVENT_INDEX_CLEARED return code.
 * When this happens, the watch should be restarted with an etcdlib_getDir call and use the returned index +1 for a new
 * etcdlib_watchDir call.
 *
 * A watch will return if:
 * - An event occurs in the watched directory, which can be a set, delete, expire, update,
 * compareAndSwap or compareAndDelete event.
 * - A timeout occurs (ETCDLIB_RC_TIMEOUT is returned).
 * - In case of useMultiCurl, when the etcdlib instance is destroyed (ETCDLIB_RC_STOPPED is then returned).
 *
 * A watch will return directly if:
 * - The server returns an unexpected HTTP code (ETCDLIB_RC_ETCD_ERROR is returned).
 * - the server returns an invalid response content (ETCDLIB_RC_INVALID_RESPONSE_CONTENT).
 * - The server is not unresolvable.
 *
 * The watch will not return if the directory or entries in the directory are refreshed (ttl, and only ttl is updated).
 *
 * Note when a directory is deleted or expired, only an event on the deleted/expired directory is returned, no events on
 * the keys in the directory. This is a limitation of etcd. Use the `isDir` parameter to check if the event is for a
 * directory.
 *
 * @param[in] etcdlib The ETCD-LIB instance (contains hostname and port info).
 * @param[in] dir The Etcd-key (Note: a leading '/' should be avoided)
 * @param[in] watchIndex The modified index the watch watches for. < 0 means watching for the first change.
 * @param[out] event If not NULL, The event action that was performed on the key. Will be NULL if the action is not
 * recognized (not set, delete, expire, update, compareAndSwap or compareAndDelete).
 * The caller should not free the memory.
 * @param[out] modifiedKey If not NULL, memory is allocated and contains the modified key. The caller is responsible for
 * freeing the memory.
 * @param[out] modifiedValue If not NULL, memory is allocated and contains the modified value. The caller is responsible
 * for freeing the memory.
 * @param[out] previousValue If not NULL, memory is allocated and contains the previous value. The caller is responsible
 * for freeing the memory.
 * @param[out] isDir If not NULL, set to true if the modified key is a directory.
 * @param[out] modifiedIndex If not NULL, the modified index in the etcd node.modifiedIndex field returned by etcd.
 * @return 0 on success, non-zero otherwise. For useMultiCurl, ETCDLIB_RC_STOPPING is returned when the etcdlib instance
 * is destroyed.
 * @return ETCDLIB_RC_NOT_FOUND if the key does not exist.
 */
ETCDLIB_EXPORT etcdlib_status_t etcdlib_watchDir(etcdlib_t* etcdlib,
                                                 const char* dir,
                                                 long watchIndex,
                                                 const char** event,
                                                 char** modifiedKey,
                                                 char** modifiedValue,
                                                 char** previousValue,
                                                 bool* isDir,
                                                 long* modifiedIndex);


#ifdef __cplusplus
}
#endif

#endif /*ETCDLIB_H_ */
