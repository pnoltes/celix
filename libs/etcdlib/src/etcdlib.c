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

#define ETCD_HEADER_INDEX               "X-Etcd-Index: "

#define MAX_OVERHEAD_LENGTH             64
#define DEFAULT_CURL_TIMEOUT            10
#define DEFAULT_CURL_CONNECT_TIMEOUT    10

#define ETCDLIB_MAX_COMPLETED_CURL_ENTRIES 16

/** Internal error codes, extends ETCDLIC_RC_* errors, but are not exposed to the user */
#define ETCDLIB_INTERNAL_RC_MAX_ENTRIES_REACHED 1002

typedef struct /*anon*/ {
    CURL* curl;
    CURLcode res;
} etcdlib_completed_curl_entry_t;

struct etcdlib {
    char *server;
    int port;

    CURLM* curlMulti;
    bool curlMultiRunning; //atomic flag, only used if curlMulti is not NULL
    pthread_mutex_t curlMutex; //protects completedCurlEntries, only used if curlMulti is not NULL
    etcdlib_completed_curl_entry_t* completedCurlEntries; //only used if curlMulti is not NULL
    size_t completedCurlEntriesSize; //only used if curlMulti is not NULL
};

typedef enum {
    GET, PUT, DELETE
} request_t;

static etcdlib_status_t etcdlib_performRequest(etcdlib_t* lib,
                                               char* url,
                                               request_t request,
                                               const char* reqData,
                                               etcdlib_reply_data_t* replyData,
                                               CURL** curlOut);

static etcdlib_status_t etcdlib_waitForMultiCurl(etcdlib_t* lib, CURL* curl);

static void etcdlib_autofree_callback(void* ptr) {
    void** pp = (void**)ptr;
    free(*pp);
}

#define etcdlib_autofree __attribute__((cleanup(etcdlib_autofree_callback)))

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

    const char* serverConfigured = options->server ? options->server : "localhost";
    const int portConfigured = options->port > 0 ? options->port : 2379;

    etcdlib_autofree etcdlib_t *lib = calloc(1, sizeof(*lib));
    etcdlib_autofree char* server = strndup(serverConfigured, 1024 * 1024 * 10);
    etcdlib_autofree etcdlib_completed_curl_entry_t* completedEntries = NULL;
    if (options->useMultiCurl) {
        completedEntries = calloc(ETCDLIB_MAX_COMPLETED_CURL_ENTRIES, sizeof(*lib->completedCurlEntries));
    }

    if (!lib || !server || (options->useMultiCurl && !completedEntries)) {
        return ETCDLIB_RC_ENOMEM;
    }

    lib->server = etcdlib_steal_ptr(server);
    lib->port = portConfigured;
    lib->completedCurlEntriesSize = ETCDLIB_MAX_COMPLETED_CURL_ENTRIES;

    if (options->useMultiCurl) {
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
    }

    *etcdlib = etcdlib_steal_ptr(lib);
    return ETCDLIB_RC_OK;
}

static void etcdlib_stopMultiCurl(etcdlib_t* lib) {
    __atomic_store_n(&lib->curlMultiRunning, false, __ATOMIC_RELEASE);
    int runningHandles;
    curl_multi_perform(lib->curlMulti, &runningHandles);
    for (int i = 0; i < runningHandles; i++) {
        curl_multi_wakeup(lib->curlMulti); //note wakeup only wakes up a single handle
    }

    //wait until there are no more running handles
    while (runningHandles > 0) {
        curl_multi_perform(lib->curlMulti, &runningHandles);
        usleep(1000);
    }
}

void etcdlib_destroy(etcdlib_t *etcdlib) {
    if (etcdlib) {
        if (etcdlib->curlMulti) {
            etcdlib_stopMultiCurl(etcdlib);
            curl_multi_cleanup(etcdlib->curlMulti);
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

const char* etcdlib_strerror(int error) {
    if (error & ETCDLIB_INTERNAL_CURLCODE_FLAG) {
        return curl_easy_strerror(error & ~ETCDLIB_INTERNAL_CURLCODE_FLAG);
    } else if (error & ETCDLIB_INTERNAL_CURLMCODE_FLAG) {
        return curl_multi_strerror(error & ~ETCDLIB_INTERNAL_CURLMCODE_FLAG);
    }

    switch (error) {
    case ETCDLIB_RC_OK:
        return "ETCDLIB OK";
    case ETCDLIB_RC_TIMEOUT:
        return "ETCDLIB Timeout";
    case ETCDLIB_RC_EVENT_CLEARED:
        return "ETCDLIB Event Cleared";
    case ETCDLIB_RC_ENOMEM:
        return "ETCDLIB Out of memory or maximum number of curl handles reached";
    case ETCDLIB_RC_INVALID_RESPONSE:
        return "ETCDLIB Content of response is invalid (not JSON, missing required fields or missing header)";
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

static long long etcd_get_current_index(const char *headerData) {
    long long index = -1;
    if (headerData == NULL) {
        return index;
    }
    char *indexStr = strstr(headerData, ETCD_HEADER_INDEX);
    indexStr += strlen(ETCD_HEADER_INDEX);

    char *endptr;
    index = strtoll(indexStr, &endptr, 10);
    if (endptr == indexStr) {
        index = -1;
    }
    return index;
}

etcdlib_status_t etcdlib_get(etcdlib_t *etcdlib, const char *key, char **value, long long *index) {
    *value = NULL;
    json_t *js_root = NULL;
    json_t *js_node = NULL;
    json_t *js_value = NULL;
    json_error_t error;
    etcdlib_reply_data_t reply;

    reply.memory = malloc(1); /* will be grown as needed by the realloc above */
    reply.memorySize = 0; /* no data at this point */
    reply.header = malloc(1); /* will be grown as needed by the realloc above */
    reply.headerSize = 0; /* no data at this point */

    etcdlib_autofree char *url;
    asprintf(&url, "http://%s:%d/v2/keys/%s", etcdlib->server, etcdlib->port, key);
    if (!url) {
        return ETCDLIB_RC_ENOMEM;
    }

    CURL* curl;
    CURLcode code = etcdlib_performRequest(etcdlib, url, GET, NULL, (void*)&reply, &curl);

    int rc = ETCDLIB_RC_OK;
    if (code == CURLE_OK) {
        rc = ETCDLIB_RC_INVALID_RESPONSE;
        js_root = json_loads(reply.memory, 0, &error);

        if (!js_root) {
            rc = ETCDLIB_RC_ENOMEM;
        } else if (js_root != NULL) {
            js_node = json_object_get(js_root, ETCD_JSON_NODE);
        }

        if (js_node != NULL) {
            js_value = json_object_get(js_node, ETCD_JSON_VALUE);
            if (js_value != NULL) {
                if (index) {
                    *index = etcd_get_current_index(reply.header);
                }
                *value = strdup(json_string_value(js_value));
                if (!*value) {
                    rc = ETCDLIB_RC_ENOMEM;
                } else {
                    rc = ETCDLIB_RC_OK;
                }
            }
        }
        if (js_root != NULL) {
            json_decref(js_root);
        }
    } else if (code == CURLE_OPERATION_TIMEDOUT) {
        //timeout
        rc = ETCDLIB_RC_TIMEOUT;
    } else {
        rc = ETCDLIB_INTERNAL_CURLCODE_FLAG | code;
    }
    curl_easy_cleanup(curl);

    if (reply.memory) {
        free(reply.memory);
    }
    return rc;
}

static etcdlib_status_t
etcd_get_recursive_values(json_t *js_root, etcdlib_key_value_callback callback, void *arg, json_int_t *mod_index) {
    json_t* js_nodes;
    if ((js_nodes = json_object_get(js_root, ETCD_JSON_NODES)) != NULL) {
        // subarray
        if (json_is_array(js_nodes)) {
            const size_t len = json_array_size(js_nodes);
            for (size_t i = 0; i < len; i++) {
                json_t *js_object = json_array_get(js_nodes, i);
                json_t *js_mod_index = json_object_get(js_object, ETCD_JSON_MODIFIEDINDEX);

                if (js_mod_index != NULL) {
                    const json_int_t index = json_integer_value(js_mod_index);
                    if (*mod_index < index) {
                        *mod_index = index;
                    }
                } else {
                    //No index found for key
                    return ETCDLIB_RC_INVALID_RESPONSE;
                }

                if (json_object_get(js_object, ETCD_JSON_NODES)) {
                    // node contains nodes
                    etcd_get_recursive_values(js_object, callback, arg, mod_index);
                } else {
                    const json_t* js_key = json_object_get(js_object, ETCD_JSON_KEY);
                    const json_t* js_value = json_object_get(js_object, ETCD_JSON_VALUE);

                    if (js_key && js_value) {
                        if (!json_object_get(js_object, ETCD_JSON_DIR)) {
                            callback(json_string_value(js_key), json_string_value(js_value), arg);
                        }
                    } //else empty etcd directory, not an error.

                }
            }
        } else {
            //mis-formatted JSON. nodes element is not an array
            return ETCDLIB_RC_INVALID_RESPONSE;
        }
    } else {
        //nodes element not found
        return ETCDLIB_RC_INVALID_RESPONSE;
    }

    if (*mod_index < 0) {
        return ETCDLIB_RC_INVALID_RESPONSE;
    }

    return ETCDLIB_RC_OK;
}

etcdlib_status_t etcdlib_get_directory(etcdlib_t *etcdlib, const char *directory, etcdlib_key_value_callback callback, void *arg,
                          long long *modifiedIndex) {

    json_t *js_root = NULL;
    json_t *js_rootnode = NULL;
    json_t *js_index = NULL;

    json_error_t error;
    etcdlib_reply_data_t reply;

    etcdlib_autofree char* memory = malloc(1);
    etcdlib_autofree char* header = malloc(1);

    reply.memory = memory; /* will be grown as needed by the realloc above */
    reply.memorySize = 0; /* no data at this point */
    reply.header = header; /* will be grown as needed by the realloc above */
    reply.headerSize = 0; /* no data at this point */

    etcdlib_status_t rc = ETCDLIB_RC_OK;
    etcdlib_autofree char *url = NULL;

    asprintf(&url, "http://%s:%d/v2/keys/%s?recursive=true", etcdlib->server, etcdlib->port, directory);
    if (!url) {
        return ETCDLIB_RC_ENOMEM;
    }

    CURL* curl = NULL;
    CURLcode code = etcdlib_performRequest(etcdlib->curlMulti, url, GET, NULL, (void *) &reply, &curl);
    if (code == CURLE_OK) {
        rc = ETCDLIB_RC_INVALID_RESPONSE;
        js_root = json_loads(reply.memory, 0, &error);

        if (js_root) {
            js_rootnode = json_object_get(js_root, ETCD_JSON_NODE);
            js_index = json_object_get(js_root, ETCD_JSON_INDEX);
        }

        if (js_rootnode != NULL) {
            long long modIndex = 0;
            long long *ptrModIndex = NULL;
            if (modifiedIndex != NULL) {
                *modifiedIndex = 0;
                ptrModIndex = modifiedIndex;
            } else {
                ptrModIndex = &modIndex;
            }
            rc = etcd_get_recursive_values(js_rootnode, callback, arg, (json_int_t *) ptrModIndex);
            long long indexFromHeader = etcd_get_current_index(reply.header);
            if (indexFromHeader > *ptrModIndex) {
                *ptrModIndex = indexFromHeader;
            }
        } else if ((modifiedIndex != NULL) && (js_index != NULL)) {
            // Error occurred, retrieve the index of ETCD from the error code
            *modifiedIndex = json_integer_value(js_index);;
        }
        if (js_root != NULL) {
            json_decref(js_root);
        }
    } else if (code == CURLE_OPERATION_TIMEDOUT) {
        rc = ETCDLIB_RC_TIMEOUT;
    } else {
        rc = ETCDLIB_INTERNAL_CURLCODE_FLAG | code;
    }
    curl_easy_cleanup(curl);

    return rc;
}

etcdlib_status_t etcdlib_set(etcdlib_t *etcdlib, const char *key, const char *value, int ttl, bool prevExist) {

    json_error_t error;
    json_t *js_root = NULL;
    json_t *js_node = NULL;
    json_t *js_value = NULL;
    etcdlib_autofree char *url = NULL;
    size_t req_len = strlen(value) + MAX_OVERHEAD_LENGTH;
    char request[req_len];
    char *requestPtr = request;
    etcdlib_reply_data_t reply;

    /* Skip leading '/', etcd cannot handle this. */
    while (*key == '/') {
        key++;
    }

    etcdlib_autofree char *memory = malloc(1);
    reply.memory = memory; /* will be grown as needed by the realloc above */
    reply.memorySize = 0; /* no data at this point */
    reply.header = NULL; /* will be grown as needed by the realloc above */
    reply.headerSize = 0; /* no data at this point */

    asprintf(&url, "http://%s:%d/v2/keys/%s", etcdlib->server, etcdlib->port, key);
    if (!url) {
        return ETCDLIB_RC_ENOMEM;
    }

    requestPtr += snprintf(requestPtr, req_len, "value=%s", value);
    if (ttl > 0) {
        requestPtr += snprintf(requestPtr, req_len - (requestPtr - request), ";ttl=%d", ttl);
    }

    if (prevExist) {
        requestPtr += snprintf(requestPtr, req_len - (requestPtr - request), ";prevExist=true");
    }

    CURL* curl = NULL;
    CURLcode code = etcdlib_performRequest(etcdlib->curlMulti, url, PUT, request, (void *) &reply, &curl);

    etcdlib_status_t rc = ETCDLIB_RC_OK;
    if (code == CURLE_OK) {
        rc = ETCDLIB_RC_INVALID_RESPONSE;
        js_root = json_loads(reply.memory, 0, &error);

        if (js_root != NULL) {
            js_node = json_object_get(js_root, ETCD_JSON_NODE);
        }
        if (js_node != NULL) {
            js_value = json_object_get(js_node, ETCD_JSON_VALUE);
        }
        if (js_value != NULL && json_is_string(js_value)) {
            if (strcmp(json_string_value(js_value), value) == 0) {
                rc = ETCDLIB_RC_OK;
            }
        }
        if (js_root != NULL) {
            json_decref(js_root);
        }
    } else {
        rc = ETCDLIB_INTERNAL_CURLCODE_FLAG | code;
    }
    curl_easy_cleanup(curl);

    return rc;
}

etcdlib_status_t etcdlib_refresh(etcdlib_t *etcdlib, const char *key, int ttl) {
    etcdlib_autofree char *url = NULL;
    size_t req_len = MAX_OVERHEAD_LENGTH;
    char request[req_len];
    etcdlib_reply_data_t reply;

    /* Skip leading '/', etcd cannot handle this. */
    while (*key == '/') {
        key++;
    }

    etcdlib_autofree char *memory = malloc(1);
    reply.memory = memory; /* will be grown as needed by the realloc above */
    reply.memorySize = 0; /* no data at this point */
    reply.header = NULL; /* will be grown as needed by the realloc above */
    reply.headerSize = 0; /* no data at this point */

    asprintf(&url, "http://%s:%d/v2/keys/%s", etcdlib->server, etcdlib->port, key);
    if (!url) {
        return ETCDLIB_RC_ENOMEM;
    }
    snprintf(request, req_len, "ttl=%d;prevExists=true;refresh=true", ttl);

    CURL* curl = NULL;
    etcdlib_status_t rc = etcdlib_performRequest(etcdlib, url, PUT, request, (void *) &reply, &curl);

    if (rc == ETCDLIB_RC_OK && reply.memory != NULL) {
        rc = ETCDLIB_RC_INVALID_RESPONSE;
        json_error_t error;
        json_t *root = NULL;
        if (reply.memory) {
            root = json_loads(reply.memory, 0, &error);
        }
        if (root) {
            json_t *errorCode = json_object_get(root, ETCD_JSON_ERRORCODE);
            if (errorCode == NULL) {
                rc = ETCDLIB_RC_OK;
            } else {
                rc = ETCDLIB_RC_INVALID_RESPONSE; //TODO improve error code for ETCD_JSON_ERRORCODE (check etcd doc)
            }
            json_decref(root);
        }
    } else {
        rc = ETCDLIB_INTERNAL_CURLCODE_FLAG | rc;
    }
    curl_easy_cleanup(curl);

    return rc;
}

etcdlib_status_t etcdlib_watch(etcdlib_t* etcdlib,
                               const char* key,
                               long long index,
                               char** actionOut,
                               char** prevValueOut,
                               char** valueOut,
                               char** rkeyOut,
                               long long* modifiedIndex) {
    json_error_t error;
    json_t *js_root = NULL;
    json_t *js_node = NULL;
    json_t *js_prevNode = NULL;
    json_t *js_action = NULL;
    json_t *js_value = NULL;
    json_t *js_rkey = NULL;
    json_t *js_prevValue = NULL;
    json_t *js_modIndex = NULL;
    json_t *js_index = NULL;
    etcdlib_reply_data_t reply;

    etcdlib_autofree char* url = NULL;
    etcdlib_autofree char* action = NULL;
    etcdlib_autofree char* prevValue = NULL;
    etcdlib_autofree char* value = NULL;
    etcdlib_autofree char* rkey = NULL;
    etcdlib_autofree char* memory = malloc(1);

    if (!memory) {
        return ETCDLIB_RC_ENOMEM;
    }

    reply.memory = memory; /* will be grown as needed by the realloc above */
    reply.memorySize = 0; /* no data at this point */
    reply.header = NULL; /* will be grown as needed by the realloc above */
    reply.headerSize = 0; /* no data at this point */

    if (index != 0) {
        asprintf(&url,
                 "http://%s:%d/v2/keys/%s?wait=true&recursive=true&waitIndex=%lld",
                 etcdlib->server,
                 etcdlib->port,
                 key,
                 index);
    } else {
        asprintf(&url, "http://%s:%d/v2/keys/%s?wait=true&recursive=true", etcdlib->server, etcdlib->port, key);
    }
    if (!url) {
        return ETCDLIB_RC_ENOMEM;
    }

    CURL *curl = NULL;
    CURLcode code = etcdlib_performRequest(etcdlib->curlMulti, url, GET, NULL, (void *) &reply, &curl);

    etcdlib_status_t rc = ETCDLIB_RC_OK;
    if (code == CURLE_OK) {
        rc = ETCDLIB_RC_INVALID_RESPONSE;
        js_root = json_loads(reply.memory, 0, &error);

        if (js_root) {
            js_action = json_object_get(js_root, ETCD_JSON_ACTION);
            js_node = json_object_get(js_root, ETCD_JSON_NODE);
            js_prevNode = json_object_get(js_root, ETCD_JSON_PREVNODE);
            js_index = json_object_get(js_root, ETCD_JSON_INDEX);
            rc = ETCDLIB_RC_OK; //TODO refactor and decide when a call is successful (no missing content)
        }
        if (js_prevNode != NULL) {
            js_prevValue = json_object_get(js_prevNode, ETCD_JSON_VALUE);
        }
        if (js_node != NULL) {
            js_rkey = json_object_get(js_node, ETCD_JSON_KEY);
            js_value = json_object_get(js_node, ETCD_JSON_VALUE);
            js_modIndex = json_object_get(js_node, ETCD_JSON_MODIFIEDINDEX);
        }
        if (js_prevNode != NULL) {
            js_prevValue = json_object_get(js_prevNode, ETCD_JSON_VALUE);
        }
        if ((prevValue != NULL) && (js_prevValue != NULL) && (json_is_string(js_prevValue))) {
            prevValue = strdup(json_string_value(js_prevValue));
            if (!prevValue) {
                return ETCDLIB_RC_ENOMEM;
            }
        }
        if (modifiedIndex != NULL) {
            if ((js_modIndex != NULL) && (json_is_integer(js_modIndex))) {
                *modifiedIndex = json_integer_value(js_modIndex);
            } else if ((js_index != NULL) && (json_is_integer(js_index))) {
                *modifiedIndex = json_integer_value(js_index);
            } else {
                *modifiedIndex = index;
            }
        }
        if ((rkey != NULL) && (js_rkey != NULL) && (json_is_string(js_rkey))) {
            rkey = strdup(json_string_value(js_rkey));
            if (!rkey) {
                return ETCDLIB_RC_ENOMEM;;
            }
        }
        if ((action != NULL) && (js_action != NULL) && (json_is_string(js_action))) {
            action = strdup(json_string_value(js_action));
            if (!action) {
                return ETCDLIB_RC_ENOMEM;
            }
        }
        if ((value != NULL) && (js_value != NULL) && (json_is_string(js_value))) {
            value = strdup(json_string_value(js_value));
            if (!value) {
                return ETCDLIB_RC_ENOMEM;
            }
        }
        if (js_root != NULL) {
            json_decref(js_root);
        }
    } else if (code == CURLE_OPERATION_TIMEDOUT) {
        rc = ETCDLIB_RC_TIMEOUT;
    } else if (code == CURLE_AUTH_ERROR) {
        // AUTH_ERROR, 401, for etcd means that the provided watch index event is cleared and no longer available.
        // This means that a watch is no longer possible and a new etcdlib_get -> etcdlib_watch setup needs to be done
        rc = ETCDLIB_RC_EVENT_CLEARED;
    } else {
        rc = ETCDLIB_INTERNAL_CURLCODE_FLAG | code;
    }
    curl_easy_cleanup(curl);

    if (rc != ETCDLIB_RC_OK) {
        *actionOut = etcdlib_steal_ptr(action);
        *prevValueOut = etcdlib_steal_ptr(prevValue);
        *valueOut = etcdlib_steal_ptr(value);
        *rkeyOut = etcdlib_steal_ptr(rkey);
    }

    return rc;
}

etcdlib_status_t etcdlib_del(etcdlib_t *etcdlib, const char *key) {

    json_error_t error;
    json_t *js_root = NULL;
    json_t *js_node = NULL;
    etcdlib_autofree char *url = NULL;
    etcdlib_reply_data_t reply;

    etcdlib_autofree char *memory = malloc(1);
    reply.memory = memory; /* will be grown as needed by the realloc above */
    reply.memorySize = 0; /* no data at this point */
    reply.header = NULL; /* will be grown as needed by the realloc above */
    reply.headerSize = 0; /* no data at this point */

    CURL* curl = NULL;
    asprintf(&url, "http://%s:%d/v2/keys/%s?recursive=true", etcdlib->server, etcdlib->port, key);
    if (!url) {
        return ETCDLIB_RC_ENOMEM;
    }
    const CURLcode code = etcdlib_performRequest(etcdlib->curlMulti, url, DELETE, NULL, (void *) &reply, &curl);

    etcdlib_status_t rc = ETCDLIB_RC_OK;
    if (code == CURLE_OK) {
        js_root = json_loads(reply.memory, 0, &error);
        if (js_root != NULL) {
            js_node = json_object_get(js_root, ETCD_JSON_NODE);
        }

        if (js_node != NULL) {
            rc = ETCDLIB_RC_OK;
        }

        if (js_root != NULL) {
            json_decref(js_root);
        }
    } else {
        rc = ETCDLIB_INTERNAL_CURLCODE_FLAG | code;
    }
    curl_easy_cleanup(curl);

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

static etcdlib_status_t etcdlib_performRequest(etcdlib_t* lib,
                                               char* url,
                                               request_t request,
                                               const char* requestData,
                                               etcdlib_reply_data_t* replyData,
                                               CURL** curlOut) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return ETCDLIB_RC_ENOMEM;
    }

    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, DEFAULT_CURL_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, DEFAULT_CURL_CONNECT_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, etcdlib_writeMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, replyData);
    curl_easy_setopt(curl, CURLOPT_PRIVATE, replyData); // note used for testing purposes (mocking reply data)
    if (replyData->header) {
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, replyData);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, etcdlib_writeHeaderCallback);
    }

    if (request == PUT) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestData);
    } else if (request == DELETE) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else if (request == GET) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
    }

    etcdlib_status_t rc = ETCDLIB_RC_OK;
    if (lib->curlMulti) {
        CURLMcode mc = curl_multi_add_handle(lib->curlMulti, curl);
        curl_multi_strerror(mc);
        if (mc != CURLM_OK) {
            rc = ETCDLIB_INTERNAL_CURLMCODE_FLAG | mc;
        } else {
            int running;
            mc = curl_multi_perform(lib->curlMulti, &running);
            if (mc == CURLM_OK) {
                rc = etcdlib_waitForMultiCurl(lib, curl);
            } else {
                rc = ETCDLIB_INTERNAL_CURLMCODE_FLAG | mc;
            }
        }
    } else {
        CURLcode code = curl_easy_perform(curl);
        if (code != CURLE_OK) {
            rc = ETCDLIB_INTERNAL_CURLCODE_FLAG | code;
        }
    }
    *curlOut = curl;
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

static etcdlib_status_t etcdlib_waitForMultiCurl(etcdlib_t* lib, CURL* curl) {
    etcdlib_status_t rc = ETCDLIB_RC_OK;
    CURLcode code = CURLE_OK;
    int nrOfHandlesRunning;
    bool etcdlibRunning;
    do {
        etcdlib_completed_curl_entry_t entry = etcdlib_removeCompletedCurlEntry(lib, curl);
        if (entry.curl) {
            code = entry.res;
            break;
        }
        CURLMsg* msg = curl_multi_info_read(lib->curlMulti, &nrOfHandlesRunning);
        if (msg && (msg->msg == CURLMSG_DONE) && (msg->easy_handle == curl)) {
            curl_multi_remove_handle(lib->curlMulti, curl); //TODO handle return code
            code = msg->data.result;
            break;
        } else if (msg && (msg->msg == CURLMSG_DONE)) {
            //found another, non-matching, completed curl, add it to the completedCurlEntries
            curl_multi_remove_handle(lib->curlMulti, msg->easy_handle); //TODO handle return code
            rc = etcdlib_addCompletedCurlEntry(lib, msg->easy_handle, msg->data.result);
            while (rc == ETCDLIB_INTERNAL_RC_MAX_ENTRIES_REACHED) {
                usleep(1000); //wait a bit and try again
                rc = etcdlib_addCompletedCurlEntry(lib, msg->easy_handle, msg->data.result);
            }
        } else {
            //no completed curl found, wait for more events
            curl_multi_poll(lib->curlMulti, NULL, 0, 1000, NULL); //TODO handle return code
        }
        etcdlibRunning = __atomic_load_n(&lib->curlMultiRunning, __ATOMIC_ACQUIRE);
    } while (nrOfHandlesRunning > 0 && etcdlibRunning && rc == ETCDLIB_RC_OK);

    if (rc == ETCDLIB_RC_OK && code != CURLE_OK) {
        rc = ETCDLIB_INTERNAL_CURLCODE_FLAG | code;
    }
    return rc;
}
