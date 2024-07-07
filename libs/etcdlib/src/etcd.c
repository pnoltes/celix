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

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <curl/curl.h>
#include <jansson.h>
#include <pthread.h>

#include "etcdlib.h"

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

struct etcdlib_struct {
    char *host;
    int port;
    //CURL *curl;
    //pthread_mutex_t mutex;

    CURLSH* curlShare; //used for get, watch, set and update. Maybe 2 share are needed
};

typedef enum {
    GET, PUT, DELETE
} request_t;

struct MemoryStruct {
    char *memory;
    char *header;
    size_t memorySize;
    size_t headerSize;
};

static int
etcdlib_performRequest(char* url, request_t request, void* reqData, void* repData, CURL** curl);

etcdlib_t *etcdlib_create(const char *server, int port, int flags) {
    if ((flags & ETCDLIB_NO_CURL_INITIALIZATION) == 0) {
        //NO_CURL_INITIALIZATION flag not set
        curl_global_init(CURL_GLOBAL_ALL);
    }

    etcdlib_t *lib = malloc(sizeof(*lib));
    char* h = strndup(server, 1024 * 1024 * 10);
    if (!lib || !h) {
        fprintf(stderr, "etcdlib: ENOMEM\n");
        return NULL;
    }
    lib->host = h;
    lib->port = port;

    lib->curlShare = curl_share_init();
    if (!lib->curlShare) {
        fprintf(stderr, "Failed to create CURLSH object\n");
        free(lib->host);
        free(lib);
        return NULL;
    }

    CURLSHcode code1 = curl_share_setopt(lib->curlShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
    CURLSHcode code2 = curl_share_setopt(lib->curlShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    CURLSHcode code3 = curl_share_setopt(lib->curlShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
    CURLSHcode code4 = curl_share_setopt(lib->curlShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);

    if (code1 != CURLSHE_OK || code2 != CURLSHE_OK || code3 != CURLSHE_OK || code4 != CURLSHE_OK ) {
        fprintf(stderr, "Failed to set CURLSH options\n");
        curl_share_cleanup(lib->curlShare);
        free(lib->host);
        free(lib);
    }

    return lib;
}

void etcdlib_destroy(etcdlib_t *etcdlib) {
    if (etcdlib) {
        free(etcdlib->host);
        curl_share_cleanup(etcdlib->curlShare);
        free(etcdlib);
    }
}

static long long etcd_get_current_index(const char *headerData) {
    long long index = -1;
    char *indexStr = strstr(headerData, ETCD_HEADER_INDEX);
    indexStr += strlen(ETCD_HEADER_INDEX);

    if (sscanf(indexStr, "%lld\n", &index) == 1) {
    } else {
        index = -1;
    }
    return index;
}

int etcdlib_get(etcdlib_t *etcdlib, const char *key, char **value, long long *index) {
    json_t *js_root = NULL;
    json_t *js_node = NULL;
    json_t *js_value = NULL;
    json_t *js_modifiedIndex = NULL;
    json_error_t error;
    int res = -1;
    struct MemoryStruct reply;

    reply.memory = malloc(1); /* will be grown as needed by the realloc above */
    reply.memorySize = 0; /* no data at this point */
    reply.header = NULL; /* will be grown as needed by the realloc above */
    reply.headerSize = 0; /* no data at this point */

    int retVal = ETCDLIB_RC_ERROR;
    char *url;
    asprintf(&url, "http://%s:%d/v2/keys/%s", etcdlib->host, etcdlib->port, key);
    CURL* curl = NULL;
    res = etcdlib_performRequest(url, GET, NULL, (void *) &reply, &curl);
    free(url);

    if (res == CURLE_OK) {
        js_root = json_loads(reply.memory, 0, &error);

        if (js_root != NULL) {
            js_node = json_object_get(js_root, ETCD_JSON_NODE);
        }
        if (js_node != NULL) {
            js_value = json_object_get(js_node, ETCD_JSON_VALUE);
            js_modifiedIndex = json_object_get(js_node, ETCD_JSON_MODIFIEDINDEX);

            if (js_modifiedIndex != NULL && js_value != NULL) {
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
        curl_easy_cleanup(curl);
    } else if (res == CURLE_OPERATION_TIMEDOUT) {
        //timeout
        retVal = ETCDLIB_RC_TIMEOUT;
    } else {
        retVal = ETCDLIB_RC_ERROR;
        fprintf(stderr, "Error getting etcd value, curl error: '%s'\n", curl_easy_strerror(res));
    }

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
    struct MemoryStruct reply;

    reply.memory = malloc(1); /* will be grown as needed by the realloc above */
    reply.memorySize = 0; /* no data at this point */
    reply.header = malloc(1); /* will be grown as needed by the realloc above */
    reply.headerSize = 0; /* no data at this point */

    int retVal = ETCDLIB_RC_OK;
    char *url;

    asprintf(&url, "http://%s:%d/v2/keys/%s?recursive=true", etcdlib->host, etcdlib->port, directory);

    CURL* curl = NULL;
    res = etcdlib_performRequest(url, GET, NULL, (void *) &reply, &curl);
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
    struct MemoryStruct reply;

    /* Skip leading '/', etcd cannot handle this. */
    while (*key == '/') {
        key++;
    }

    reply.memory = calloc(1, 1); /* will be grown as needed by the realloc above */
    reply.memorySize = 0; /* no data at this point */
    reply.header = NULL; /* will be grown as needed by the realloc above */
    reply.headerSize = 0; /* no data at this point */

    asprintf(&url, "http://%s:%d/v2/keys/%s", etcdlib->host, etcdlib->port, key);

    requestPtr += snprintf(requestPtr, req_len, "value=%s", value);
    if (ttl > 0) {
        requestPtr += snprintf(requestPtr, req_len - (requestPtr - request), ";ttl=%d", ttl);
    }

    if (prevExist) {
        requestPtr += snprintf(requestPtr, req_len - (requestPtr - request), ";prevExist=true");
    }

    CURL* curl = NULL;
    res = etcdlib_performRequest(url, PUT, request, (void *) &reply, &curl);
    if (url) {
        free(url);
    }

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
    }

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
    struct MemoryStruct reply;

    /* Skip leading '/', etcd cannot handle this. */
    while (*key == '/') {
        key++;
    }

    reply.memory = calloc(1, 1); /* will be grown as needed by the realloc above */
    reply.memorySize = 0; /* no data at this point */
    reply.header = NULL; /* will be grown as needed by the realloc above */
    reply.headerSize = 0; /* no data at this point */

    asprintf(&url, "http://%s:%d/v2/keys/%s", etcdlib->host, etcdlib->port, key);
    snprintf(request, req_len, "ttl=%d;prevExists=true;refresh=true", ttl);

    CURL* curl = NULL;
    res = etcdlib_performRequest(url, PUT, request, (void *) &reply, &curl);
    if (url) {
        free(url);
    }

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
    }

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
    struct MemoryStruct reply;

    reply.memory = malloc(1); /* will be grown as needed by the realloc above */
    reply.memorySize = 0; /* no data at this point */
    reply.header = NULL; /* will be grown as needed by the realloc above */
    reply.headerSize = 0; /* no data at this point */

    if (index != 0)
        asprintf(&url, "http://%s:%d/v2/keys/%s?wait=true&recursive=true&waitIndex=%lld", etcdlib->host, etcdlib->port,
                 key, index);
    else
        asprintf(&url, "http://%s:%d/v2/keys/%s?wait=true&recursive=true", etcdlib->host, etcdlib->port, key);

    // don't use shared curl/mutex for watch, that will lock everything.
    CURL *curl = NULL;

    res = etcdlib_performRequest(url, GET, NULL, (void *) &reply, &curl);

    curl_easy_cleanup(curl);

    if (url) {
        free(url);
    }

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
    struct MemoryStruct reply;

    reply.memory = malloc(1); /* will be grown as needed by the realloc above */
    reply.memorySize = 0; /* no data at this point */
    reply.header = NULL; /* will be grown as needed by the realloc above */
    reply.headerSize = 0; /* no data at this point */

    CURL* curl = NULL;
    asprintf(&url, "http://%s:%d/v2/keys/%s?recursive=true", etcdlib->host, etcdlib->port, key);
    res = etcdlib_performRequest(url, DELETE, NULL, (void *) &reply, &curl);
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
    }

    free(reply.memory);

    return retVal;
}

static size_t etcdlib_writeMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct* mem = (struct MemoryStruct*)userp;

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
    struct MemoryStruct* mem = (struct MemoryStruct*)userp;

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

static int etcdlib_performRequest(char* url, request_t request, void* reqData, void* repData, CURL** curl) {
    if (!curl) {
        *curl = curl_easy_init();
        if (!*curl) {
            fprintf(stderr, "Cannot setup curl\n");
            return ETCDLIB_RC_ERROR;
        }
    }

    CURLcode res = 0;

    curl_easy_setopt(*curl, CURLOPT_NOSIGNAL, 1);
    curl_easy_setopt(*curl, CURLOPT_TIMEOUT, DEFAULT_CURL_TIMEOUT);
    curl_easy_setopt(*curl, CURLOPT_CONNECTTIMEOUT, DEFAULT_CURL_CONNECT_TIMEOUT);
    curl_easy_setopt(*curl, CURLOPT_FOLLOWLOCATION, 1L);
    // curl_easy_setopt(*curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(*curl, CURLOPT_URL, url);
    curl_easy_setopt(*curl, CURLOPT_WRITEFUNCTION, etcdlib_writeMemoryCallback);
    curl_easy_setopt(*curl, CURLOPT_WRITEDATA, repData);
    if (((struct MemoryStruct*)repData)->header) {
        curl_easy_setopt(*curl, CURLOPT_HEADERDATA, repData);
        curl_easy_setopt(*curl, CURLOPT_HEADERFUNCTION, etcdlib_writeHeaderCallback);
    }

    if (request == PUT) {
        curl_easy_setopt(*curl, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(*curl, CURLOPT_POST, 1L);
        curl_easy_setopt(*curl, CURLOPT_POSTFIELDS, reqData);
    } else if (request == DELETE) {
        curl_easy_setopt(*curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else if (request == GET) {
        curl_easy_setopt(*curl, CURLOPT_CUSTOMREQUEST, "GET");
    }

    res = curl_easy_perform(*curl);

    if (res != CURLE_OK && res != CURLE_OPERATION_TIMEDOUT) {
        const char* m = request == GET ? "GET" : request == PUT ? "PUT" : request == DELETE ? "DELETE" : "?";
        fprintf(stderr, "[etclib] Curl error for %s @ %s: %s\n", url, m, curl_easy_strerror(res));

        curl_easy_cleanup(*curl);
        curl = NULL;
    }

    return res;
}
