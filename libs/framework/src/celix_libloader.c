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

#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <string.h>

#include "celix_constants.h"
#include "celix_libloader.h"
#include "hash_map.h"
#include "celix_utils.h"
#include "utils.h"

static pthread_mutex_t handleLookupTableMutex = PTHREAD_MUTEX_INITIALIZER;
static hash_map_t* handleLookupTable = NULL;

static void celix_libloader_addHandleToLookup(void *handle) {
    if (handle == NULL) {
        return;
    }
    struct link_map* lmap = NULL;
    int rc = dlinfo(handle, RTLD_DI_LINKMAP, &lmap);
    if (rc == 0 && lmap != NULL) {
        pthread_mutex_lock(&handleLookupTableMutex);
        if (handleLookupTable == NULL) {
            handleLookupTable = hashMap_create(utils_stringHash, utils_stringHash, utils_stringEquals, utils_stringEquals);
        }
        char* name = celix_utils_strdup(lmap->l_name);
        printf("Storing handle for bundle activator %s\n", name);
        hashMap_put(handleLookupTable, name, handle);
        pthread_mutex_unlock(&handleLookupTableMutex);
    } else {
        fprintf(stderr, "Cannot find bundle activator library name from dlinfo\n");
    }
}

static void celix_libloader_removeHandleFromLookup(void *handle) {
    if (handle == NULL) {
        return;
    }
    struct link_map* lmap = NULL;
    int rc = dlinfo(handle, RTLD_DI_LINKMAP, &lmap);
    const char* name =  NULL;
    if (rc == 0 && lmap != NULL) {
        name = lmap->l_name;
    } else {
        fprintf(stderr, "Cannot find bundle activator library name from dlinfo\n");
    }

    if (name != NULL) {
        pthread_mutex_lock(&handleLookupTableMutex);
        hashMap_removeFreeKey(handleLookupTable, name);
        if (hashMap_size(handleLookupTable) == 0) {
            hashMap_destroy(handleLookupTable, false, false);
            handleLookupTable = NULL;
        }
        pthread_mutex_unlock(&handleLookupTableMutex);
    }
}

//static Lmid_t celix_libloader_createNewNamespace(int flags) {
//    Lmid_t namespace = LM_ID_NEWLM;
//    struct link_map* lmap = NULL;
//    void* exec = dlopen(NULL, RTLD_LAZY);
//    dlinfo(exec, RTLD_DI_LINKMAP, &lmap);
//    while (lmap != NULL) {
//        printf("Found %s\n", lmap->l_name);
//        if (lmap->l_name != NULL && strstr(lmap->l_name, "lib") != NULL) {
//            void *handle = dlmopen(namespace, lmap->l_name, flags);
//            dlinfo(handle, RTLD_DI_LMID, &namespace);
//        }
//        lmap = lmap->l_next;
//    }
//    return namespace;
//}


celix_library_handle_t* celix_libloader_open(celix_bundle_context_t *ctx, const char *libPath) {
#if defined(NDEBUG)
    bool defaultNoDelete = false;
#else
    bool defaultNoDelete = true;
#endif
    celix_library_handle_t* handle = NULL;
    bool noDelete = celix_bundleContext_getPropertyAsBool(ctx, CELIX_LOAD_BUNDLES_WITH_NODELETE, defaultNoDelete);
    int flags = RTLD_LAZY|RTLD_LOCAL;
    if (noDelete) {
        flags = RTLD_LAZY|RTLD_LOCAL|RTLD_NODELETE;
    }
    //Lmid_t namepace = celix_libloader_createNewNamespace(flags);
    //handle = dlmopen(namepace, libPath, flags);
    handle = dlopen(libPath, flags);
    celix_libloader_addHandleToLookup(handle);
    return handle;
}


void celix_libloader_close(celix_library_handle_t *handle) {
    celix_libloader_removeHandleFromLookup(handle);
    dlclose(handle);
}

void* celix_libloader_getSymbol(celix_library_handle_t *handle, const char *name) {
    return dlsym(handle, name);
}

const char* celix_libloader_getLastError() {
    return dlerror();
}

static void* celix_libloader_findHandleForName(const char* name) {
    void* handle = NULL;
    pthread_mutex_lock(&handleLookupTableMutex);
    if (handleLookupTable != NULL) {
        handle = hashMap_get(handleLookupTable, name);
    }
    pthread_mutex_unlock(&handleLookupTableMutex);
    return handle;
}

void* celix_libloader_findBundleActivatorSymbolFromAddr(void *addr, const char* symbol) {
    void* foundSymbol = NULL;
    struct link_map* lmap = NULL;
    Dl_info info;
    int rc = dladdr1(addr, &info, (void **) &lmap, RTLD_DL_LINKMAP); //note that for dladdr a non-zero return is success
    if (rc != 0) {
        if (lmap != NULL) {
            void* handle = celix_libloader_findHandleForName(lmap->l_name);
            if (handle != NULL) {
                foundSymbol = dlsym(handle, symbol);
            }
            while (foundSymbol == NULL && lmap->l_prev != NULL) {
                lmap = lmap->l_prev;
                handle = celix_libloader_findHandleForName(lmap->l_name);
                if (handle != NULL) {
                    foundSymbol = dlsym(handle, symbol);
                }
            }
        }
    } else {
        fprintf(stderr,"Could not find shared library for addr %p\n", addr);
    }
    return foundSymbol;
}