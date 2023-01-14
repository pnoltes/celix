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

#define CELIX_BUNDLE_ARCHIVE_STATE_PROPERTIES_FILE_NAME "bundle_state.properties"

#define CELIX_BUNDLE_ARCHIVE_SYMBOLIC_NAME_PROPERTY_NAME "bundle.symbolic_name"
#define CELIX_BUNDLE_ARCHIVE_VERSION_PROPERTY_NAME "bundle.version"
#define CELIX_BUNDLE_ARCHIVE_BUNDLE_ID_PROPERTY_NAME "bundle.id"

#define CELIX_BUNDLE_ARCHIVE_LOCATION_PROPERTY_NAME "bundle.location"
#define CELIX_BUNDLE_ARCHIVE_BUNDLE_STATE_PROPERTY_NAME "bundle.state"
#define CELIX_BUNDLE_ARCHIVE_REVISION_PROPERTY_NAME "bundle.archive.revision"
//TODO bundle zip hash

/**
 * @Note the bundle archive revision directory name is kept version.<number>.number> to not break compatibility with the
 * previous releases.
 */
#define CELIX_BUNDLE_ARCHIVE_REVISION_DIRECTORY_NAME_FORMAT "version0.%li"
#define CELIX_BUNDLE_ARCHIVE_STORE_DIRECTORY_NAME "store"

struct bundleArchive {
	celix_framework_t* fw;
	long id;
	char *archiveRoot;
    char *savedBundleStatePropertiesPath; //todo protect access to the saved bundle state properties path
	celix_array_list_t* revisions;

    //bundle state in properties form
    celix_properties_t* bundleStateProperties;

    //persistent info
    char* location;
    bool isSystemBundle;
    long refreshCount;
	long revisionNr;
	struct timespec lastModified;
	bundle_state_e persistentState;
};

//static celix_status_t bundleArchive_getRevisionLocation(bundle_archive_pt archive, long revNr, char **revision_location);
//static celix_status_t bundleArchive_setRevisionLocation(bundle_archive_pt archive, const char * location, long revNr);
//
//static celix_status_t bundleArchive_initialize(bundle_archive_pt archive);
//
//static celix_status_t bundleArchive_createRevisionFromLocation(bundle_archive_pt archive, const char *location, const char *inputFile, long revNr, bundle_revision_pt *bundle_revision);
//static celix_status_t bundleArchive_reviseInternal(bundle_archive_pt archive, bool isReload, long revNr, const char * location, const char *inputFile);
//
//static celix_status_t bundleArchive_readLastModified(bundle_archive_pt archive, time_t *time);
//static celix_status_t bundleArchive_writeLastModified(bundle_archive_pt archive);


static celix_status_t bundleArchive_initialize(bundle_archive_pt archive) {
    if (celix_utils_fileExists(archive->archiveRoot)) {
        fw_log(archive->fw->logger, CELIX_LOG_LEVEL_DEBUG, "Bundle archive root for bundle id %li already exists.",
               archive->id);
        return CELIX_SUCCESS;
    }


    const char* errorStr = NULL;
    celix_status_t status = celix_utils_createDirectory(archive->archiveRoot, false, &errorStr);
    if (status != CELIX_SUCCESS) {
        fw_log(archive->fw->logger, CELIX_LOG_LEVEL_ERROR, "Failed to initialize archive: %s", errorStr);
        return status;
    }

    celix_properties_setLong(archive->bundleStateProperties, CELIX_BUNDLE_ARCHIVE_BUNDLE_ID_PROPERTY_NAME, archive->id);
    celix_properties_set(archive->bundleStateProperties, CELIX_BUNDLE_ARCHIVE_BUNDLE_STATE_PROPERTY_NAME, celix_bundleState_getName(CELIX_BUNDLE_STATE_UNKNOWN));
    celix_properties_set(archive->bundleStateProperties, CELIX_BUNDLE_ARCHIVE_LOCATION_PROPERTY_NAME, archive->location);
    celix_properties_setLong(archive->bundleStateProperties, CELIX_BUNDLE_ARCHIVE_REVISION_PROPERTY_NAME, archive->revisionNr);

    celix_properties_store(archive->bundleStateProperties, archive->savedBundleStatePropertiesPath, "Bundle State Properties");
    return status;
}

static celix_status_t bundleArchive_createArchiveInternal(celix_framework_t* fw, char* archiveRoot, long id, const char *location, long revisionNr, bundle_archive_pt* bundle_archive) {
    celix_status_t status = CELIX_SUCCESS;
    bundle_archive_pt archive = calloc(1, sizeof(*archive));
    if (archive == NULL) {
        status = CELIX_ENOMEM;
        fw_logCode(fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Could not create archive");
        return status;
    }

    archive->fw = fw;
    archive->id = id;
    archive->location = id == CELIX_FRAMEWORK_BUNDLE_ID ? strdup("System Bundle") : celix_utils_strdup(location);
    archive->isSystemBundle = id == CELIX_FRAMEWORK_BUNDLE_ID;
    asprintf(&archive->savedBundleStatePropertiesPath, "%s/%s", archive->archiveRoot, CELIX_BUNDLE_ARCHIVE_STATE_PROPERTIES_FILE_NAME);
    archive->archiveRoot = archiveRoot;
    archive->revisionNr = revisionNr;
    archive->persistentState = CELIX_BUNDLE_STATE_UNKNOWN;
    archive->bundleStateProperties = celix_properties_create();
    archive->revisions = celix_arrayList_create();
    archive->lastModified.tv_sec = 0;
    archive->lastModified.tv_nsec = 0;

    if (archive->location == NULL || archive->savedBundleStatePropertiesPath == NULL || archiveRoot == NULL ||
            archive->bundleStateProperties == NULL || archive->revisions == NULL) {
        status = CELIX_ENOMEM;
        fw_logCode(fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Could not create archive");
        bundleArchive_destroy(archive);
        return status;
    }

    status = bundleArchive_initialize(archive);
    if (status != CELIX_SUCCESS) {
        fw_logCode(fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Could not initialize archive");
        bundleArchive_destroy(archive);
        return status;
    }

    *bundle_archive = archive;
    return status;
}


//TODO check if bundleArchive_initialize is called in all cases
celix_status_t bundleArchive_createSystemBundleArchive(celix_framework_t* fw, char *archiveRoot, bundle_archive_pt *bundle_archive) {
	return bundleArchive_createArchiveInternal(fw, archiveRoot, CELIX_FRAMEWORK_BUNDLE_ID, NULL, 1, bundle_archive);
}

//TODO check if bundleArchive_initialize is called in all cases
celix_status_t bundleArchive_create(celix_framework_t* fw, char *archiveRoot, long id, const char *location, const char *inputFile __attribute__((unused)), bundle_archive_pt *bundle_archive) {
    return bundleArchive_createArchiveInternal(fw, archiveRoot, id, location, 1, bundle_archive);
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
        celix_properties_destroy(archive->bundleStateProperties);
        celix_arrayList_destroy(archive->revisions);
        free(archive);
	}
	return CELIX_SUCCESS;
}


//TODO check if bundleArchive_initialize is called in all cases
celix_status_t bundleArchive_recreate(celix_framework_t* fw, char* archiveRoot, bundle_archive_pt* bundle_archive) {
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
    char *propsFile = celix_utils_writeOrCreateString(pathBuffer, sizeof(pathBuffer), "%s/%s", archiveRoot, CELIX_BUNDLE_ARCHIVE_STATE_PROPERTIES_FILE_NAME);
    if (stat(propsFile, &st) == 0) {
        stateProps = celix_properties_load(propsFile);
        if (stateProps == NULL) {
            fw_log(fw->logger, CELIX_LOG_LEVEL_TRACE, "Could not find previous revision for bundle archive %s", archiveRoot);
        }
    }
    celix_utils_freeStringIfNeeded(pathBuffer, propsFile);

    if (stateProps == NULL) {
        status = CELIX_FRAMEWORK_EXCEPTION;
        fw_logCode(fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Could not find previous revision for bundle archive %s", archiveRoot);
        return status;
    }

    long bndId = celix_properties_getAsLong(stateProps, CELIX_BUNDLE_ARCHIVE_BUNDLE_ID_PROPERTY_NAME, -1L);
    long revisionId = celix_properties_getAsLong(stateProps, CELIX_BUNDLE_ARCHIVE_BUNDLE_ID_PROPERTY_NAME, -1L);
    const char* location = celix_properties_get(stateProps, CELIX_BUNDLE_ARCHIVE_LOCATION_PROPERTY_NAME, NULL);
    if (bndId < 0 || revisionId < 0 || location == NULL) {
        status = CELIX_FRAMEWORK_EXCEPTION;
        fw_logCode(fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Could not find previous revision entries for bundle archive %s", archiveRoot);
        return status;
    }

    status = bundleArchive_createArchiveInternal(fw, archiveRoot, bndId, location, revisionId, bundle_archive);
    if (status != CELIX_SUCCESS) {
        fw_logCode(fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Could not create bundle archive for %s", archiveRoot);
    }
    return status;
}

celix_status_t bundleArchive_getId(bundle_archive_pt archive, long *id) {
    //TODO add mutex
     *id = archive->id;
	return CELIX_SUCCESS;
}

celix_status_t bundleArchive_getLocation(bundle_archive_pt archive, const char **location) {
    //TODO add mutex
    *location = archive->location;
    return CELIX_SUCCESS;
}

celix_status_t bundleArchive_getArchiveRoot(bundle_archive_pt archive, const char **archiveRoot) {
    //TODO add mutex
    *archiveRoot = archive->archiveRoot;
    return CELIX_SUCCESS;
}

celix_status_t bundleArchive_getCurrentRevisionNumber(bundle_archive_pt archive, long *revisionNumber) {
    //TODO add mutex
    *revisionNumber = archive->revisionNr;
    return CELIX_SUCCESS;
}

celix_status_t bundleArchive_getCurrentRevision(bundle_archive_pt archive, bundle_revision_pt *revision) {
    if (celix_arrayList_size(archive->revisions) > 0) {
        *revision = celix_arrayList_get(archive->revisions, celix_arrayList_size(archive->revisions) - 1);
        return CELIX_SUCCESS;
    }
    return CELIX_BUNDLE_EXCEPTION;
}

celix_status_t bundleArchive_getRevision(bundle_archive_pt archive, long revNr, bundle_revision_pt *revision) {
    for (int i = 0; i < celix_arrayList_size(archive->revisions); ++i) {
        bundle_revision_pt rev = celix_arrayList_get(archive->revisions, i);
        long nr = 0;
        bundleRevision_getNumber(rev, &nr);
        if (nr == revNr) {
            *revision = rev;
            return CELIX_SUCCESS;
        }
    }
    return CELIX_BUNDLE_EXCEPTION;
}

static celix_status_t celix_bundleArchive_saveState(bundle_archive_pt archive) {
    celix_status_t status = CELIX_SUCCESS;
    celix_properties_t* props = archive->bundleStateProperties;
    if (props != NULL) {
        celix_properties_setLong(props, CELIX_BUNDLE_ARCHIVE_BUNDLE_ID_PROPERTY_NAME, archive->id);
        celix_properties_setLong(props, CELIX_BUNDLE_ARCHIVE_REVISION_PROPERTY_NAME, archive->revisionNr);
        celix_properties_set(props, CELIX_BUNDLE_ARCHIVE_LOCATION_PROPERTY_NAME, archive->location);
        //todo store bundle symbolic name and version
        celix_properties_store(props, archive->savedBundleStatePropertiesPath, "Bundle archive state properties");
    }
    return status;
}

//load bundle archive state properties
//static celix_status_t celix_bundleArchive_loadState(bundle_archive_pt archive) {
//    celix_status_t status = CELIX_SUCCESS;
//    celix_properties_t* props = celix_properties_load(archive->savedBundleStatePropertiesPath);
//    if (!props) {
//        status = CELIX_BUNDLE_EXCEPTION;
//        fw_logCode(archive->fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Could not load bundle archive state properties for %s", archive->savedBundleStatePropertiesPath);
//        return status;
//    }
//
//    celix_properties_destroy(archive->bundleStateProperties); //destroy old properties
//    archive->bundleStateProperties = props;
//    archive->id = celix_properties_getAsLong(props, CELIX_BUNDLE_ARCHIVE_BUNDLE_ID_PROPERTY_NAME, -1L);
//    archive->revisionNr = celix_properties_getAsLong(props, CELIX_BUNDLE_ARCHIVE_REVISION_PROPERTY_NAME, -1L);
//    archive->location = celix_utils_strdup(celix_properties_get(props, CELIX_BUNDLE_ARCHIVE_LOCATION_PROPERTY_NAME, NULL));
//    status = celix_utils_getLastModified(archive->archiveRoot, &archive->lastModified);
//    if (status != CELIX_SUCCESS) {
//        fw_logCode(archive->fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Could not get last modified for %s", archive->archiveRoot);
//    }
//
//    return status;
//}

celix_status_t bundleArchive_getPersistentState(bundle_archive_pt archive, bundle_state_e *state) {
    //TODO add mutex
    *state = archive->persistentState;
    return CELIX_SUCCESS;
}

celix_status_t bundleArchive_setPersistentState(bundle_archive_pt archive, bundle_state_e state) {
    //TODO add mutex
    archive->persistentState = state;
    celix_bundleArchive_saveState(archive);
    return CELIX_SUCCESS;
}

celix_status_t bundleArchive_getRefreshCount(bundle_archive_pt archive __attribute__((unused)), long *refreshCount) {
    //deprecated, just return 0
    *refreshCount = 0;
    return CELIX_SUCCESS;
}

celix_status_t bundleArchive_setRefreshCount(bundle_archive_pt archive __attribute__((unused))) {
    //deprecated, do nothing
    return CELIX_SUCCESS;
}

celix_status_t bundleArchive_getLastModified(bundle_archive_pt archive, time_t *lastModified) {
    //TODO add mutex
    *lastModified = archive->lastModified.tv_sec;
    return CELIX_SUCCESS;
}

celix_status_t bundleArchive_setLastModified(bundle_archive_pt archive __attribute__((unused)), time_t lastModifiedTime  __attribute__((unused))) {
    //TODO add mutex
    //nop, ignore because last modified is set when loading the archive
    return CELIX_SUCCESS;
}

//static celix_status_t bundleArchive_readLastModified(bundle_archive_pt archive, time_t* time) {
//    struct timespec modified;
//    celix_status_t status = celix_utils_getLastModified(archive->archiveRoot, &modified);
//    if (status != CELIX_SUCCESS) {
//        fw_logCode(archive->fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Could not get last modified for %s", archive->archiveRoot);
//        return status;
//    }
//    *time = (time_t) modified.tv_sec;
//    return status;
//}
//
//static celix_status_t bundleArchive_writeLastModified(bundle_archive_pt archive) {
//    celix_status_t status = celix_utils_touch(archive->archiveRoot);
//    if (status != CELIX_SUCCESS) {
//        fw_logCode(archive->fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Could not touch %s", archive->archiveRoot);
//    }
//    return status;
//}

celix_status_t bundleArchive_revise(bundle_archive_pt archive, const char * location, const char *inputFile) {
    assert(inputFile == NULL); //Input file is deprecated and should be NULL
    //nop, not sure why this is needed
    return CELIX_SUCCESS;
    /*
	celix_status_t status = CELIX_SUCCESS;
	long revNr = 0l;
	if (!linkedList_isEmpty(archive->revisions)) {
		long revisionNr;
		status = bundleRevision_getNumber(linkedList_getLast(archive->revisions), &revisionNr);
		revNr = revisionNr + 1;
	}
	if (status == CELIX_SUCCESS) {
		status = bundleArchive_reviseInternal(archive, false, revNr, location, inputFile);
	}

	framework_logIfError(celix_frameworkLogger_globalLogger(), status, NULL, "Could not revise bundle archive");

	return status;
     */
}

//static celix_status_t bundleArchive_reviseInternal(bundle_archive_pt archive, bool isReload, long revNr, const char * location, const char *inputFile) {
//	celix_status_t status = CELIX_SUCCESS;
//    //nop, not sure why this is needed
//    return status;
//    /*
//	bundle_revision_pt revision = NULL;
//
//	if (inputFile != NULL) {
//		location = "inputstream:";
//	}
//
//	status = bundleArchive_createRevisionFromLocation(archive, location, inputFile, revNr, &revision);
//
//	if (status == CELIX_SUCCESS) {
//		if (!isReload) {
//			status = bundleArchive_setRevisionLocation(archive, location, revNr);
//		}
//
//		linkedList_addElement(archive->revisions, revision);
//	}
//
//	framework_logIfError(celix_frameworkLogger_globalLogger(), status, NULL, "Could not revise bundle archive");
//
//	return status;
//     */
//}

celix_status_t bundleArchive_rollbackRevise(bundle_archive_pt archive, bool *rolledback) {
	*rolledback = true;
	return CELIX_SUCCESS;
}

//static celix_status_t bundleArchive_createRevisionFromLocation(bundle_archive_pt archive, const char *location, const char *inputFile, long revNr, bundle_revision_pt *bundle_revision) {
//	celix_status_t status = CELIX_SUCCESS;
//	char root[256];
//	long refreshCount;
//
//	status = bundleArchive_getRefreshCount(archive, &refreshCount);
//	if (status == CELIX_SUCCESS) {
//		bundle_revision_pt revision = NULL;
//
//		sprintf(root, "%s/version%ld.%ld", archive->archiveRoot, refreshCount, revNr);
//		status = bundleRevision_create(archive->fw, root, location, revNr, &revision);
//
//		if (status == CELIX_SUCCESS) {
//			*bundle_revision = revision;
//		}
//	}
//
//	framework_logIfError(archive->fw->logger, status, NULL, "Could not create revision [location=%s,inputFile=%s]", location, inputFile);
//
//	return status;
//}

//static celix_status_t bundleArchive_getRevisionLocation(bundle_archive_pt archive, long revNr, char **revision_location) {
//        celix_status_t status = CELIX_SUCCESS;
//        char revisionLocation[256];
//        long refreshCount;
//
//        status = bundleArchive_getRefreshCount(archive, &refreshCount);
//        if (status == CELIX_SUCCESS) {
//            FILE *revisionLocationFile;
//
//            snprintf(revisionLocation, sizeof(revisionLocation), "%s/version%ld.%ld/revision.location", archive->archiveRoot, refreshCount, revNr);
//
//            revisionLocationFile = fopen(revisionLocation, "r");
//            if (revisionLocationFile != NULL) {
//                char location[256];
//                fgets(location , sizeof(location) , revisionLocationFile);
//                fclose(revisionLocationFile);
//
//                *revision_location = strdup(location);
//                status = CELIX_SUCCESS;
//            } else {
//                // revision file not found
//                printf("Failed to open revision file at: %s\n", revisionLocation);
//                status = CELIX_FILE_IO_EXCEPTION;
//            }
//        }
//
//
//        framework_logIfError(archive->fw->logger, status, NULL, "Failed to get revision location");
//
//        return status;
//}
//
//static celix_status_t bundleArchive_setRevisionLocation(bundle_archive_pt archive, const char * location, long revNr) {
//	celix_status_t status = CELIX_SUCCESS;
//
//	char revisionLocation[256];
//	long refreshCount;
//
//	status = bundleArchive_getRefreshCount(archive, &refreshCount);
//	if (status == CELIX_SUCCESS) {
//		FILE * revisionLocationFile;
//
//		snprintf(revisionLocation, sizeof(revisionLocation), "%s/version%ld.%ld/revision.location", archive->archiveRoot, refreshCount, revNr);
//
//		revisionLocationFile = fopen(revisionLocation, "w");
//		if (revisionLocationFile == NULL) {
//			status = CELIX_FILE_IO_EXCEPTION;
//		} else {
//			fprintf(revisionLocationFile, "%s", location);
//			fclose(revisionLocationFile);
//		}
//	}
//
//	framework_logIfError(archive->fw->logger, status, NULL, "Failed to set revision location");
//
//	return status;
//}

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

const char* celix_bundleArchive_getPersistentStoreRoot(bundle_archive_t *archive) {
    return archive->savedBundleStatePropertiesPath;
}

const char* celix_bundleArchive_getBundleLatestRevisionRoot(bundle_archive_t *archive) {
    //TODO
    return NULL;
    /*
     bundle_revision_t *revision = linkedList_isEmpty(archive->revisions) ? NULL : linkedList_getLast(archive->revisions);
    return revision == NULL ? NULL : revision->root;
     */
}