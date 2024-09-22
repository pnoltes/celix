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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>

static void logMsg(void *data __attribute__((unused)), const char * fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

static void logInvalidResponseReply(void *data __attribute__((unused)), const char *reply) {
    fprintf(stderr, "Invalid response: %s\n", reply);
}

static void logHttpCalls(void *data __attribute__((unused)), const char* url, const char* method, const char* reqData, const char* replyData) {
    printf("HTTP call: %s %s.", method, url);
    if (reqData != NULL) {
            printf(" Request data: %s.", reqData);
    }
    if (replyData != NULL) {
            printf(" Reply data: %s.", replyData);
    }
    printf("\n");
}

void printDirEntries(void* data, const char* key, const char* value) {
    const char* dir = (const char*)data;
    printf("Value for key %s in dir %s is %s\n", key, dir, value);
}


int main() {
    etcdlib_create_options_t opts = ETCDLIB_EMPTY_CREATE_OPTIONS;
    opts.logErrorMessageCallback = logMsg;
    opts.logInvalidResponseReplyCallback = logInvalidResponseReply;
    opts.logHttpCallsCallback = logHttpCalls;
    etcdlib_autoptr_t etcdlib = NULL;
    etcdlib_status_t rc = etcdlib_createWithOptions(&opts, &etcdlib);

    if (rc != ETCDLIB_RC_OK) {
        return rc;
    }

    char *value;

    //get non existing key
    rc = etcdlib_get(etcdlib, "/non-existing/key", &value, 0);
    printf("etcdlib_get on non-existing key: %s\n", etcdlib_strerror(rc));

    //get non existing dir
    rc = etcdlib_getDir(etcdlib, "/non-existing/dir", NULL, NULL, 0);
    printf("etcdlib_getDir on non-existing dir: %s\n", etcdlib_strerror(rc));


    int count = 0;
    while (1) {
        //get permanent entries
        long index;
        (void)etcdlib_get(etcdlib, "/permanent/key", &value, &index);
        printf("For /permanent/key got value %s and index %ld\n", value, index);
        free(value);
        etcdlib_getDir(etcdlib, "/permanent/dir", printDirEntries, (void*)"/permanent/dir", &index);

        if (count++ % 2 == 0) {
            //watch tmp key
            (void)etcdlib_get(etcdlib, "/temp/key", &value, &index);
            if (rc != ETCDLIB_RC_OK) {
                printf("etcdlib_get on /temp/key failed with %s\n", etcdlib_strerror(rc));
                sleep(5);
                continue;
            }
            printf("Get for /temp/key got value %s and index %ld\n", value, index);
            free(value);
            printf("\nWatching /temp/key ...");
            fflush(stdout);
            (void)etcdlib_watch(etcdlib, "/temp/key", index+1, NULL, &value, NULL, &index);
            printf(".. done\n");
            printf("Watch for /temp/key got value %s and index %ld\n", value, index);
            free(value);
        } else {
            //watch tmp dir
            rc = etcdlib_getDir(etcdlib, "/temp/dir", printDirEntries, (void*)"/tmp/dir", &index);
            if (rc != ETCDLIB_RC_OK) {
                printf("etcdlib_getDir on /temp/dir failed with %s\n", etcdlib_strerror(rc));
                sleep(5);
                continue;
            }
            bool isDir;
            char* key;
            printf("\nWatching /temp/dir ...");
            fflush(stdout);
            etcdlib_watchDir(etcdlib, "/temp/dir", index+1, NULL, &key, &value, NULL, &isDir, &index);
            printf(".. done\n");
            printf("Watch for /temp/dir got key %s value %s, index %ld and isDir %s\n", key, value, index, isDir ? "true" : "false");
            free(key);
            free(value);
        }
        sleep(1);
    }

    etcdlib_destroy(etcdlib);
}