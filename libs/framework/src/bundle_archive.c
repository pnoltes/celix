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

#include "bundle_archive_private.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include "celix_constants.h"
#include "celix_utils_api.h"
#include "linked_list_iterator.h"
#include "framework_private.h"
#include "celix_file_utils.h"
#include "bundle_revision_private.h"
#include "celix_framework_utils_private.h"

#define CELIX_BUNDLE_ARCHIVE_STATE_PROPERTIES_FILE_NAME "bundle_state.properties"

#define CELIX_BUNDLE_ARCHIVE_SYMBOLIC_NAME_PROPERTY_NAME "bundle.symbolic_name"
#define CELIX_BUNDLE_ARCHIVE_VERSION_PROPERTY_NAME "bundle.version"
#define CELIX_BUNDLE_ARCHIVE_BUNDLE_ID_PROPERTY_NAME "bundle.id"
#define CELIX_BUNDLE_ARCHIVE_LOCATION_PROPERTY_NAME "bundle.location"
#define CELIX_BUNDLE_ARCHIVE_BUNDLE_STATE_PROPERTY_NAME "bundle.state"
#define CELIX_BUNDLE_ARCHIVE_REVISION_PROPERTY_NAME "bundle.archive.revision"
#define CELIX_BUNDLE_ARCHIVE_REFRESH_COUNT_PROPERTY_NAME "bundle.archive.refresh_count"
//TODO bundle zip hash

//for Celix 3.0 update to a different revision name scheme
//#define CELIX_BUNDLE_ARCHIVE_REVISION_DIRECTORY_NAME_FORMAT "%s/revision_%li"

//The refresh count in the revision version is always 0 and not supported.
#define CELIX_BUNDLE_ARCHIVE_REVISION_DIRECTORY_NAME_FORMAT "%s/version%li.0"
#define CELIX_BUNDLE_ARCHIVE_STORE_DIRECTORY_NAME "store"

struct bundleArchive {
	celix_framework_t* fw;
	long id;
	char *archiveRoot;
    char *savedBundleStatePropertiesPath;
    char* location;
    char* storeRoot;
    bool isSystemBundle;

    celix_thread_mutex_t lock; //protects below and saving of bundle state properties

    char* currentRevisionRoot;
    celix_array_list_t* revisions; //list of bundle_revision_t*

    //bundle cache state info
    long refreshCount;
	long revisionNr;
	struct timespec lastModified;
	bundle_state_e persistentState;

    //bundle cache state in properties form
    celix_properties_t* bundleStateProperties;
};

static void celix_bundleArchive_updateAndStoreBundleStateProperties(bundle_archive_pt archive) {
    celixThreadMutex_lock(&archive->lock);
    //set/update bundle cache state properties
    celix_properties_setLong(archive->bundleStateProperties, CELIX_BUNDLE_ARCHIVE_BUNDLE_ID_PROPERTY_NAME, archive->id);
    celix_properties_set(archive->bundleStateProperties, CELIX_BUNDLE_ARCHIVE_BUNDLE_STATE_PROPERTY_NAME, celix_bundleState_getName(CELIX_BUNDLE_STATE_UNKNOWN));
    celix_properties_set(archive->bundleStateProperties, CELIX_BUNDLE_ARCHIVE_LOCATION_PROPERTY_NAME, archive->location);
    celix_properties_set(archive->bundleStateProperties, CELIX_BUNDLE_ARCHIVE_SYMBOLIC_NAME_PROPERTY_NAME, "");
    celix_properties_set(archive->bundleStateProperties, CELIX_BUNDLE_ARCHIVE_VERSION_PROPERTY_NAME, "");
    celix_properties_setLong(archive->bundleStateProperties, CELIX_BUNDLE_ARCHIVE_REVISION_PROPERTY_NAME, archive->revisionNr);
    celix_properties_setLong(archive->bundleStateProperties, CELIX_BUNDLE_ARCHIVE_REFRESH_COUNT_PROPERTY_NAME, archive->refreshCount);

    //save bundle cache state properties
    celix_properties_store(archive->bundleStateProperties, archive->savedBundleStatePropertiesPath, "Bundle State Properties");
    celixThreadMutex_unlock(&archive->lock);
}

static celix_status_t celix_bundleArchive_initialize(bundle_archive_pt archive, bool extractBundleZip, manifest_pt* manifestOut) {
    if (celix_utils_fileExists(archive->archiveRoot)) {
        fw_log(archive->fw->logger, CELIX_LOG_LEVEL_TRACE, "Bundle archive root for bundle id %li already exists.",
               archive->id);
        return CELIX_SUCCESS;
    }

    //create archive root
    const char* errorStr = NULL;
    celix_status_t status = celix_utils_createDirectory(archive->archiveRoot, false, &errorStr);
    if (status != CELIX_SUCCESS) {
        fw_log(archive->fw->logger, CELIX_LOG_LEVEL_ERROR, "Failed to initialize archive. Failed to create bundle root archive dir: %s", errorStr);
        return status;
    }

    //create store directory
    int rc = asprintf(&archive->storeRoot, "%s/%s", archive->archiveRoot, CELIX_BUNDLE_ARCHIVE_STORE_DIRECTORY_NAME);
    if (rc < 0) {
        fw_log(archive->fw->logger, CELIX_LOG_LEVEL_ERROR, "Failed to initialize archive. Failed to create bundle store dir.");
        return CELIX_ENOMEM;
    }
    status = celix_utils_createDirectory(archive->storeRoot, false, &errorStr);
    if (status != CELIX_SUCCESS) {
        fw_log(archive->fw->logger, CELIX_LOG_LEVEL_ERROR, "Failed to initialize archive. Failed to create bundle store dir: %s", errorStr);
        return status;
    }

    //create bundle revision directory
    rc = asprintf(&archive->currentRevisionRoot, CELIX_BUNDLE_ARCHIVE_REVISION_DIRECTORY_NAME_FORMAT, archive->archiveRoot, archive->revisionNr);
    if (rc < 0) {
        fw_log(archive->fw->logger, CELIX_LOG_LEVEL_ERROR, "Failed to initialize archive. Failed to create bundle revision dir.");
        return CELIX_ENOMEM;
    }
    status = celix_utils_createDirectory(archive->currentRevisionRoot, false, &errorStr);
    if (status != CELIX_SUCCESS) {
        fw_log(archive->fw->logger, CELIX_LOG_LEVEL_ERROR, "Failed to initialize archive. Failed to create bundle revision dir: %s", errorStr);
        return status;
    }

    if (extractBundleZip) {
        //extract bundle zip to revision directory
        status = celix_framework_utils_extractBundle(archive->fw, archive->location, archive->currentRevisionRoot);
        if (status != CELIX_SUCCESS) {
            fw_log(archive->fw->logger, CELIX_LOG_LEVEL_ERROR,
                   "Failed to initialize archive. Failed to extract bundle zip to revision directory.");
            return status;
        }
    }

    //read manifest from extracted bundle zip
    char pathBuffer[512];
    char* manifestPath = celix_utils_writeOrCreateString(pathBuffer, sizeof(pathBuffer), "%s/META-INF/MANIFEST.MF", archive->currentRevisionRoot);
    status = manifest_createFromFile(manifestPath, manifestOut);
    celix_utils_freeStringIfNeeded(pathBuffer, manifestPath);
    if (status != CELIX_SUCCESS) {
        fw_log(archive->fw->logger, CELIX_LOG_LEVEL_ERROR, "Failed to initialize archive. Cannot read manifest.");
        return status;
    }

    celix_bundleArchive_updateAndStoreBundleStateProperties(archive);

    return status;
}

static celix_status_t bundleArchive_createArchiveInternal(celix_framework_t* fw, const char* archiveRoot, long id, const char *location, long revisionNr, bool extractBundleZip, bundle_archive_pt* bundle_archive) {
    celix_status_t status = CELIX_SUCCESS;
    bundle_archive_pt archive = calloc(1, sizeof(*archive));

    if (archive) {
        archive->fw = fw;
        archive->id = id;
        archive->isSystemBundle = id == CELIX_FRAMEWORK_BUNDLE_ID;
        archive->revisionNr = revisionNr;
        archive->persistentState = CELIX_BUNDLE_STATE_UNKNOWN;
        archive->bundleStateProperties = celix_properties_create();
        archive->revisions = celix_arrayList_create();
        celixThreadMutex_create(&archive->lock, NULL);
    }

    if (archive == NULL || archive->bundleStateProperties == NULL || archive->revisions == NULL) {
        status = CELIX_ENOMEM;
        fw_logCode(fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Could not create archive. Out of memory.");
        bundleArchive_destroy(archive);
        return status;
    }

    int rc;
    if (!archive->isSystemBundle) {
        archive->location = celix_utils_strdup(location);
        archive->archiveRoot = celix_utils_strdup(archiveRoot);
        rc = asprintf(&archive->savedBundleStatePropertiesPath, "%s/%s", archiveRoot,
                      CELIX_BUNDLE_ARCHIVE_STATE_PROPERTIES_FILE_NAME);
        if (rc < 0 || archive->location == NULL || archive->savedBundleStatePropertiesPath == NULL
                || archive->archiveRoot == NULL) {
            status = CELIX_ENOMEM;
            fw_logCode(fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Could not create archive. Out of memory.");
            bundleArchive_destroy(archive);
            return status;
        }
    }

    manifest_pt manifest = NULL;
    if (archive->isSystemBundle) {
        status = manifest_create(&manifest);
    } else {
        status = celix_bundleArchive_initialize(archive, extractBundleZip, &manifest);
    }
    if (!manifest) {
        status = CELIX_ENOMEM;
        fw_logCode(fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Could not create archive. Failed to initialize archive or create manifest.");
        bundleArchive_destroy(archive);
        return status;
    }

    bundle_revision_t* rev = NULL;
    if (archive->isSystemBundle) {
        status = bundleRevision_create(fw, NULL /*TODO cwd?*/, NULL, 0, manifest, &rev);
    } else {
        status = bundleRevision_create(fw, archive->archiveRoot, archive->location, archive->revisionNr, manifest, &rev);
    }
    if (status != CELIX_SUCCESS) {
        fw_logCode(fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Could not create archive. Could not create bundle revision.");
        manifest_destroy(manifest);
        bundleArchive_destroy(archive);
        return status;
    } else {
        celix_arrayList_add(archive->revisions, rev);
    }

    *bundle_archive = archive;
    return status;
}

celix_status_t bundleArchive_create(celix_framework_t* fw, const char *archiveRoot, long id, const char *location, const char *inputFile __attribute__((unused)), bundle_archive_pt *bundle_archive) {
    return bundleArchive_createArchiveInternal(fw, archiveRoot, id, location, 1, true, bundle_archive);
}

celix_status_t bundleArchive_destroy(bundle_archive_pt archive) {
	if (archive != NULL) {
		if (archive->revisions != NULL) {
            for (int i = 0; i < celix_arrayList_size(archive->revisions); ++i) {
                bundle_revision_pt revision = celix_arrayList_get(archive->revisions, i);
                bundleRevision_destroy(revision);
            }
		}
        free(archive->location);
        free(archive->savedBundleStatePropertiesPath);
        free(archive->archiveRoot);
        free(archive->currentRevisionRoot);
        free(archive->storeRoot);
        celix_properties_destroy(archive->bundleStateProperties);
        celix_arrayList_destroy(archive->revisions);
        celixThreadMutex_destroy(&archive->lock);
        free(archive);
	}
	return CELIX_SUCCESS;
}

celix_status_t bundleArchive_recreate(celix_framework_t* fw, const char* archiveRoot, bundle_archive_pt* bundle_archive) {
    celix_status_t status = CELIX_SUCCESS;

    DIR *archiveRootDir = opendir(archiveRoot);
    if (archiveRootDir == NULL) {
        status = CELIX_FRAMEWORK_EXCEPTION;
        fw_logCode(fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Could not open archive root directory %s", archiveRoot);
        return status;
    }

    celix_properties_t* stateProps = NULL;
    struct stat st;
    char pathBuffer[512];
    char *propsPath = celix_utils_writeOrCreateString(pathBuffer, sizeof(pathBuffer), "%s/%s", archiveRoot, CELIX_BUNDLE_ARCHIVE_STATE_PROPERTIES_FILE_NAME);
    if (stat(propsPath, &st) == 0) {
        stateProps = celix_properties_load(propsPath);
    }
    celix_utils_freeStringIfNeeded(pathBuffer, propsPath);

    if (stateProps == NULL) {
        status = CELIX_FRAMEWORK_EXCEPTION;
        fw_logCode(fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Could not find previous revision for bundle archive %s", archiveRoot);
        return status;
    }

    long bndId = celix_properties_getAsLong(stateProps, CELIX_BUNDLE_ARCHIVE_BUNDLE_ID_PROPERTY_NAME, -1L);
    long revisionId = celix_properties_getAsLong(stateProps, CELIX_BUNDLE_ARCHIVE_BUNDLE_ID_PROPERTY_NAME, -1L);
    const char* location = celix_properties_get(stateProps, CELIX_BUNDLE_ARCHIVE_LOCATION_PROPERTY_NAME, NULL);
    celix_properties_destroy(stateProps);
    if (bndId < 0 || revisionId < 0 || location == NULL) {
        status = CELIX_FRAMEWORK_EXCEPTION;
        fw_logCode(fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Could not find previous revision entries for bundle archive %s", archiveRoot);
        return status;
    }

    status = bundleArchive_createArchiveInternal(fw, archiveRoot, bndId, location, revisionId, false, bundle_archive);
    if (status != CELIX_SUCCESS) {
        fw_logCode(fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Could not recreate bundle archive for %s", archiveRoot);
    }
    return status;
}

celix_status_t bundleArchive_getId(bundle_archive_pt archive, long *id) {
     *id = archive->id;
	return CELIX_SUCCESS;
}

celix_status_t bundleArchive_getLocation(bundle_archive_pt archive, const char **location) {
    *location = archive->location;
    return CELIX_SUCCESS;
}

celix_status_t bundleArchive_getArchiveRoot(bundle_archive_pt archive, const char **archiveRoot) {
    *archiveRoot = archive->archiveRoot;
    return CELIX_SUCCESS;
}

celix_status_t bundleArchive_getCurrentRevisionNumber(bundle_archive_pt archive, long *revisionNumber) {
    celixThreadMutex_lock(&archive->lock);
    *revisionNumber = archive->revisionNr;
    celixThreadMutex_unlock(&archive->lock);
    return CELIX_SUCCESS;
}

celix_status_t bundleArchive_getCurrentRevision(bundle_archive_pt archive, bundle_revision_pt *revision) {
    bundle_revision_pt rev = NULL;
    celixThreadMutex_lock(&archive->lock);
    if (celix_arrayList_size(archive->revisions) > 0) {
        rev = celix_arrayList_get(archive->revisions, celix_arrayList_size(archive->revisions) - 1);
    }
    celixThreadMutex_unlock(&archive->lock);
    *revision = rev;
    return rev == NULL ? CELIX_BUNDLE_EXCEPTION : CELIX_SUCCESS;
}

celix_status_t bundleArchive_getRevision(bundle_archive_pt archive, long revNr, bundle_revision_pt *revision) {
    bundle_revision_pt match = NULL;
    celixThreadMutex_lock(&archive->lock);
    for (int i = 0; i < celix_arrayList_size(archive->revisions); ++i) {
        bundle_revision_pt rev = celix_arrayList_get(archive->revisions, i);
        long nr = 0;
        bundleRevision_getNumber(rev, &nr);
        if (nr == revNr) {
            match = rev;
        }
    }
    celixThreadMutex_unlock(&archive->lock);
    *revision = match;
    return match == NULL ? CELIX_BUNDLE_EXCEPTION : CELIX_SUCCESS;
}

celix_status_t bundleArchive_getPersistentState(bundle_archive_pt archive, bundle_state_e *state) {
    celixThreadMutex_lock(&archive->lock);
    *state = archive->persistentState;
    celixThreadMutex_unlock(&archive->lock);
    return CELIX_SUCCESS;
}

celix_status_t bundleArchive_setPersistentState(bundle_archive_pt archive, bundle_state_e state) {
    celixThreadMutex_lock(&archive->lock);
    archive->persistentState = state;
    celixThreadMutex_unlock(&archive->lock);
    celix_bundleArchive_updateAndStoreBundleStateProperties(archive);
    return CELIX_SUCCESS;
}

celix_status_t bundleArchive_getRefreshCount(bundle_archive_pt archive __attribute__((unused)), long *refreshCount) {
    celixThreadMutex_lock(&archive->lock);
    *refreshCount = archive->refreshCount;
    celixThreadMutex_unlock(&archive->lock);
    return CELIX_SUCCESS;
}

celix_status_t bundleArchive_setRefreshCount(bundle_archive_pt archive __attribute__((unused))) {
    celixThreadMutex_lock(&archive->lock);
    archive->refreshCount++;
    celixThreadMutex_unlock(&archive->lock);
    celix_bundleArchive_updateAndStoreBundleStateProperties(archive);
    return CELIX_SUCCESS;
}

celix_status_t bundleArchive_getLastModified(bundle_archive_pt archive, time_t *lastModified) {
    struct timespec mod;
    celix_status_t status = celix_utils_getLastModified(archive->archiveRoot, &mod);
    if (status == CELIX_SUCCESS) {
        *lastModified = mod.tv_sec;
    }
    return status;
}

celix_status_t celix_bundleArchive_getLastModifiedCurrentRevision(bundle_archive_pt archive, struct timespec* lastModified) {
    celixThreadMutex_lock(&archive->lock);
    celix_status_t status = celix_utils_getLastModified(archive->currentRevisionRoot, lastModified);
    celixThreadMutex_unlock(&archive->lock);
    return status;
}

celix_status_t bundleArchive_setLastModified(bundle_archive_pt archive __attribute__((unused)), time_t lastModifiedTime  __attribute__((unused))) {
    return celix_utils_touch(archive->archiveRoot);
}

celix_status_t bundleArchive_revise(bundle_archive_pt archive, const char * location, const char *inputFile) {
    assert(inputFile == NULL); //Input file is deprecated and should be NULL
    fw_log(archive->fw->logger, CELIX_LOG_LEVEL_ERROR, "Revise bundle %s not supported yet.", location);
    return CELIX_BUNDLE_EXCEPTION;
}


celix_status_t bundleArchive_rollbackRevise(bundle_archive_pt archive, bool *rolledback) {
	*rolledback = true;
    fw_log(archive->fw->logger, CELIX_LOG_LEVEL_ERROR, "Revise rollback not supported.");
	return CELIX_BUNDLE_EXCEPTION;
}

celix_status_t bundleArchive_close(bundle_archive_pt archive) {
	// close revision
	// not yet needed/possible
	return CELIX_SUCCESS;
}

celix_status_t bundleArchive_closeAndDelete(bundle_archive_pt archive) {
	celix_status_t status = CELIX_SUCCESS;

	status = bundleArchive_close(archive);
	if (status == CELIX_SUCCESS) {
		const char* err = NULL;
		status = celix_utils_deleteDirectory(archive->archiveRoot, &err);
		framework_logIfError(archive->fw->logger, status, NULL, "Failed to delete archive root '%s': %s", archive->archiveRoot, err);
	}

	framework_logIfError(archive->fw->logger, status, NULL, "Failed to close and delete archive");

	return status;
}

const char* celix_bundleArchive_getPersistentStoreRoot(bundle_archive_t* archive) {
    return archive->storeRoot;
}

const char* celix_bundleArchive_getCurrentRevisionRoot(bundle_archive_t* archive) {
    celixThreadMutex_lock(&archive->lock);
    const char* revRoot = archive->currentRevisionRoot;
    celixThreadMutex_unlock(&archive->lock);
    return revRoot;
}


