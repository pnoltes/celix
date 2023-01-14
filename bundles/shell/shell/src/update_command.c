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


#include <stdlib.h>
#include <string.h>

#include "celix_utils.h"
#include "celix_array_list.h"
#include "celix_bundle_context.h"
#include "celix_framework.h"

bool updateCommand_execute(void *handle, const char *const_line, FILE *outStream, FILE *errStream) {
    celix_bundle_context_t *ctx = handle;

    char delims[] = " ";
    char * sub = NULL;
    char *save_ptr = NULL;

    char *line = celix_utils_strdup(const_line);

    // ignore the command
    strtok_r(line, delims, &save_ptr);
    sub = strtok_r(NULL, delims, &save_ptr);

    if (sub == NULL) {
        fprintf(errStream, "Incorrect number of arguments.\n");
    } else {
        while (sub != NULL) {
            char *endptr = NULL;
            errno = 0;
            long bndId = strtol(sub, &endptr, 10);
            if (endptr == sub || errno != 0) {
                fprintf(errStream, "Cannot convert '%s' to long (bundle id)\n", sub);
            } else {
                celix_framework_t *fw = celix_bundleContext_getFramework(ctx);
                celix_framework_updateBundleAsync(fw, bndId);
                fprintf(outStream, "Updating bundle with bunde id %li\n", bndId);
            }
            sub = strtok_r(NULL, delims, &save_ptr);
        }
    }

    free(line);

    return true;
}

