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

#include "celix_bundle_cache.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/errno.h>

#include "celix_constants.h"
#include "celix_log.h"
#include "celix_properties.h"
#include "celix_file_utils.h"
#include "celix_utils.h"
#include "celix_bundle_context.h"
#include "framework_private.h"
#include "bundle_archive_private.h"
#include "celix_convert_utils.h"

//for Celix 3.0 update to a different bundle root scheme
//#define CELIX_BUNDLE_ARCHIVE_ROOT_FORMAT "%s/bundle_%li"

#define CELIX_BUNDLE_ARCHIVE_ROOT_FORMAT "%s/bundle%li"

#define FW_LOG(level, ...) \
    celix_framework_log(cache->fw->logger, (level), __FUNCTION__ , __FILE__, __LINE__, __VA_ARGS__)

struct celix_bundle_cache {
    celix_framework_t* fw;
    char* cacheDir;
    bool deleteOnDestroy;
};

static const char* bundleCache_progamName() {
#if defined(__APPLE__) || defined(__FreeBSD__)
	return getprogname();
#elif defined(_GNU_SOURCE)
	return program_invocation_short_name;
#else
	return "";
#endif
}

celix_status_t celix_bundleCache_create(celix_framework_t* fw, celix_bundle_cache_t **out) {
    celix_status_t status = CELIX_SUCCESS;

    const char* cacheDir = NULL;
    const char* useTmpDirStr = NULL;
    status = fw_getProperty(fw, OSGI_FRAMEWORK_FRAMEWORK_STORAGE, ".cache", &cacheDir);
    status = CELIX_DO_IF(status, fw_getProperty(fw, OSGI_FRAMEWORK_STORAGE_USE_TMP_DIR, "false", &useTmpDirStr));
    bool useTmpDir = celix_utils_convertStringToBool(useTmpDirStr, false, NULL);
    if (cacheDir == NULL) {
        cacheDir = ".cache";
    }

    celix_bundle_cache_t *cache = calloc(1, sizeof(*cache));
    if (!cache) {
        status = CELIX_ENOMEM;
        fw_logCode(fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Cannot create bundle cache, out of memory");
        return status;
    }

    if (useTmpDir) {
        //Using /tmp dir for cache, so that multiple frameworks can be launched
        //instead of cacheDir = ".cache";
        const char *pg = bundleCache_progamName();
        if (pg == NULL) {
            pg = "";
        }

        asprintf(&cache->cacheDir, "/tmp/celix-cache-%s-%s", pg, celix_framework_getUUID(fw));
        cache->deleteOnDestroy = true; //TODO update and make configurable
    } else {
        cache->cacheDir = celix_utils_strdup(cacheDir);
        cache->deleteOnDestroy = false; //TODO update and make configurable
    }

    *out = cache;
	return CELIX_SUCCESS;
}

celix_status_t celix_bundleCache_destroy(celix_bundle_cache_t* cache) {
    celix_status_t status = CELIX_SUCCESS;
	if (cache->deleteOnDestroy) {
        status = celix_bundleCache_delete(cache);
	}
	free(cache->cacheDir);
	free(cache);
	return status;
}

celix_status_t celix_bundleCache_delete(celix_bundle_cache_t* cache) {
    const char* err = NULL;
    celix_status_t status = celix_utils_deleteDirectory(cache->cacheDir, &err);
    if (err != NULL) {
        FW_LOG(CELIX_LOG_LEVEL_ERROR, "Cannot delete cache dir at %s: %s", cache->cacheDir, err);
    }
    return status;
}

celix_status_t celix_bundleCache_getArchives(celix_bundle_cache_t* cache, celix_array_list_t **archives) {
	celix_status_t status = CELIX_SUCCESS;

	DIR *dir;
	struct stat st;

	dir = opendir(cache->cacheDir);

	if (dir == NULL && errno == ENOENT) {
		if(mkdir(cache->cacheDir, S_IRWXU) == 0 ){
			dir = opendir(cache->cacheDir);
		}
	}

    char archiveRootBuffer[512];
	if (dir != NULL) {
		celix_array_list_t *list = celix_arrayList_create();
		struct dirent* dent = NULL;
		errno = 0;
		dent = readdir(dir);
		while (errno == 0 && dent != NULL) {
            char* archiveRoot = celix_utils_writeOrCreateString(archiveRootBuffer, sizeof(archiveRootBuffer), "%s/%s", cache->cacheDir, dent->d_name);
			if (stat (archiveRoot, &st) == 0 &&
                        S_ISDIR (st.st_mode)
						&& (strcmp((dent->d_name), ".") != 0)
						&& (strcmp((dent->d_name), "..") != 0)
						&& (strncmp(dent->d_name, "bundle", 6) == 0)) {
                bundle_archive_pt archive = NULL;
                status = bundleArchive_recreate(cache->fw, archiveRoot, &archive);
                if (status == CELIX_SUCCESS) {
                    arrayList_add(list, archive);
                } else {
                    FW_LOG(CELIX_LOG_LEVEL_ERROR, "Cannot recreate bundle archive from %s", archiveRoot);
                }
                celix_utils_freeStringIfNeeded(archiveRootBuffer, archiveRoot);
            }
			errno = 0;
			dent = readdir(dir);
		}

		if (errno != 0) {
			fw_log(celix_frameworkLogger_globalLogger(), CELIX_LOG_LEVEL_ERROR, "Error reading dir");
			status = CELIX_FILE_IO_EXCEPTION;
		} else {
			status = CELIX_SUCCESS;
		}

		closedir(dir);

		if (status == CELIX_SUCCESS) {
			*archives = list;
		}
		else{
			int idx = 0;
			for(;idx<arrayList_size(list);idx++){
				bundleArchive_destroy((bundle_archive_pt)arrayList_get(list,idx));
			}
			arrayList_destroy(list);
			*archives = NULL;
		}

	} else {
		status = CELIX_FILE_IO_EXCEPTION;
	}

	framework_logIfError(celix_frameworkLogger_globalLogger(), status, NULL, "Failed to get bundle archives");
	if (status != CELIX_SUCCESS) {
		perror("\t");
	}

	return status;
}

celix_status_t celix_bundleCache_createArchive(celix_framework_t* fw, long id, const char *location, const char *inputFile __attribute__((unused)), bundle_archive_pt *archive) {
	celix_status_t status = CELIX_SUCCESS;
    char archiveRootBuffer[512];
    char *archiveRoot = celix_utils_writeOrCreateString(archiveRootBuffer, sizeof(archiveRootBuffer), CELIX_BUNDLE_ARCHIVE_ROOT_FORMAT, fw->cache->cacheDir, id);
    if (archiveRoot) {
		status = bundleArchive_create(fw, archiveRoot, id, location, inputFile, archive);
	} else {
        status = CELIX_ENOMEM;
    }
    celix_utils_freeStringIfNeeded(archiveRootBuffer, archiveRoot);
	framework_logIfError(fw->logger, status, NULL, "Failed to create archive.");
	return status;
}

celix_status_t celix_bundleCache_createSystemArchive(celix_framework_t* fw, bundle_archive_pt* archive) {
    return celix_bundleCache_createArchive(fw, CELIX_FRAMEWORK_BUNDLE_ID, NULL, NULL, archive);
}
