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

#include "celix_api.h"
#include <stdio.h>
#include <assert.h>

void foo();
const char* get_bundle_name();

void foo() {
    printf("nop\n");
}

const char* get_bundle_name() {
    celix_bundle_context_t* ctx = celix_getBundleContext();
    if (ctx) {
        celix_bundle_t *bnd = celix_bundleContext_getBundle(ctx);
        return celix_bundle_getSymbolicName(bnd);
    }
    return NULL;
}
