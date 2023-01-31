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


#ifndef BUNDLE_ARCHIVE_PRIVATE_H_
#define BUNDLE_ARCHIVE_PRIVATE_H_

#include "bundle_archive.h"

#define CELIX_BUNDLE_ARCHIVE_STATE_PROPERTIES_FILE_NAME "bundle_state.properties"

#define CELIX_BUNDLE_ARCHIVE_SYMBOLIC_NAME_PROPERTY_NAME "bundle.symbolic_name"
#define CELIX_BUNDLE_ARCHIVE_VERSION_PROPERTY_NAME "bundle.version"
#define CELIX_BUNDLE_ARCHIVE_BUNDLE_ID_PROPERTY_NAME "bundle.id"
#define CELIX_BUNDLE_ARCHIVE_LOCATION_PROPERTY_NAME "bundle.location"
#define CELIX_BUNDLE_ARCHIVE_REVISION_PROPERTY_NAME "bundle.archive.revision"

//for Celix 3.0 update to a different revision name scheme
//#define CELIX_BUNDLE_ARCHIVE_REVISION_DIRECTORY_NAME_FORMAT "%s/revision_%li"
//OR
//#define CELIX_BUNDLE_ARCHIVE_REVISION_DIRECTORY_NAME_FORMAT "%s/revision"

//The refresh count in the revision version is always 0 and not supported.
#define CELIX_BUNDLE_ARCHIVE_REVISION_DIRECTORY_NAME_FORMAT "%s/version%li.0"
#define CELIX_BUNDLE_ARCHIVE_STORE_DIRECTORY_NAME "storage"

#define CELIX_BUNDLE_MANIFEST_REL_PATH "META-INF/MANIFEST.MF"

/**
 * @brief Create bundle archive.
 *
 * Takes ownership of archiveRoot.
 */
celix_status_t bundleArchive_create(celix_framework_t* fw, const char *archiveRoot, long id, const char *location, bundle_archive_pt *bundle_archive);

celix_status_t bundleArchive_destroy(bundle_archive_pt archive);

/**
 * @brief Returns the bundle id of the bundle archive.
 * @param archive The bundle archive.
 * @return The bundle id.
 */
long celix_bundleArchive_getId(bundle_archive_pt archive);

/**
 * @brief Returns the bundle symbolic name of the bundle archive.
 * @param archive The bundle archive.
 * @return The bundle symbolic name.
 */
const char* celix_bundleArchive_getSymbolicName(bundle_archive_pt archive);

/**
 * Returns the root of the bundle persistent store.
 */
const char* celix_bundleArchive_getPersistentStoreRoot(bundle_archive_t *archive);

/**
 * Get the last modified time of the current bundle revision.
 * @param[in] archive The bundle archive.
 * @return The current revision root
 * @retval NULL if the current revision root is not set.
 */
const char* celix_bundleArchive_getCurrentRevisionRoot(bundle_archive_pt archive);

#endif /* BUNDLE_ARCHIVE_PRIVATE_H_ */
