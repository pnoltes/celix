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
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef FIND_HOOK_SERVICE_H_
#define FIND_HOOK_SERVICE_H_

#include "celix_array_list.h"
#include "celix_bundle.h"
#include "celix_filter.h"
#include "celix_properties.h"

#define OSGI_FRAMEWORK_FIND_HOOK_SERVICE_NAME "find_hook_service"
/**
 * @brief Service property (type=string) to target a find hook to a specific service name.
 *
 * A registered find hook service must provide this property.
 * The value must match the looked up service name (objectClass) for the hook to be invoked.
 */
#define CELIX_FIND_HOOK_TARGET_SERVICE_NAME "find.hook.service.name"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct find_hook_service_info find_hook_service_info_t;
typedef struct find_hook_service_info celix_find_hook_service_info_t;

typedef struct find_hook_service find_hook_service_t;
typedef struct find_hook_service celix_find_hook_service_t;

/**
 * Information for a service candidate in a find hook callback.
 *
 * The find hook can remove service candidates from the list by removing the corresponding
 * find_hook_service_info_t* entries from the provided array list.
 */
struct find_hook_service_info {
    /** @brief Service provider bundle for this candidate. */
    const celix_bundle_t* bundle;
    /** @brief Service properties for this candidate. */
    const celix_properties_t* properties;
    /** @brief Service id for this candidate. */
    long serviceId;
};

/**
 * Find hook service to filter services returned by service lookups.
 */
struct find_hook_service {
    void* handle;

    /**
     * Called when services are looked up.
     *
     * @param[in] handle The service handle.
     * @param[in] serviceName The queried service name. Never NULL.
     * @param[in] filter The queried filter. Can be NULL.
     * @param[in] allServices Whether all services were requested.
     * @param[in,out] serviceInfos A list of celix_find_hook_service_info_t* entries.
     *                            Hooks can remove entries from this list to hide service candidates.
     *                            Adding entries is not supported.
     * @return CELIX_SUCCESS if the hook processed the lookup.
     * @return CELIX_ILLEGAL_ARGUMENT if the input is invalid.
     * @return CELIX_SERVICE_EXCEPTION for unexpected hook failures.
     */
    celix_status_t (*find)(void* handle,
                           const char* serviceName,
                           const celix_filter_t* filter,
                           bool allServices,
                           celix_array_list_t* serviceInfos);
};

#ifdef __cplusplus
}
#endif

#endif //FIND_HOOK_SERVICE_H_
