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

#include "celix/BundleActivator.h"
#include "get_bundle_name_service.h"

namespace {
    class BundleActivator {
    public:
        explicit BundleActivator(const std::shared_ptr<celix::BundleContext> &ctx) {
            auto svc = std::make_shared<get_bundle_name_service>();
            svc->getBundleName = []() -> char * {
                auto& ctx = celix::getBundleContext();
                static auto name = ctx->getBundle().getSymbolicName();
                return celix_utils_strdup(name.c_str());
            };
            reg = ctx->registerService<get_bundle_name_service>(svc, GET_BUNDLE_NAME_SERVICE_NAME).build();
        }

        ~BundleActivator() {
            //NOTE getting ctx to ensure this the ctx is still valid during activator dtor
            auto& ctx = celix::getBundleContext();
            ctx->logDebug("~BundleActivator");
        }

    private:
        std::shared_ptr<celix::ServiceRegistration> reg{};
    };
}

CELIX_GEN_CXX_BUNDLE_ACTIVATOR(BundleActivator)
