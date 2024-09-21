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

#include "etcdlib.h"
#include "etcdlib_private.h"

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include <curl/curl.h>
#include <jansson.h>

#define ETCD_JSON_NODE                  "node"
#define ETCD_JSON_PREVNODE              "prevNode"
#define ETCD_JSON_NODES                 "nodes"
#define ETCD_JSON_ACTION                "action"
#define ETCD_JSON_KEY                   "key"
#define ETCD_JSON_VALUE                 "value"
#define ETCD_JSON_DIR                   "dir"
#define ETCD_JSON_MODIFIEDINDEX         "modifiedIndex"
#define ETCD_JSON_INDEX                 "index"
#define ETCD_JSON_ERRORCODE             "errorCode"

#define ETCD_GET_ACTION                 "get"
#define ETCD_SET_ACTION                 "set"
#define ETCD_DEL_ACTION                 "delete"
#define ETCD_REFRESH_ACTION             "update"


#define ETCD_HEADER_INDEX               "X-Etcd-Index: "

#define DEFAULT_CURL_TIMEOUT            30000
#define DEFAULT_CURL_CONNECT_TIMEOUT    10000

#define ETCDLIB_MAX_COMPLETED_CURL_ENTRIES 16

/** Internal error codes, extends ETCDLIC_RC_* errors, but are not exposed to the user */
#define ETCDLIB_INTERNAL_RC_MAX_ENTRIES_REACHED 1002

typedef struct /*anon*/ {
    CURL* curl;
    CURLcode res;
} etcdlib_completed_curl_entry_t;

struct etcdlib {
    const char* scheme;
    char *server;
    unsigned int port;
    unsigned int connectTimeoutInMs;
    unsigned int timeoutInMs;

    void* logInvalidResponseReplyData;
    etcdlib_log_invalid_response_reply_callback* logInvalidResponseReplyCallback;
    void* logErrorMessageData;
    etcdlib_log_error_message_callback* logErrorMessageCallback;

    size_t activeRequest; //atomic, counts the number of active curl requests

    //Used for curl multi
    CURLM* curlMulti;
    bool curlMultiRunning; //atomic flag, only used if curlMulti is not NULL
    pthread_mutex_t curlMutex; //protects completedCurlEntries, only used if curlMulti is not NULL
    etcdlib_completed_curl_entry_t* completedCurlEntries; //only used if curlMulti is not NULL
    size_t completedCurlEntriesSize; //only used if curlMulti is not NULL
};

typedef enum {
    GET, PUT, DELETE
} request_t;

static etcdlib_status_t etcdlib_performRequest(etcdlib_t* etcdlib,
                                               request_t request,
                                               const char* reqData,
                                               const char* expectedAction,
                                               json_t** jsonRootOut,
                                               json_t** nodeOut,
                                               const char** valueOut,
                                               long* indexOut,
                                               const char* urlFmt,
                                               ...) __attribute__((format(printf, 9, 10)));

static etcdlib_status_t etcdlib_waitForMultiCurl(etcdlib_t* lib, CURL* curl);

static void etcdlib_autofree_callback(void* ptr) {
    void** pp = (void**)ptr;
    free(*pp);
}

#define etcdlib_autofree __attribute__((cleanup(etcdlib_autofree_callback)))


static pthread_key_t etcdlibThreadSpecificCurlsKey;
static pthread_once_t etcdlibThreadSpecificCurlsOnce = PTHREAD_ONCE_INIT;

static void etcdlib_cleanupThreadSpecificCurl(void* data) {
    CURL* curl = data;
    if (curl) {
        curl_easy_cleanup(curl);
    }
}

static void etcdlib_initThreadSpecificCurlsOnce() {
    pthread_key_create(&etcdlibThreadSpecificCurlsKey, etcdlib_cleanupThreadSpecificCurl);
}

etcdlib_t *etcdlib_create(const char *server, int port, int flags) {
    etcdlib_create_options_t opts = ETCDLIB_EMPTY_CREATE_OPTIONS;
    opts.server = server;
    opts.port = port;
    opts.initializeCurl = !(flags & ETCDLIB_NO_CURL_INITIALIZATION);
    etcdlib_t* etcdlib = NULL;
    etcdlib_status_t result = etcdlib_createWithOptions(&opts, &etcdlib);
    if (result != ETCDLIB_RC_OK) {
        fprintf(stderr, "Error creating etcdlib: %s\n", etcdlib_strerror(result));
    }
    return etcdlib;
}

etcdlib_status_t etcdlib_createWithOptions(const etcdlib_create_options_t* options,
                                                               etcdlib_t** etcdlib) {
    assert(options != NULL);
    assert(etcdlib != NULL);
    if (options->initializeCurl) {
        CURLcode code = curl_global_init(CURL_GLOBAL_ALL);
        if (code != CURLE_OK) {
            return ETCDLIB_INTERNAL_CURLCODE_FLAG | code;
        }
    }

    const char* schemeConfigured = options->useHttps ? "https" : "http";
    const char* serverConfigured = options->server ? options->server : "localhost";
    const unsigned int portConfigured = options->port > 0 ? options->port : 2379;

    etcdlib_autofree etcdlib_t *lib = calloc(1, sizeof(*lib));
    etcdlib_autofree char* server = strndup(serverConfigured, 1024 * 1024 * 10);
    etcdlib_autofree etcdlib_completed_curl_entry_t* completedEntries = NULL;
    if (options->mode == ETCDLIB_MODE_DEFAULT) {
        completedEntries = calloc(ETCDLIB_MAX_COMPLETED_CURL_ENTRIES, sizeof(*lib->completedCurlEntries));
    }

    if (!lib || !server || (options->mode == ETCDLIB_MODE_DEFAULT && !completedEntries)) {
        return ETCDLIB_RC_ENOMEM;
    }

    lib->logInvalidResponseReplyData = options->logInvalidResponseReplyData;
    lib->logInvalidResponseReplyCallback = options->logInvalidResponseReplyCallback;
    lib->logErrorMessageData = options->logErrorMessageData;
    lib->logErrorMessageCallback = options->logErrorMessageCallback;

    lib->scheme = schemeConfigured;
    lib->server = etcdlib_steal_ptr(server);
    lib->port = portConfigured;
    lib->connectTimeoutInMs = options->connectTimeoutInMs > 0 ? options->connectTimeoutInMs : DEFAULT_CURL_CONNECT_TIMEOUT;
    lib->timeoutInMs = options->timeoutInMs > 0 ? options->timeoutInMs : DEFAULT_CURL_TIMEOUT;
    lib->completedCurlEntriesSize = ETCDLIB_MAX_COMPLETED_CURL_ENTRIES;

    if (options->mode == ETCDLIB_MODE_DEFAULT) {
        lib->completedCurlEntries = etcdlib_steal_ptr(completedEntries);
        lib->completedCurlEntriesSize = ETCDLIB_MAX_COMPLETED_CURL_ENTRIES;

        lib->curlMultiRunning = true;
        lib->curlMulti = curl_multi_init();
        if (!lib->curlMulti) {
            free(lib->server);
            free(lib->completedCurlEntries);
            return ETCDLIB_RC_ENOMEM;
        }

        const CURLMcode mCode = curl_multi_setopt(lib->curlMulti, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
        if (mCode != CURLM_OK) {
            curl_multi_cleanup(lib->curlMulti);
            free(lib->server);
            free(lib->completedCurlEntries);
            return ETCDLIB_INTERNAL_CURLMCODE_FLAG | mCode;
        }



        int rc = pthread_mutex_init(&lib->curlMutex, NULL);
        if (rc != 0) {
            curl_multi_cleanup(lib->curlMulti);
            free(lib->server);
            free(lib->completedCurlEntries);
            return ETCDLIB_RC_ENOMEM;
        }
    } else {
        //use CURL* per thread.
        pthread_once(&etcdlibThreadSpecificCurlsOnce, etcdlib_initThreadSpecificCurlsOnce);
    }

    *etcdlib = etcdlib_steal_ptr(lib);
    return ETCDLIB_RC_OK;
}

static void etcdlib_stopMultiCurl(etcdlib_t* lib) {
    __atomic_store_n(&lib->curlMultiRunning, false, __ATOMIC_RELEASE);
    int runningHandles = 0;
    curl_multi_perform(lib->curlMulti, &runningHandles);
    for (int i = 0; i < runningHandles; i++) {
        curl_multi_wakeup(lib->curlMulti); //note wakeup only wakes up a single handle
    }
}

void etcdlib_destroy(etcdlib_t *etcdlib) {
    if (etcdlib) {
        if (etcdlib->curlMulti) {
            etcdlib_stopMultiCurl(etcdlib);
            curl_multi_cleanup(etcdlib->curlMulti);
        }

        size_t count = __atomic_load_n(&etcdlib->activeRequest, __ATOMIC_ACQUIRE);
        while (count > 0) {
            sched_yield();
            count = __atomic_load_n(&etcdlib->activeRequest, __ATOMIC_ACQUIRE);
        }

        if (etcdlib->curlMulti) {
            pthread_mutex_destroy(&etcdlib->curlMutex);
            free(etcdlib->completedCurlEntries);
        }

        free(etcdlib->server);
        free(etcdlib);
    }
}

void etcdlib_cleanup(etcdlib_t** etcdlib) {
    if (etcdlib) {
        etcdlib_destroy(*etcdlib);
    }
}

const char* etcdlib_strerror(int status) {
    if (status & ETCDLIB_INTERNAL_CURLCODE_FLAG) {
        return curl_easy_strerror(status & ~ETCDLIB_INTERNAL_CURLCODE_FLAG);
    }
    if (status & ETCDLIB_INTERNAL_CURLMCODE_FLAG) {
        return curl_multi_strerror(status & ~ETCDLIB_INTERNAL_CURLMCODE_FLAG);
    }
    if (status & ETCDLIB_INTERNAL_HTTPCODE_FLAG) {
        return "HTTP error"; //TODO improve error code for HTTP errors
    }

    switch (status) {
    case ETCDLIB_RC_OK:
        return "ETCDLIB OK";
    case ETCDLIB_RC_TIMEOUT:
        return "ETCDLIB Timeout";
    case ETCDLIB_RC_EVENT_INDEX_CLEARED:
        return "ETCDLIB Event Index Cleared";
    case ETCDLIB_RC_ENOMEM:
        return "ETCDLIB Out of memory or maximum number of curl handles reached";
    case ETCDLIB_RC_INVALID_RESPONSE_CONTENT:
        return "ETCDLIB Content of response is invalid (not JSON, missing required fields or missing header)";
    case ETCDLIB_RC_ETCD_ERROR:
        return "ETCDLIB Etcd error";
    case ETCDLIB_RC_STOPPING:
        return "ETCDLIB Stopping";
    default:
        return "ETCDLIB Unknown error";
    }
}

const char* etcdlib_host(etcdlib_t* etcdlib) {
    return etcdlib->server;
}

int etcdlib_port(etcdlib_t* etcdlib) {
    return etcdlib->port;
}

static long long etcdlib_getCurrentIndex(const char *headerData) {
    long long index = -1;
    if (headerData == NULL) {
        return index;
    }
    char* indexStr = strstr(headerData, ETCD_HEADER_INDEX);
    if (!indexStr) {
        return -1;
    }
    indexStr += strlen(ETCD_HEADER_INDEX);

    char *endptr;
    index = strtoll(indexStr, &endptr, 10);
    if (endptr == indexStr) {
        index = -1;
    }
    return index;
}

static etcdlib_reply_data_t etcdlib_constructReplyData(bool includeMemory, bool includeHeader) {
    etcdlib_reply_data_t reply;
    reply.memory = includeMemory ? malloc(1) : NULL;
    reply.memorySize = 0;
    reply.header = includeHeader ? malloc(1) : NULL;
    reply.headerSize = 0;
    if (reply.memory) {
        reply.memory[0] = '\0';
    }
    if (reply.header) {
        reply.header[0] = '\0';
    }
    return reply;
}

const char* etcdlib_createUrl(char* localBuf, size_t localBufSize, char** heapBuf, const char* urlFmt, va_list args) {
    *heapBuf = NULL;
    const char* result = NULL;
    va_list args2;
    va_copy(args2, args);

    int written = vsnprintf(localBuf, localBufSize, urlFmt, args);
    if (written <= localBufSize) {
        result = localBuf;
    } else {
        //buffer too small, allocate new buffer
        written = vasprintf(heapBuf, urlFmt, args2);
        if (written >= 0) {
            result = *heapBuf;
        }
    }

    va_end(args);
    return result;
}

static const char* etcdlib_skipLeadingSlashes(const char* key) {
    while (*key == '/') {
        key++;
    }
    return key;
}

static void etcdlib_freeReplyData(etcdlib_reply_data_t* reply) {
    free(reply->memory);
    free(reply->header);
}

static void
etcdlib_logReply(const etcdlib_t* etcdlib, etcdlib_status_t rc, const char* reply, const json_t* parsedReply) {
    if (rc == ETCDLIB_RC_INVALID_RESPONSE_CONTENT && etcdlib->logInvalidResponseReplyCallback) {
        if (reply) {
            etcdlib->logInvalidResponseReplyCallback(etcdlib->logInvalidResponseReplyData, reply);
        } else if (parsedReply) {
            etcdlib_autofree char* replyStr = json_dumps(parsedReply, JSON_COMPACT);
            if (replyStr) {
                etcdlib->logInvalidResponseReplyCallback(etcdlib->logInvalidResponseReplyData, replyStr);
            }
        }
    }
}

#define ETCDLIB_LOG_ERROR(etcdlib, errorFmt, ...)                                                                      \
    if (etcdlib->logErrorMessageCallback) {                                                                            \
        etcdlib->logErrorMessageCallback(etcdlib->logErrorMessageData, errorFmt, ##__VA_ARGS__);                       \
    }

etcdlib_status_t etcdlib_parseEtcdReply(const etcdlib_t* etcdlib,
                                               const etcdlib_reply_data_t* reply,
                                               const char* expectedAction,
                                               json_t** jsonRootOut,
                                               json_t** nodeOut,
                                               const char** valueOut,
                                               long* indexOut) {
    json_error_t error;
    json_auto_t* jsonRoot = json_loads(reply->memory, 0, &error);
    if (!jsonRoot) {
        const enum json_error_code jsonError = json_error_code(&error);
        if (jsonError != json_error_out_of_memory) {
            ETCDLIB_LOG_ERROR(
                etcdlib, "ETCDLIB: Error parsing JSON at line %d:%d: %s", error.line, error.column, error.text);
        }
        return jsonError == json_error_out_of_memory ? ETCDLIB_RC_ENOMEM : ETCDLIB_RC_INVALID_RESPONSE_CONTENT;
    }

    const json_t* jsonErrorCode = json_object_get(jsonRoot, ETCD_JSON_ERRORCODE);
    if (jsonErrorCode) {
        const int errorCode = json_is_integer(jsonErrorCode) ? (int)json_integer_value(jsonErrorCode) : -1;
        if (errorCode == 401) {
            return ETCDLIB_RC_EVENT_INDEX_CLEARED;
        }
        json_t* jsonMessage = json_object_get(jsonRoot, "message");
        const char* message = jsonMessage ? json_string_value(jsonMessage) : "No message";
        ETCDLIB_LOG_ERROR(etcdlib, "ETCDLIB: JSON reply contains error code %i: %s", errorCode, message);
        return ETCDLIB_RC_ETCD_ERROR;
    }


    if (expectedAction) { // try to extract action and check if it matches
        json_t* jsonAction = json_object_get(jsonRoot, ETCD_JSON_ACTION);
        if (!jsonAction || !json_is_string(jsonAction)) {
            ETCDLIB_LOG_ERROR(etcdlib, "ETCDLIB: JSON reply is missing required string field " ETCD_JSON_ACTION);
            return ETCDLIB_RC_INVALID_RESPONSE_CONTENT;
        }
        if (strncmp(expectedAction, json_string_value(jsonAction), strlen(expectedAction)) != 0) {
            ETCDLIB_LOG_ERROR(etcdlib,
                              "ETCDLIB: JSON reply action mismatch, expected %s, got %s",
                              expectedAction,
                              json_string_value(jsonAction));
            return ETCDLIB_RC_INVALID_RESPONSE_CONTENT;
        }
    }

    if (nodeOut) {
        *nodeOut = json_object_get(jsonRoot, ETCD_JSON_NODE);
        if (!*nodeOut) {
            ETCDLIB_LOG_ERROR(etcdlib, "ETCDLIB: JSON reply is missing required object field " ETCD_JSON_NODE);
            return ETCDLIB_RC_INVALID_RESPONSE_CONTENT;
        }
    }

    if (valueOut) { // try to extra value
        json_t* jsonNode = json_object_get(jsonRoot, ETCD_JSON_NODE);
        json_t* jsonValue = jsonNode ? json_object_get(jsonNode, ETCD_JSON_VALUE) : NULL;
        const bool jsonValueIsString = jsonValue ? json_is_string(jsonValue) : false;
        if (!jsonNode || !jsonValue || !jsonValueIsString) {
            ETCDLIB_LOG_ERROR(etcdlib,
                              "ETCDLIB: JSON reply is missing required %s",
                              jsonNode ? "string field " ETCD_JSON_VALUE : "object field " ETCD_JSON_NODE);
            return ETCDLIB_RC_INVALID_RESPONSE_CONTENT;
        }
        *valueOut = json_string_value(jsonValue);
    }

    if (indexOut) {
        *indexOut = etcdlib_getCurrentIndex(reply->header);
    }

    if (jsonRootOut) {
        json_incref(jsonRoot); // to offset json_auto_t
        *jsonRootOut = jsonRoot;
    }
    return ETCDLIB_RC_OK;
}

etcdlib_status_t etcdlib_get(etcdlib_t* etcdlib, const char* key, char** valueOut, long* index) {
    *valueOut = NULL;
    if (index) {
        *index = -1;
    }
    key = etcdlib_skipLeadingSlashes(key);

    json_auto_t* jsonRoot = NULL;
    const char* value = NULL;
    etcdlib_status_t rc = etcdlib_performRequest(etcdlib,
                                                 GET,
                                                 NULL,
                                                 ETCDLIB_ACTION_GET,
                                                 &jsonRoot,
                                                 NULL,
                                                 &value,
                                                 index,
                                                 "%s://%s:%d/v2/keys/%s",
                                                 etcdlib->scheme,
                                                 etcdlib->server,
                                                 etcdlib->port,
                                                 key);

    if (rc == ETCDLIB_RC_OK) {
        *valueOut = strdup(value);
        if (!*valueOut) {
            rc = ETCDLIB_RC_ENOMEM;
        }
    }
    return rc;
}

static etcdlib_status_t etcd_getRecursiveValues(const etcdlib_t* etcdlib,
                                                const json_t* jsonRoot,
                                                const json_t* jsonDir,
                                                etcdlib_key_value_callback callback,
                                                void* callbackData) {
    json_t* jsonNodes;
    if ((jsonNodes = json_object_get(jsonDir, ETCD_JSON_NODES)) != NULL) {
        // subarray
        if (json_is_array(jsonNodes)) {
            const size_t len = json_array_size(jsonNodes);
            for (size_t i = 0; i < len; i++) {
                json_t* jsonNode = json_array_get(jsonNodes, i);
                if (json_object_get(jsonNode, ETCD_JSON_NODES)) {
                    // node contains nodes
                    etcdlib_status_t rc = etcd_getRecursiveValues(etcdlib, jsonRoot, jsonNode, callback, callbackData);
                    if (rc != ETCDLIB_RC_OK) {
                        return rc;
                    }
                } else {
                    // leaf node
                    const json_t* jsonKey = json_object_get(jsonNode, ETCD_JSON_KEY);
                    const json_t* jsonValue = json_object_get(jsonNode, ETCD_JSON_VALUE);

                    if (jsonKey && jsonValue && json_is_string(jsonValue) && json_is_string(jsonKey)) {
                        if (!json_object_get(jsonNode, ETCD_JSON_DIR)) {
                            callback(json_string_value(jsonKey), json_string_value(jsonValue), callbackData);
                        }
                    } else {
                        etcdlib_logReply(etcdlib, ETCDLIB_RC_INVALID_RESPONSE_CONTENT, NULL, jsonRoot);
                        ETCDLIB_LOG_ERROR(
                            etcdlib, "ETCDLIB: Invalid node in recursive get. Missing required string key or value");
                        return ETCDLIB_RC_INVALID_RESPONSE_CONTENT;
                    }
                }
            }
        } else {
            etcdlib_logReply(etcdlib, ETCDLIB_RC_INVALID_RESPONSE_CONTENT, NULL, jsonRoot);
            ETCDLIB_LOG_ERROR(etcdlib, "ETCDLIB: Invalid nodes element in recursive get. Expected array");
            return ETCDLIB_RC_INVALID_RESPONSE_CONTENT;
        }
    } else {
        // nop, empty dir
    }

    return ETCDLIB_RC_OK;
}

etcdlib_status_t etcdlib_getDir(
    etcdlib_t* etcdlib, const char* dir, etcdlib_key_value_callback callback, void* callbackData, long* index) {
    if (index) {
        *index = -1;
    }

    dir = etcdlib_skipLeadingSlashes(dir);

    json_auto_t* jsonRoot = NULL;
    json_t* jsonNode = NULL;
    etcdlib_status_t rc = etcdlib_performRequest(etcdlib,
                                                 GET,
                                                 NULL,
                                                 ETCDLIB_ACTION_GET,
                                                 &jsonRoot,
                                                 &jsonNode,
                                                 NULL,
                                                 index,
                                                 "%s://%s:%d/v2/keys/%s",
                                                 etcdlib->scheme,
                                                 etcdlib->server,
                                                 etcdlib->port,
                                                 dir);

    if (rc == ETCDLIB_RC_OK) {
        rc = etcd_getRecursiveValues(etcdlib, jsonRoot, jsonNode, callback, callbackData);
    }
    return rc;
}

etcdlib_status_t etcdlib_set(etcdlib_t* etcdlib, const char* key, const char* value, int ttl, bool prevExist) {
    key = etcdlib_skipLeadingSlashes(key);

    char ttlStr[24];
    ttlStr[0] = '\0';
    if (ttl > 0) {
        (void)snprintf(ttlStr, sizeof(ttlStr), ";ttl=%d", ttl);
    }

    const char* prevExistStr = "";
    if (prevExist) {
        prevExistStr = ";prevExist=true";
    }

    etcdlib_autofree char* requestAutoFree = NULL;
    char requestBuffer[512];
    const int needed = snprintf(requestBuffer, sizeof(requestBuffer), "value=%s%s%s", value, ttlStr, prevExistStr);
    const char* request = requestBuffer;
    if (needed >= sizeof(requestBuffer)) {
        const int written = asprintf(&requestAutoFree, "value=%s%s%s", value, ttlStr, prevExistStr);
        if (written < 0) {
            return ETCDLIB_RC_ENOMEM;
        }
        request = requestAutoFree;
    }

    json_auto_t* jsonRoot = NULL;
    const char* valueReturned = NULL;
    etcdlib_status_t rc = etcdlib_performRequest(etcdlib,
                                                 PUT,
                                                 request,
                                                 ETCDLIB_ACTION_SET,
                                                 &jsonRoot,
                                                 NULL,
                                                 &valueReturned,
                                                 NULL,
                                                 "%s://%s:%d/v2/keys/%s",
                                                 etcdlib->scheme,
                                                 etcdlib->server,
                                                 etcdlib->port,
                                                 key);



    if (rc == ETCDLIB_RC_OK && strcmp(value, valueReturned) != 0) {
        rc = ETCDLIB_RC_INVALID_RESPONSE_CONTENT;
        ETCDLIB_LOG_ERROR(etcdlib, "ETCDLIB: Set value mismatch, expected %s, got %s", value, valueReturned);
    }
    return rc;
}

etcdlib_status_t etcdlib_refresh(etcdlib_t *etcdlib, const char *key, int ttl) {
    key = etcdlib_skipLeadingSlashes(key);

    char buffer[512];
    etcdlib_autofree char *requestAutoFree = NULL;
    const int needed = snprintf(buffer, sizeof(buffer), "prevExist=true;refresh=true;ttl=%d", ttl);
    const char* request = buffer;
    if (needed >= sizeof(buffer)) {
        const int written = asprintf(&requestAutoFree, "prevExist=true;refresh=true;ttl=%d", ttl);
        if (written < 0) {
            return ETCDLIB_RC_ENOMEM;
        }
        request = requestAutoFree;
    }

    etcdlib_status_t rc = etcdlib_performRequest(etcdlib,
                                                 PUT,
                                                 request,
                                                 ETCDLIB_ACTION_UPDATE,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 "%s://%s:%d/v2/keys/%s",
                                                 etcdlib->scheme,
                                                 etcdlib->server,
                                                 etcdlib->port,
                                                 key);
    return rc;
}

static void etcdlib_extractAction(json_t* jsonRoot, const char** action) {
    if (!action) {
        return;
    }
    *action = NULL;
    json_t* jsonAction = json_object_get(jsonRoot, ETCD_JSON_ACTION);
    if (jsonAction && json_is_string(jsonAction)) {
        const char* replyAction = json_string_value(jsonAction);
        if (strncmp(ETCDLIB_ACTION_SET, replyAction, strlen(ETCDLIB_ACTION_SET)) == 0) {
            *action = ETCDLIB_ACTION_SET;
        } else if (strncmp(ETCDLIB_ACTION_DELETE, replyAction, strlen(ETCDLIB_ACTION_DELETE)) == 0) {
            *action = ETCDLIB_ACTION_DELETE;
        } else if (strncmp(ETCDLIB_ACTION_UPDATE, replyAction, strlen(ETCDLIB_ACTION_UPDATE)) == 0) {
            *action = ETCDLIB_ACTION_UPDATE;
        } else if (strncmp(ETCDLIB_ACTION_EXPIRE, replyAction, strlen(ETCDLIB_ACTION_EXPIRE)) == 0) {
            *action = ETCDLIB_ACTION_EXPIRE;
        } else if (strncmp(ETCDLIB_ACTION_COMPARE_AND_SWAP, replyAction, strlen(ETCDLIB_ACTION_COMPARE_AND_SWAP)) == 0) {
            *action = ETCDLIB_ACTION_COMPARE_AND_SWAP;
        } else if (strncmp(ETCDLIB_ACTION_COMPARE_AND_DELETE, replyAction, strlen(ETCDLIB_ACTION_COMPARE_AND_DELETE)) == 0) {
            *action = ETCDLIB_ACTION_COMPARE_AND_DELETE;
        }
    }
}

//TODO make static etcdlib_watchInternal that supports a recursive and non-recursive watch
static etcdlib_status_t etcdlib_watchInternal(etcdlib_t* etcdlib,
                                  bool resursiveWatch,
                                  const char* dir,
                                  long watchIndex,
                                  const char** action,
                                  char** modifiedKey,
                                  char** modifiedValue,
                                  char** previousValue,
                                  bool* isDir,
                                  long* modifiedIndex) {
    dir = etcdlib_skipLeadingSlashes(dir);
    etcdlib_status_t rc;
    json_auto_t* jsonRoot = NULL;
    json_t* jsonNode = NULL;
    const char* resursiveParam = resursiveWatch ? "&recursive=true" : "";
    // TODO refactor, expire has not value, set has not prevNode and this shuold not lead to a
    // RC_INVALID_RESPONSE_CONTENT
    if (watchIndex < 0) {
        rc = etcdlib_performRequest(etcdlib,
                                    GET,
                                    NULL,
                                    NULL,
                                    &jsonRoot,
                                    &jsonNode,
                                    NULL,
                                    NULL,
                                    "%s://%s:%d/v2/keys/%s?wait=true%s",
                                    etcdlib->scheme,
                                    etcdlib->server,
                                    etcdlib->port,
                                    resursiveParam,
                                    dir);
    } else {
        rc = etcdlib_performRequest(etcdlib,
                                    GET,
                                    NULL,
                                    NULL,
                                    &jsonRoot,
                                    &jsonNode,
                                    NULL,
                                    NULL,
                                    "%s://%s:%d/v2/keys/%s?wait=true%s&waitIndex=%ld",
                                    etcdlib->scheme,
                                    etcdlib->server,
                                    etcdlib->port,
                                    dir,
                                    resursiveParam,
                                    watchIndex);
    }

    etcdlib_autofree char* mValue = NULL;
    etcdlib_autofree char* mKey = NULL;
    etcdlib_autofree char* pValue = NULL;
    if (rc == ETCDLIB_RC_OK) {
        json_t* jsonModIndex = json_object_get(jsonNode, ETCD_JSON_MODIFIEDINDEX);
        if (jsonModIndex && json_is_integer(jsonModIndex)) {
            if (modifiedIndex) {
                *modifiedIndex = json_integer_value(jsonModIndex);
            }
        } else {
            rc = ETCDLIB_RC_INVALID_RESPONSE_CONTENT;
            etcdlib_logReply(etcdlib, rc, NULL, jsonRoot);
            ETCDLIB_LOG_ERROR(etcdlib, "ETCDLIB: Invalid watch response, cannot find modified index");
            return rc;
        }

        json_t* jsonDir = json_object_get(jsonNode, ETCD_JSON_DIR);
        if (jsonDir && !json_is_boolean(jsonDir)) {
            rc = ETCDLIB_RC_INVALID_RESPONSE_CONTENT;
            etcdlib_logReply(etcdlib, rc, NULL, jsonRoot);
            ETCDLIB_LOG_ERROR(etcdlib, "ETCDLIB: Invalid watch response, dir flag is not boolean");
            return rc;
        }
        if (isDir) {
            *isDir = jsonDir ? json_boolean(jsonDir) : false;
        }

        etcdlib_extractAction(jsonRoot, action);

        if (modifiedValue) {
            const json_t* jsonValue = json_object_get(jsonNode, ETCD_JSON_VALUE);
            if (jsonValue && json_is_string(jsonValue)) {
                mValue = strdup(json_string_value(jsonValue));
                if (!mValue) {
                    return ETCDLIB_RC_ENOMEM;
                }
            } else {
                // ok, actions like delete will not have a current value.
            }
        }
        if (modifiedKey) {
            const json_t* jsonKey = json_object_get(jsonNode, ETCD_JSON_KEY);
            if (jsonKey && json_is_string(jsonKey)) {
                mKey = strdup(json_string_value(jsonKey));
                if (!mKey) {
                    return ETCDLIB_RC_ENOMEM;
                }
            } else {
                rc = ETCDLIB_RC_INVALID_RESPONSE_CONTENT;
                etcdlib_logReply(etcdlib, rc, NULL, jsonRoot);
                ETCDLIB_LOG_ERROR(etcdlib, "ETCDLIB: Invalid watch response, cannot find modified key");
                return rc;
            }
        }
        if (previousValue) {
            const json_t* jsonPrevValue = json_object_get(jsonRoot, ETCD_JSON_PREVNODE);
            const json_t* jsonValue = jsonPrevValue ? json_object_get(jsonPrevValue, ETCD_JSON_VALUE) : NULL;
            if (jsonValue && json_is_string(jsonValue)) {
                pValue = strdup(json_string_value(jsonValue));
                if (!pValue) {
                    return ETCDLIB_RC_ENOMEM;
                }
            } else {
                // ok, actions like set will not have a previous value
            }
        }
    }

    if (rc == ETCDLIB_RC_OK) {
        if (modifiedValue) {
            *modifiedValue = etcdlib_steal_ptr(mValue);
        }
        if (modifiedKey) {
            *modifiedKey = etcdlib_steal_ptr(mKey);
        }
        if (previousValue) {
            *previousValue = etcdlib_steal_ptr(pValue);
        }
    }
    return rc;
}

etcdlib_status_t etcdlib_watchDir(etcdlib_t* etcdlib,
                                  const char* dir,
                                  long watchIndex,
                                  const char** action,
                                  char** modifiedKey,
                                  char** modifiedValue,
                                  char** previousValue,
                                  bool* isDir,
                                  long* modifiedIndex) {
    return etcdlib_watchInternal(
        etcdlib, true, dir, watchIndex, action, modifiedKey, modifiedValue, previousValue, isDir, modifiedIndex);
}

etcdlib_status_t etcdlib_delete(etcdlib_t* etcdlib, const char* key) {
    key = etcdlib_skipLeadingSlashes(key);

    etcdlib_status_t rc = etcdlib_performRequest(etcdlib,
                                                 DELETE,
                                                 NULL,
                                                 ETCDLIB_ACTION_DELETE,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 "%s://%s:%d/v2/keys/%s?recursive=true",
                                                 etcdlib->scheme,
                                                 etcdlib->server,
                                                 etcdlib->port,
                                                 key);
    return rc;
}

static size_t etcdlib_writeMemoryCallback(const void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realSize = size * nmemb;
    etcdlib_reply_data_t* mem = (etcdlib_reply_data_t*)userp;

    void* newMem = realloc(mem->memory, mem->memorySize + realSize + 1);
    if (newMem == NULL) {
        return 0; /*Out of memory*/
    }
    mem->memory = newMem;

    memcpy(&(mem->memory[mem->memorySize]), contents, realSize);
    mem->memorySize += realSize;
    mem->memory[mem->memorySize] = 0;

    return realSize;
}

static size_t etcdlib_writeHeaderCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    etcdlib_reply_data_t* mem = (etcdlib_reply_data_t*)userp;

    void* newHeader = realloc(mem->header, mem->headerSize + realsize + 1);
    if (newHeader == NULL) {
        return 0; /* out of memory! */
    }
    mem->header = newHeader;

    memcpy(&(mem->header[mem->headerSize]), contents, realsize);
    mem->headerSize += realsize;
    mem->header[mem->headerSize] = 0;

    return realsize;
}

static CURL* etcdlib_getCurl(etcdlib_t* etcdlib) {
    __atomic_fetch_add(&etcdlib->activeRequest, 1, __ATOMIC_ACQ_REL);
    CURL* curl;
    if (etcdlib->curlMulti) {
        curl = curl_easy_init();
    } else {
        curl = pthread_getspecific(etcdlibThreadSpecificCurlsKey);
        if (!curl) {
            curl = curl_easy_init();
            if (curl) {
                int rc = pthread_setspecific(etcdlibThreadSpecificCurlsKey, curl);
                if (rc != 0) {
                    curl_easy_cleanup(curl);
                    ETCDLIB_LOG_ERROR(etcdlib, "ETCDLIB: Error setting thread specific data");
                    curl = NULL;
                }
            }
        }
    }
    return curl;
}

static void etcdlib_cleanupCurl(etcdlib_t* etcdlib, CURL* curl) {
    __atomic_fetch_sub(&etcdlib->activeRequest, 1, __ATOMIC_ACQ_REL);
    if (etcdlib->curlMulti) {
        curl_easy_cleanup(curl);
    } else {
        curl_easy_reset(curl);
    }
}

static etcdlib_status_t etcdlib_transformCurlCodeToEtcdlibStatus(CURLcode code, CURL* curl) {
    if (code == CURLE_OK) {
        return ETCDLIB_RC_OK;
    }
    if (code == CURLE_HTTP_RETURNED_ERROR) {
        long httpCode;
        (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        return ETCDLIB_INTERNAL_HTTPCODE_FLAG | httpCode;
    }
    if (code == CURLE_OPERATION_TIMEDOUT) {
        return ETCDLIB_RC_TIMEOUT;
    }
    return ETCDLIB_INTERNAL_CURLCODE_FLAG | code;
}

static etcdlib_status_t etcdlib_performRequest(etcdlib_t* etcdlib,
                                               request_t request,
                                               const char* reqData,
                                               const char* expectedAction,
                                               json_t** jsonRootOut,
                                               json_t** nodeOut,
                                               const char** valueOut,
                                               long* indexOut,
                                               const char* urlFmt,
                                               ...) {
    etcdlib_reply_data_t reply = etcdlib_constructReplyData(true, true);
    if (!reply.memory || !reply.header) {
        return ETCDLIB_RC_ENOMEM;
    }

    etcdlib_autofree char* heapBuf = NULL;
    char localBuf[128];
    va_list args;
    va_start(args, urlFmt);
    const char* url = etcdlib_createUrl(localBuf, sizeof(localBuf), &heapBuf, urlFmt, args);
    va_end(args);

    if (!url) {
        return ETCDLIB_RC_ENOMEM;
    }

    CURL* curl = etcdlib_getCurl(etcdlib);
    if (!curl) {
        return ETCDLIB_RC_ENOMEM;
    }

    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, etcdlib->timeoutInMs);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, etcdlib->connectTimeoutInMs);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, etcdlib_writeMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L); // fail on HTTP error codes outside 200-399
    if (true /*can this be based on PUT , etc */) {
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &reply);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, etcdlib_writeHeaderCallback);
    }

    if (request == PUT) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, reqData);
    } else if (request == DELETE) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else if (request == GET) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
    }

    etcdlib_status_t rc = ETCDLIB_RC_OK;
    if (etcdlib->curlMulti != NULL) {
        CURLMcode mc = curl_multi_add_handle(etcdlib->curlMulti, curl);
        if (mc != CURLM_OK) {
            rc = ETCDLIB_INTERNAL_CURLMCODE_FLAG | mc;
        } else {
            int running;
            mc = curl_multi_perform(etcdlib->curlMulti, &running);
            if (mc == CURLM_OK) {
                rc = etcdlib_waitForMultiCurl(etcdlib, curl);
            } else {
                rc = ETCDLIB_INTERNAL_CURLMCODE_FLAG | mc;
            }
        }
    } else {
        const CURLcode code = curl_easy_perform(curl);
        rc = etcdlib_transformCurlCodeToEtcdlibStatus(code, curl);
    }

    if (rc == ETCDLIB_RC_OK) {
        rc = etcdlib_parseEtcdReply(etcdlib, &reply, expectedAction, jsonRootOut, nodeOut, valueOut, indexOut);
        etcdlib_logReply(etcdlib, rc, reply.memory, NULL);
    }
    etcdlib_cleanupCurl(etcdlib, curl);
    etcdlib_freeReplyData(&reply);

    return rc;
}

static etcdlib_completed_curl_entry_t etcdlib_removeCompletedCurlEntry(etcdlib_t* lib, CURL* curl) {
    etcdlib_completed_curl_entry_t entry = {NULL, CURLE_FAILED_INIT};
    pthread_mutex_lock(&lib->curlMutex);
    for (int i = 0; i < lib->completedCurlEntriesSize; i++) {
        if (lib->completedCurlEntries[i].curl == curl) {
            memcpy(&entry, &lib->completedCurlEntries[i], sizeof(entry));
            lib->completedCurlEntries[i].curl = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&lib->curlMutex);
    return entry;

}

static bool etcdlib_addCompletedCurlEntryToList(etcdlib_t* lib, CURL* curl, CURLcode res) {
    for (int i = 0; i < lib->completedCurlEntriesSize; i++) {
        if (lib->completedCurlEntries[i].curl == NULL) {
            lib->completedCurlEntries[i].curl = curl;
            lib->completedCurlEntries[i].res = res;
            return true;
        }
    }
    return false;
}

static etcdlib_status_t etcdlib_addCompletedCurlEntry(etcdlib_t* lib, CURL* curl, CURLcode res) {
    etcdlib_status_t rc = ETCDLIB_RC_OK;
    pthread_mutex_lock(&lib->curlMutex);
    bool added = etcdlib_addCompletedCurlEntryToList(lib, curl, res);
    if (!added) {
        // no free entry found, try to expand the array
        int newSize = lib->completedCurlEntriesSize * 2;
        newSize = newSize > ETCDLIB_MAX_COMPLETED_CURL_ENTRIES ? ETCDLIB_MAX_COMPLETED_CURL_ENTRIES : newSize;
        if (lib->completedCurlEntriesSize >= ETCDLIB_MAX_COMPLETED_CURL_ENTRIES) {
            rc = ETCDLIB_INTERNAL_RC_MAX_ENTRIES_REACHED;
        } else {
            etcdlib_completed_curl_entry_t* newEntries =
                realloc(lib->completedCurlEntries, newSize * sizeof(*newEntries));
            if (newEntries) {
                lib->completedCurlEntries = newEntries;
                lib->completedCurlEntriesSize = newSize;
                added = etcdlib_addCompletedCurlEntryToList(lib, curl, res);
            } else {
                rc = ETCDLIB_RC_ENOMEM;
            }
        }
    }
    pthread_mutex_unlock(&lib->curlMutex);
    return rc;
}

static CURLMcode etcdlib_removeCurlFromCurlMulti(etcdlib_t* lib, CURL* curl) {
    CURLMcode mCode = curl_multi_remove_handle(lib->curlMulti, curl);
    if (mCode != CURLM_OK) {
        ETCDLIB_LOG_ERROR(lib, "ETCDLIB: curl_multi_remove_handle failed with code %d", mCode);
    }
    return mCode;
}

static etcdlib_status_t etcdlib_waitForMultiCurl(etcdlib_t* lib, CURL* curl) {
    etcdlib_status_t rc = ETCDLIB_RC_OK;
    CURLcode code = CURLE_OK;
    int nrOfHandlesRunning;
    bool etcdlibRunning = true;
    do {
        const etcdlib_completed_curl_entry_t entry = etcdlib_removeCompletedCurlEntry(lib, curl);
        if (entry.curl) {
            code = entry.res;
            break;
        }
        CURLMsg* msg = curl_multi_info_read(lib->curlMulti, &nrOfHandlesRunning);
        if (msg && (msg->msg == CURLMSG_DONE) && (msg->easy_handle == curl)) {
            const CURLMcode mCode = etcdlib_removeCurlFromCurlMulti(lib, curl);
            if (mCode != CURLM_OK) {
                return ETCDLIB_INTERNAL_CURLMCODE_FLAG | mCode;
            }
            code = msg->data.result;
            break;
        }
        if (msg && (msg->msg == CURLMSG_DONE)) {
            //found another, non-matching, completed curl, add it to the completedCurlEntries
            const CURLMcode mCode = etcdlib_removeCurlFromCurlMulti(lib, curl);
            if (mCode != CURLM_OK) {
                return ETCDLIB_INTERNAL_CURLMCODE_FLAG | mCode;
            }
            rc = etcdlib_addCompletedCurlEntry(lib, msg->easy_handle, msg->data.result);
            while (rc == ETCDLIB_INTERNAL_RC_MAX_ENTRIES_REACHED) {
                usleep(1000); //wait a bit and try again
                rc = etcdlib_addCompletedCurlEntry(lib, msg->easy_handle, msg->data.result);
            }
        } else {
            //no completed curl found, wait for more events
            int running;
            rc = curl_multi_perform(lib->curlMulti, &running);
        }
        etcdlibRunning = __atomic_load_n(&lib->curlMultiRunning, __ATOMIC_ACQUIRE);
    } while (etcdlibRunning && rc == ETCDLIB_RC_OK);

    if (!etcdlibRunning) {
        return ETCDLIB_RC_STOPPING;
    }
    if (rc == ETCDLIB_RC_OK) {
        rc = etcdlib_transformCurlCodeToEtcdlibStatus(code, curl);
    }
    return rc;
}

bool etcdlib_isStatusHttpError(etcdlib_status_t status) {
    return status & ETCDLIB_INTERNAL_HTTPCODE_FLAG;
}

int etcdlib_getHttpCodeFromStatus(etcdlib_status_t status) {
    if (status & ETCDLIB_INTERNAL_HTTPCODE_FLAG) {
        return status & ~ETCDLIB_INTERNAL_HTTPCODE_FLAG;
    }
    return 0;
}

// bool etcdlib_isStatusCurlError(etcdlib_status_t status) {
//     return status & ETCDLIB_INTERNAL_CURLCODE_FLAG;
// }
//
// int etcdlib_getCurlCodeFromStatus(etcdlib_status_t status) {
//     if (status & ETCDLIB_INTERNAL_CURLCODE_FLAG) {
//         return status & ~ETCDLIB_INTERNAL_CURLCODE_FLAG;
//     }
//     return 0;
// }