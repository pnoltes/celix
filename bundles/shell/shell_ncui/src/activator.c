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

#include <unistd.h>

#include "celix_bundle_activator.h"
#include "celix_shell.h"
#include "shell_ncui.h"

typedef struct celix_shell_ncui_activator {
    shell_ncui_t* ui;
    long shellTrackerId;
} celix_shell_ncui_activator_t;

static celix_status_t celix_shellNcuiActivator_start(celix_shell_ncui_activator_t* act, celix_bundle_context_t* ctx) {
    if (!isatty(STDIN_FILENO)) {
        celix_bundleContext_log(ctx, CELIX_LOG_LEVEL_INFO, "[Shell NCUI] no tty connected. Shell NCUI will not activate.");
        return CELIX_SUCCESS;
    }

    act->ui = shellNcui_create(ctx);
    if (act->ui == NULL) {
        return CELIX_ENOMEM;
    }

    celix_service_tracking_options_t opts = CELIX_EMPTY_SERVICE_TRACKING_OPTIONS;
    opts.filter.serviceName = CELIX_SHELL_SERVICE_NAME;
    opts.callbackHandle = act->ui;
    opts.set = (void*)shellNcui_setShell;
    act->shellTrackerId = celix_bundleContext_trackServicesWithOptions(ctx, &opts);

    celix_status_t status = shellNcui_start(act->ui);
    if (status != CELIX_SUCCESS) {
        celix_bundleContext_stopTracker(ctx, act->shellTrackerId);
        shellNcui_destroy(act->ui);
        act->ui = NULL;
    }
    return status;
}

static celix_status_t celix_shellNcuiActivator_stop(celix_shell_ncui_activator_t* act, celix_bundle_context_t* ctx) {
    if (act->ui != NULL) {
        celix_bundleContext_stopTracker(ctx, act->shellTrackerId);
        shellNcui_stop(act->ui);
        shellNcui_destroy(act->ui);
        act->ui = NULL;
    }
    return CELIX_SUCCESS;
}

CELIX_GEN_BUNDLE_ACTIVATOR(celix_shell_ncui_activator_t, celix_shellNcuiActivator_start, celix_shellNcuiActivator_stop)
