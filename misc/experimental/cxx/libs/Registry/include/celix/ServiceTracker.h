/**
 *Licensed to the Apache Software Foundation (ASF) under one
 *or more contributor license agreements.  See the NOTICE file
 *distributed with this work for additional information
 *regarding copyright ownership.  The ASF licenses this file
 *to you under the Apache License, Version 2.0 (the
 *"License"); you may not use this file except in compliance
 *with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *Unless required by applicable law or agreed to in writing,
 *software distributed under the License is distributed on an
 *"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 *specific language governing permissions and limitations
 *under the License.
 */

#pragma once

#include "celix/Filter.h"

namespace celix {

    //RAII service tracker: out of scope -> stop tracker
    // NOTE access not thread safe -> TODO make thread save?
    class ServiceTracker {
    public:
        class Impl; //opaque impl class

        explicit ServiceTracker(celix::ServiceTracker::Impl* impl);
        explicit ServiceTracker();

        ServiceTracker(ServiceTracker &&rhs) noexcept;

        ServiceTracker(const ServiceTracker &rhs) = delete;

        ~ServiceTracker();

        ServiceTracker &operator=(ServiceTracker &&rhs) noexcept;

        ServiceTracker &operator=(const ServiceTracker &rhs) = delete;

        int trackCount() const;

        const std::string& serviceName() const;

        const celix::Filter& filter() const;

        bool valid() const;

        //TODO use(Function)Service(s) calls

        void stop();

    private:
        std::unique_ptr<celix::ServiceTracker::Impl> pimpl;
    };
}
