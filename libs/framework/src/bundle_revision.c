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
#include <sys/stat.h>
#include <archive.h>

#include "celix_utils.h"
#include "celix_framework.h"
#include "bundle_revision_private.h"
#include "celix_framework_utils_private.h"
#include "framework_private.h"

celix_status_t bundleRevision_create(celix_framework_t* fw, const char *root, const char *location, long revisionNr, bundle_revision_pt *bundle_revision) {
    celix_status_t status = CELIX_SUCCESS;
	bundle_revision_pt revision =  malloc(sizeof(*revision));

    if (!revision) {
        status = CELIX_ENOMEM;
        fw_logCode(fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Cannot create bundle revision, out of memory");
        return CELIX_ENOMEM;
    }

    int state = mkdir(root, S_IRWXU);
    if ((state != 0) && (errno != EEXIST)) {
        free(revision);
        status = CELIX_FILE_IO_EXCEPTION;
        fw_logCode(fw->logger, CELIX_LOG_LEVEL_ERROR, status,
                   "Cannot create bundle revision, cannot create directory %s", root);
        return status;
    }

    status = celix_framework_utils_extractBundle(fw, location, root);
    if (status != CELIX_SUCCESS) {
        fw_logCode(fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Cannot create bundle revision, cannot extract bundle from %s to %s", location, root);
        free(revision);
        return status;
    }

    revision->fw = fw;
    revision->root = celix_utils_strdup(root);
    revision->location = celix_utils_strdup(location);;
    revision->libraryHandles = celix_arrayList_create();
    revision->revisionNr = revisionNr;
    //celixThreadMutex_create(&revision->libraryHandlesLock, NULL);

    if (revision->root == NULL || revision->location == NULL || revision->libraryHandles == NULL) {
        status = CELIX_ENOMEM;
        fw_logCode(fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Cannot create bundle revision, out of memory");
        bundleRevision_destroy(revision);
        return status;
    }

    char manifestBuffer[512];
    char* manifestPath = celix_utils_writeOrCreateString(manifestBuffer, sizeof(manifestBuffer), "%s/META-INF/MANIFEST.MF", root);
    status = manifest_createFromFile(manifestPath, &revision->manifest);
    celix_utils_freeStringIfNeeded(manifestBuffer, manifestPath);
    if (status != CELIX_SUCCESS) {
        fw_logCode(fw->logger, CELIX_LOG_LEVEL_ERROR, status, "Cannot create bundle revision, cannot create manifest from file %s/META-INF/MANIFEST.MF", root);
        bundleRevision_destroy(revision);
        return status;
    }

    *bundle_revision = revision;
	return status;
}

celix_status_t bundleRevision_destroy(bundle_revision_pt revision) {
    celix_arrayList_destroy(revision->libraryHandles);
    manifest_destroy(revision->manifest);
    free(revision->root);
    free(revision->location);
    free(revision);
	return CELIX_SUCCESS;
}

celix_status_t bundleRevision_getNumber(bundle_revision_pt revision, long *revisionNr) {
    *revisionNr = revision->revisionNr;
    return CELIX_SUCCESS;
}

celix_status_t bundleRevision_getLocation(bundle_revision_pt revision, const char **location) {
    *location = revision->location;
    return CELIX_SUCCESS;
}

celix_status_t bundleRevision_getRoot(bundle_revision_pt revision, const char **root) {
    *root = revision->root;
    return CELIX_SUCCESS;
}

celix_status_t bundleRevision_getManifest(bundle_revision_pt revision, manifest_pt *manifest) {
    *manifest = revision->manifest;
    return CELIX_SUCCESS;
}

