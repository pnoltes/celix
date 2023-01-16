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

/**
 * @brief Create bundle archive.
 *
 * Takes ownership of archiveRoot.
 */
celix_status_t bundleArchive_create(celix_framework_t* fw, const char *archiveRoot, long id, const char *location,
                                    bundle_archive_pt *bundle_archive);

celix_status_t bundleArchive_recreate(celix_framework_t* fw, const char *archiveRoot, bundle_archive_pt *bundle_archive);

celix_status_t bundleArchive_destroy(bundle_archive_pt archive);

/**
 * Returns the root of the bundle persistent store.
 */
const char* celix_bundleArchive_getPersistentStoreRoot(bundle_archive_t *archive);

/**
 * Get the last modified time of the current bundle revision.
 * @param[in] archive The bundle archive.
 * @param[out] lastModified The last modified time of the current bundle revision.
 * @return CELIX_SUCCESS if the last modified time is returned.
 */
celix_status_t celix_bundleArchive_getLastModifiedCurrentRevision(bundle_archive_pt archive, struct timespec* lastModified);

/**
 * Get the last modified time of the current bundle revision.
 * @param[in] archive The bundle archive.
 * @return The current revision root
 * @retval NULL if the current revision root is not set.
 */
const char* celix_bundleArchive_getCurrentRevisionRoot(bundle_archive_pt archive);

#endif /* BUNDLE_ARCHIVE_PRIVATE_H_ */
