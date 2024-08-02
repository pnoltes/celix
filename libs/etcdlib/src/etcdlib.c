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
#include <curl/curl.h>
#include <jansson.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ETCD_JSON_NODE                  "node"
#define ETCD_JSON_PREVNODE              "prevNode"
#define ETCD_JSON_NODES                 "nodes"
#define ETCD_JSON_ACTION                "action"
#define ETCD_JSON_KEY                   "key"
#define ETCD_JSON_VALUE                 "value"
#define ETCD_JSON_DIR                   "dir"
#define ETCD_JSON_MODIFIEDINDEX         "modifiedIndex"
#define ETCD_JSON_INDEX                 "index"
#define ETCD_JSON_ERRORCODE                "errorCode"

#define ETCD_HEADER_INDEX               "X-Etcd-Index: "

#define MAX_OVERHEAD_LENGTH           64
#define DEFAULT_CURL_TIMEOUT          10
#define DEFAULT_CURL_CONNECT_TIMEOUT  10

#define ETCDLIB_MAX_COMPLETED_CURL_ENTRIES 16

#define ETCDLIB_OK_RESULT {ETCDLIB_RC_OK, NULL};

typedef struct /*anon*/ {
    CURL* curl;
    CURLcode res;
} etcdlib_completed_curl_entry_t;

struct etcdlib {
    char *host;
    int port;

    CURLM* curlMulti;
    pthread_mutex_t curlMutex; //protects entries, only used if curlMulti is not NULL
    etcdlib_completed_curl_entry_t* completedCurlEntries; //only used if curlMulti is not NULL
    size_t completedCurlEntriesSize; //only used if curlMulti is not NULL
};

typedef enum {
    GET, PUT, DELETE
} request_t;

static int etcdlib_performRequest(
    etcdlib_t* lib, char* url, request_t request, const char* reqData, etcdlib_reply_data_t* replyData, CURL** curlOut);

static CURLcode etcdlib_waitForMultiCurl(etcdlib_t* lib, CURL* curl);

etcdlib_t *etcdlib_create(const char *server, int port, int flags) {
    etcdlib_create_options_t opts = ETCDLIB_EMPTY_CREATE_OPTIONS;
    opts.server = server;
    opts.port = port;
    opts.initializeCurl = !(flags & ETCDLIB_NO_CURL_INITIALIZATION);
    etcdlib_t* etcdlib = NULL;
    etcdlib_result_t result = etcdlib_createWithOptions(&opts, &etcdlib);
    if (result.rc != ETCDLIB_RC_OK) {
        fprintf(stderr, "Error creating etcdlib: %s\n", result.errorStr);
    }
    return etcdlib;
}

etcdlib_result_t etcdlib_createWithOptions(const etcdlib_create_options_t* options,
                                                               etcdlib_t** etcdlib) {
    assert(options != NULL);
    assert(etcdlib != NULL);
    etcdlib_result_t result = ETCDLIB_OK_RESULT;
    const char* server = options->server ? options->server : "localhost";
    int port = options->port > 0 ? options->port : 2379;

    if (options->initializeCurl) {
        curl_global_init(CURL_GLOBAL_ALL);
    }

    etcdlib_t *lib = calloc(1, sizeof(*lib));
    char* h = strndup(server, 1024 * 1024 * 10);
    etcdlib_completed_curl_entry_t *entries = calloc(ETCDLIB_MAX_COMPLETED_CURL_ENTRIES, sizeof(*entries));
    if (!lib || !h || !entries) {
        result.rc = ETCDLIB_RC_ENOMEM;
        result.errorStr = "ENOMEM";
        return result;
    }
    lib->host = h;
    lib->port = port;
    lib->completedCurlEntries = entries;
    lib->completedCurlEntriesSize = ETCDLIB_MAX_COMPLETED_CURL_ENTRIES;

    if (options->useMultiCurl) {
        lib->completedCurlEntries = calloc(ETCDLIB_MAX_COMPLETED_CURL_ENTRIES, sizeof(*lib->completedCurlEntries));
        lib->completedCurlEntriesSize = ETCDLIB_MAX_COMPLETED_CURL_ENTRIES;
        if (!lib->completedCurlEntries) {
            result.rc = ETCDLIB_RC_ENOMEM;
            result.errorStr = "ENOMEM";
            free(lib->host);
            free(lib);
            return result;
        }

        lib->curlMulti = curl_multi_init();
        if (!lib->curlMulti) {
            result.errorStr = "Failed to create CURL multi handle";
            result.rc = ETCDLIB_RC_ERROR;
            free(lib->host);
            free(lib->completedCurlEntries);
            free(lib);
            return result;
        }

        CURLMcode code1 = curl_multi_setopt(lib->curlMulti, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
        if (code1 != CURLM_OK) {
            result.rc = ETCDLIB_RC_ERROR;
            result.errorStr = "Failed to set CURL multi options";
            curl_multi_cleanup(lib->curlMulti);
            free(lib->host);
            free(lib->completedCurlEntries);
            free(lib);
            return result;
        }

        int rc = pthread_mutex_init(&lib->curlMutex, NULL);
        if (rc != 0) {
            result.rc = ETCDLIB_RC_ERROR;
            result.errorStr = "Failed to create CURL mutex";
            curl_multi_cleanup(lib->curlMulti);
            free(lib->host);
            free(lib->completedCurlEntries);
            free(lib);
            return result;
        }
    }

    *etcdlib = lib;
    return result;
}

void etcdlib_destroy(etcdlib_t *etcdlib) {
    if (etcdlib) {
        free(etcdlib->host);

        if (etcdlib->curlMulti) {
            curl_multi_cleanup(etcdlib->curlMulti);
            pthread_mutex_destroy(&etcdlib->curlMutex);
            free(etcdlib->completedCurlEntries);
        }

        free(etcdlib);
    }
}

void etcdlib_cleanup(etcdlib_t** etcdlib) {
    if (etcdlib) {
        etcdlib_destroy(*etcdlib);
    }
}

const char* etcdlib_host(etcdlib_t* etcdlib) {
    return etcdlib->host;
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

int etcdlib_get(etcdlib_t *etcdlib, const char *key, char **value, long long *index) {
    json_t *js_root = NULL;
    json_t *js_node = NULL;
    json_t *js_value = NULL;
    json_error_t error;
    int res = -1;
    etcdlib_reply_data_t reply;

    reply.memory = malloc(1); /* will be grown as needed by the realloc above */
    reply.memorySize = 0; /* no data at this point */
    reply.header = malloc(1); /* will be grown as needed by the realloc above */
    reply.headerSize = 0; /* no data at this point */

    int retVal = ETCDLIB_RC_ERROR;
    char *url;
    asprintf(&url, "http://%s:%d/v2/keys/%s", etcdlib->host, etcdlib->port, key);
    if (!url) {
        return ETCDLIB_RC_ENOMEM;
    }

    CURL* curl;
    res = etcdlib_performRequest(etcdlib, url, GET, NULL, (void*)&reply, &curl);
    free(url);

    if (res == CURLE_OK) {
        js_root = json_loads(reply.memory, 0, &error);

        if (js_root != NULL) {
            js_node = json_object_get(js_root, ETCD_JSON_NODE);
        }
        if (js_node != NULL) {
            js_value = json_object_get(js_node, ETCD_JSON_VALUE);
            if (js_value != NULL) {
                if (index) {
                    *index = etcd_get_current_index(reply.header);
                }
                *value = strdup(json_string_value(js_value));
                retVal = ETCDLIB_RC_OK;
            }
        }
        if (js_root != NULL) {
            json_decref(js_root);
        }
    } else if (res == CURLE_OPERATION_TIMEDOUT) {
        //timeout
        retVal = ETCDLIB_RC_TIMEOUT;
    } else {
        retVal = ETCDLIB_RC_ERROR;
        fprintf(stderr, "Error getting etcd value, curl error: '%s'\n", curl_easy_strerror(res));
    }
    curl_easy_cleanup(curl);

    if (reply.memory) {
        free(reply.memory);
    }
    if (retVal != ETCDLIB_RC_OK) {
        *value = NULL;
    }
    return retVal;
}

static int
etcd_get_recursive_values(json_t *js_root, etcdlib_key_value_callback callback, void *arg, json_int_t *mod_index) {
    json_t *js_nodes;
    if ((js_nodes = json_object_get(js_root, ETCD_JSON_NODES)) != NULL) {
        // subarray
        if (json_is_array(js_nodes)) {
            int len = json_array_size(js_nodes);
            for (int i = 0; i < len; i++) {
                json_t *js_object = json_array_get(js_nodes, i);
                json_t *js_mod_index = json_object_get(js_object, ETCD_JSON_MODIFIEDINDEX);

                if (js_mod_index != NULL) {
                    json_int_t index = json_integer_value(js_mod_index);
                    if (*mod_index < index) {
                        *mod_index = index;
                    }
                } else {
                    printf("[ETCDLIB] Error: No INDEX found for key!\n");
                }

                if (json_object_get(js_object, ETCD_JSON_NODES)) {
                    // node contains nodes
                    etcd_get_recursive_values(js_object, callback, arg, mod_index);
                } else {
                    json_t *js_key = json_object_get(js_object, ETCD_JSON_KEY);
                    json_t *js_value = json_object_get(js_object, ETCD_JSON_VALUE);

                    if (js_key && js_value) {
                        if (!json_object_get(js_object, ETCD_JSON_DIR)) {
                            callback(json_string_value(js_key), json_string_value(js_value), arg);
                        }
                    } //else empty etcd directory, not an error.

                }
            }
        } else {
            fprintf(stderr, "[ETCDLIB] Error: misformatted JSON: nodes element is not an array !!\n");
        }
    } else {
        fprintf(stderr, "[ETCDLIB] Error: nodes element not found!!\n");
    }

    return (*mod_index > 0 ? 0 : 1);
}

int etcdlib_get_directory(etcdlib_t *etcdlib, const char *directory, etcdlib_key_value_callback callback, void *arg,
                          long long *modifiedIndex) {

    json_t *js_root = NULL;
    json_t *js_rootnode = NULL;
    json_t *js_index = NULL;

    json_error_t error;
    int res;
    etcdlib_reply_data_t reply;

    reply.memory = malloc(1); /* will be grown as needed by the realloc above */
    reply.memorySize = 0; /* no data at this point */
    reply.header = malloc(1); /* will be grown as needed by the realloc above */
    reply.headerSize = 0; /* no data at this point */

    int retVal = ETCDLIB_RC_OK;
    char *url;

    asprintf(&url, "http://%s:%d/v2/keys/%s?recursive=true", etcdlib->host, etcdlib->port, directory);
    if (!url) {
        return ETCDLIB_RC_ENOMEM;
    }

    CURL* curl = NULL;
    res = etcdlib_performRequest(etcdlib->curlMulti, url, GET, NULL, (void *) &reply, &curl);
    free(url);
    if (res == CURLE_OK) {
        js_root = json_loads(reply.memory, 0, &error);
        if (js_root != NULL) {
            js_rootnode = json_object_get(js_root, ETCD_JSON_NODE);
            js_index = json_object_get(js_root, ETCD_JSON_INDEX);
        } else {
            retVal = ETCDLIB_RC_ERROR;
            fprintf(stderr, "[ETCDLIB] Error: %s in js_root not found\n", ETCD_JSON_NODE);
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
            retVal = etcd_get_recursive_values(js_rootnode, callback, arg, (json_int_t *) ptrModIndex);
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
    } else if (res == CURLE_OPERATION_TIMEDOUT) {
        retVal = ETCDLIB_RC_TIMEOUT;
    } else {
        retVal = ETCDLIB_RC_ERROR;
    }
    curl_easy_cleanup(curl);

    free(reply.memory);
    free(reply.header);

    return retVal;
}

int etcdlib_set(etcdlib_t *etcdlib, const char *key, const char *value, int ttl, bool prevExist) {

    json_error_t error;
    json_t *js_root = NULL;
    json_t *js_node = NULL;
    json_t *js_value = NULL;
    int retVal = ETCDLIB_RC_ERROR;
    char *url;
    size_t req_len = strlen(value) + MAX_OVERHEAD_LENGTH;
    char request[req_len];
    char *requestPtr = request;
    int res;
    etcdlib_reply_data_t reply;

    /* Skip leading '/', etcd cannot handle this. */
    while (*key == '/') {
        key++;
    }

    reply.memory = calloc(1, 1); /* will be grown as needed by the realloc above */
    reply.memorySize = 0; /* no data at this point */
    reply.header = NULL; /* will be grown as needed by the realloc above */
    reply.headerSize = 0; /* no data at this point */

    asprintf(&url, "http://%s:%d/v2/keys/%s", etcdlib->host, etcdlib->port, key);
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
    res = etcdlib_performRequest(etcdlib->curlMulti, url, PUT, request, (void *) &reply, &curl);
    free(url);

    if (res == CURLE_OK) {
        js_root = json_loads(reply.memory, 0, &error);

        if (js_root != NULL) {
            js_node = json_object_get(js_root, ETCD_JSON_NODE);
        }
        if (js_node != NULL) {
            js_value = json_object_get(js_node, ETCD_JSON_VALUE);
        }
        if (js_value != NULL && json_is_string(js_value)) {
            if (strcmp(json_string_value(js_value), value) == 0) {
                retVal = ETCDLIB_RC_OK;
            }
        }
        if (js_root != NULL) {
            json_decref(js_root);
        }
    } else {
        retVal = ETCDLIB_RC_ERROR;
        fprintf(stderr, "[ETCDLIB] Error: %s\n", curl_easy_strerror(res));
    }
    curl_easy_cleanup(curl);

    if (reply.memory) {
        free(reply.memory);
    }

    return retVal;
}

int etcdlib_refresh(etcdlib_t *etcdlib, const char *key, int ttl) {
    int retVal = ETCDLIB_RC_ERROR;
    char *url;
    size_t req_len = MAX_OVERHEAD_LENGTH;
    char request[req_len];

    int res;
    etcdlib_reply_data_t reply;

    /* Skip leading '/', etcd cannot handle this. */
    while (*key == '/') {
        key++;
    }

    reply.memory = calloc(1, 1); /* will be grown as needed by the realloc above */
    reply.memorySize = 0; /* no data at this point */
    reply.header = NULL; /* will be grown as needed by the realloc above */
    reply.headerSize = 0; /* no data at this point */

    asprintf(&url, "http://%s:%d/v2/keys/%s", etcdlib->host, etcdlib->port, key);
    if (!url) {
        return ETCDLIB_RC_ENOMEM;
    }
    snprintf(request, req_len, "ttl=%d;prevExists=true;refresh=true", ttl);

    CURL* curl = NULL;
    res = etcdlib_performRequest(etcdlib, url, PUT, request, (void *) &reply, &curl);
    free(url);


    if (res == CURLE_OK && reply.memory != NULL) {
        json_error_t error;
        json_t *root = json_loads(reply.memory, 0, &error);
        if (root != NULL) {
            json_t *errorCode = json_object_get(root, ETCD_JSON_ERRORCODE);
            if (errorCode == NULL) {
                //no curl error and no etcd errorcode reply -> OK
                retVal = ETCDLIB_RC_OK;
            } else {
                fprintf(stderr, "[ETCDLIB] errorcode %lli\n", json_integer_value(errorCode));
                retVal = ETCDLIB_RC_ERROR;
            }
            json_decref(root);
        } else {
            retVal = ETCDLIB_RC_ERROR;
            fprintf(stderr, "[ETCDLIB] Error: %s is not json\n", reply.memory);
        }
    } else {
        retVal = ETCDLIB_RC_ERROR;
        fprintf(stderr, "[ETCDLIB] Error: %s\n", curl_easy_strerror(res));
    }
    curl_easy_cleanup(curl);

    if (reply.memory) {
        free(reply.memory);
    }

    return retVal;
}

int etcdlib_set_with_check(etcdlib_t *etcdlib, const char *key, const char *value, int ttl, bool always_write) {
    char *etcd_value;
    int result = 0;
    if (etcdlib_get(etcdlib, key, &etcd_value, NULL) == 0) {
        if (etcd_value != NULL) {
            if (strcmp(etcd_value, value) != 0) {
                fprintf(stderr, "[ETCDLIB] WARNING: value already exists and is different\n");
                fprintf(stderr, "   key       = %s\n", key);
                fprintf(stderr, "   old value = %s\n", etcd_value);
                fprintf(stderr, "   new value = %s\n", value);
                result = -1;
            }
            free(etcd_value);
        }
    }
    if (always_write || !result) {
        result = etcdlib_set(etcdlib, key, value, ttl, false);
    }
    return result;
}

int etcdlib_watch(etcdlib_t *etcdlib, const char *key, long long index, char **action, char **prevValue, char **value,
                  char **rkey, long long *modifiedIndex) {

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
    int retVal = ETCDLIB_RC_ERROR;
    char *url = NULL;
    int res;
    etcdlib_reply_data_t reply;

    reply.memory = malloc(1); /* will be grown as needed by the realloc above */
    reply.memorySize = 0; /* no data at this point */
    reply.header = NULL; /* will be grown as needed by the realloc above */
    reply.headerSize = 0; /* no data at this point */

    if (index != 0) {
        asprintf(&url,
                 "http://%s:%d/v2/keys/%s?wait=true&recursive=true&waitIndex=%lld",
                 etcdlib->host,
                 etcdlib->port,
                 key,
                 index);
    } else {
        asprintf(&url, "http://%s:%d/v2/keys/%s?wait=true&recursive=true", etcdlib->host, etcdlib->port, key);
    }
    if (!url) {
        return ETCDLIB_RC_ENOMEM;
    }

    CURL *curl = NULL;
    res = etcdlib_performRequest(etcdlib->curlMulti, url, GET, NULL, (void *) &reply, &curl);
    free(url);

    if (res == CURLE_OK) {
        js_root = json_loads(reply.memory, 0, &error);

        if (js_root != NULL) {
            js_action = json_object_get(js_root, ETCD_JSON_ACTION);
            js_node = json_object_get(js_root, ETCD_JSON_NODE);
            js_prevNode = json_object_get(js_root, ETCD_JSON_PREVNODE);
            js_index = json_object_get(js_root, ETCD_JSON_INDEX);
            retVal = ETCDLIB_RC_OK;
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

            *prevValue = strdup(json_string_value(js_prevValue));
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
            *rkey = strdup(json_string_value(js_rkey));

        }
        if ((action != NULL) && (js_action != NULL) && (json_is_string(js_action))) {
            *action = strdup(json_string_value(js_action));
        }
        if ((value != NULL) && (js_value != NULL) && (json_is_string(js_value))) {
            *value = strdup(json_string_value(js_value));
        }
        if (js_root != NULL) {
            json_decref(js_root);
        }

    } else if (res == CURLE_OPERATION_TIMEDOUT) {
        // ignore timeout
        retVal = ETCDLIB_RC_TIMEOUT;
    } else if (res == CURLE_AUTH_ERROR) {
        // AUTH_ERROR, 401, for etcd means that the provided watch index event is cleared and no longer available.
        // This means that a watch is no longer possible and a new etcdlib_get -> etcdlib_watch setup needs to be done
        retVal = ETCDLIB_RC_EVENT_CLEARED;
    } else {
        fprintf(stderr, "Got curl error: %s\n", curl_easy_strerror(res));
        retVal = ETCDLIB_RC_ERROR;
    }
    curl_easy_cleanup(curl);

    free(reply.memory);

    return retVal;
}

int etcdlib_del(etcdlib_t *etcdlib, const char *key) {

    json_error_t error;
    json_t *js_root = NULL;
    json_t *js_node = NULL;
    int retVal = ETCDLIB_RC_ERROR;
    char *url;
    int res;
    etcdlib_reply_data_t reply;

    reply.memory = malloc(1); /* will be grown as needed by the realloc above */
    reply.memorySize = 0; /* no data at this point */
    reply.header = NULL; /* will be grown as needed by the realloc above */
    reply.headerSize = 0; /* no data at this point */

    CURL* curl = NULL;
    asprintf(&url, "http://%s:%d/v2/keys/%s?recursive=true", etcdlib->host, etcdlib->port, key);
    if (!url) {
        return ETCDLIB_RC_ENOMEM;
    }
    res = etcdlib_performRequest(etcdlib->curlMulti, url, DELETE, NULL, (void *) &reply, &curl);
    free(url);

    if (res == CURLE_OK) {
        js_root = json_loads(reply.memory, 0, &error);
        if (js_root != NULL) {
            js_node = json_object_get(js_root, ETCD_JSON_NODE);
        }

        if (js_node != NULL) {
            retVal = ETCDLIB_RC_OK;
        }

        if (js_root != NULL) {
            json_decref(js_root);
        }
    } else {
        retVal = ETCDLIB_RC_ERROR;
        fprintf(stderr, "Error deleting etcd value, curl error: '%s'\n", curl_easy_strerror(res));
    }
    curl_easy_cleanup(curl);

    free(reply.memory);

    return retVal;
}

static size_t etcdlib_writeMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    etcdlib_reply_data_t* mem = (etcdlib_reply_data_t*)userp;

    void* newMem = realloc(mem->memory, mem->memorySize + realsize + 1);
    if (newMem == NULL) {
        fprintf(stderr, "[ETCDLIB] Error: not enough memory (realloc returned NULL)\n");
        return 0;
    }
    mem->memory = newMem;

    memcpy(&(mem->memory[mem->memorySize]), contents, realsize);
    mem->memorySize += realsize;
    mem->memory[mem->memorySize] = 0;

    return realsize;
}

static size_t etcdlib_writeHeaderCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    etcdlib_reply_data_t* mem = (etcdlib_reply_data_t*)userp;

    void* newHeader = realloc(mem->header, mem->headerSize + realsize + 1);
    if (newHeader == NULL) {
        /* out of memory! */
        fprintf(stderr, "[ETCDLIB] Error: not enough header-memory (realloc returned NULL)\n");
        return 0;
    }
    mem->header = newHeader;

    memcpy(&(mem->header[mem->headerSize]), contents, realsize);
    mem->headerSize += realsize;
    mem->header[mem->headerSize] = 0;

    return realsize;
}

static int etcdlib_performRequest(etcdlib_t* lib,
                                  char* url,
                                  request_t request,
                                  const char* requestData,
                                  etcdlib_reply_data_t* replyData,
                                  CURL** curlOut) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Cannot setup curl\n");
        return ETCDLIB_RC_ERROR;
    }

    CURLcode res = 0;

    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, DEFAULT_CURL_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, DEFAULT_CURL_CONNECT_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, etcdlib_writeMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, replyData);
    curl_easy_setopt(curl, CURLOPT_PRIVATE, replyData); //note used for testing purposes (mocking reply data)
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

    if (lib->curlMulti) {
        curl_multi_add_handle(lib->curlMulti, curl);
        int running;
        CURLMcode mc = curl_multi_perform(lib->curlMulti, &running);
        if (mc == CURLM_OK) {
            res = etcdlib_waitForMultiCurl(lib, curl);
        } else {
            fprintf(stderr, "[ETCDLIB] Error: curl_multi_perform failed: %d\n", mc);
            res = CURLE_FAILED_INIT;
        }
    } else {
        res = curl_easy_perform(curl);
    }
    *curlOut = curl;
    return res;
}

static etcdlib_completed_curl_entry_t etcdlib_removeCompletedCurlEntry(etcdlib_t* lib, CURL* curl) {
    etcdlib_completed_curl_entry_t entry = {NULL, CURLE_FAILED_INIT};
    for (int i = 0; i < lib->completedCurlEntriesSize; i++) {
        if (lib->completedCurlEntries[i].curl == curl) {
            memcpy(&entry, &lib->completedCurlEntries[i], sizeof(entry));
            lib->completedCurlEntries[i].curl = NULL;
            break;
        }
    }
    return entry;
}

static bool etcdlib_addCompletedCurlEntry(etcdlib_t* lib, CURL* curl, CURLcode res) {
    for (int i = 0; i < lib->completedCurlEntriesSize; i++) {
        if (lib->completedCurlEntries[i].curl == NULL) {
            lib->completedCurlEntries[i].curl = curl;
            lib->completedCurlEntries[i].res = res;
            return true;
        }
    }
    return false; //no free entry found

}

static CURLcode etcdlib_waitForMultiCurl(etcdlib_t* lib, CURL* curl) {
    int running;
    CURLcode res = CURLE_OPERATION_TIMEDOUT;
    pthread_mutex_lock(&lib->curlMutex);
    do {
        etcdlib_completed_curl_entry_t entry = etcdlib_removeCompletedCurlEntry(lib, curl);
        if (entry.curl) {
            res = entry.res;
            break;
        }
        CURLMsg* msg = curl_multi_info_read(lib->curlMulti, &running);
        if (msg && (msg->msg == CURLMSG_DONE) && (msg->easy_handle == curl)) {
            curl_multi_remove_handle(lib->curlMulti, curl);
            res = msg->data.result;
            break;
        } else if (msg && (msg->msg == CURLMSG_DONE)) {
            //found another, non-matching, completed curl, add it to the completedCurlEntries
            curl_multi_remove_handle(lib->curlMulti, msg->easy_handle);
            bool added;
            do {
                added = etcdlib_addCompletedCurlEntry(lib, msg->easy_handle, msg->data.result);
                if (!added) {
                    //note max completed entries storage reached -> simple delay and try again
                    pthread_mutex_unlock(&lib->curlMutex);
                    printf("[ETCDLIB] Warning: max completed entries reached, retrying after 1 second\n");
                    sleep(1);
                    pthread_mutex_lock(&lib->curlMutex);
                }
            } while (!added);
        }
    } while (running > 0);
    pthread_mutex_unlock(&lib->curlMutex);
    return res;
}
