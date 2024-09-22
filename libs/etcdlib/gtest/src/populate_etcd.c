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
#include <stdarg.h>

static void logMsg(void *data __attribute__((unused)), const char * fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
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

    //create a permanent key
    (void)etcdlib_set(etcdlib, "/persistant/key", "value", 0);

    //create a permanent dir with 2 entries
    (void)etcdlib_createDir(etcdlib, "/persistant/dir", 0);
    (void)etcdlib_set(etcdlib, "/persistant/dir/key1", "value1", 0);
    (void)etcdlib_set(etcdlib, "/persistant/dir/key2", "value2", 0);

    //create a key with a ttl of 10 seconds
    (void)etcdlib_set(etcdlib, "/temp/key", "value", 10);

    //create a dir with a ttl of 10 seconds
    (void)etcdlib_createDir(etcdlib, "/temp/dir", 10);
    (void)etcdlib_set(etcdlib, "/temp/dir/key1", "value3", 0);
    (void)etcdlib_set(etcdlib, "/temp/dir/key2", "value4", 0);
    (void)etcdlib_set(etcdlib, "/temp/dir/key2&with?escapable&url", "and&with;Escapable?value", 0);

    printf("etcdlib populate done. looping to refresh watches\n");


    int count = 0;
    char buf[128];
    while (1) {
        etcdlib_refresh(etcdlib, "/temp/key", 10);
        etcdlib_refreshDir(etcdlib, "/temp/dir", 10);
        count++;
        if (count % 30 == 0) {
                printf("etcdlib updating values\n");
                snprintf(buf, sizeof(buf), "value-%d", count);
                etcdlib_set(etcdlib, "/temp/key", buf, 10);
                etcdlib_set(etcdlib, "/temp/dir/key1", buf, 0);
                etcdlib_set(etcdlib, "/temp/dir/key2", buf, 0);
        }
        sleep(1);
    }
}