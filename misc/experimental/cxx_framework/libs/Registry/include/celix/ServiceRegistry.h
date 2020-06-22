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

#include <utility>
#include <vector>
#include <functional>
#include <memory>
#include <string_view>

#include "celix/Constants.h"
#include "celix/Properties.h"
#include "celix/Filter.h"
#include "celix/Utils.h"
#include "celix/IResourceBundle.h"
#include "celix/IServiceFactory.h"
#include "celix/ServiceTracker.h"
#include "celix/ServiceRegistration.h"

namespace celix {

    template<typename I>
    struct UseServiceOptions {
        int limit{1}; //the limit for the services to be found. 0 -> unlimited
        celix::Filter filter{};
        long targetServiceId{-1L}; //note -1 means not targeting a specific service id.
        std::chrono::milliseconds waitFor{0}; //note not 0, means the use call with wait for 'waitFor' period of time for a service match to become available
        std::function<void(I& svc)> use{};
        std::function<void(I& svc, const celix::Properties &props)> useWithProperties{};
        std::function<void(I& svc, const celix::Properties &props, const celix::IResourceBundle &bundle)> useWithOwner{};
    };

    template<typename F>
    struct UseFunctionServiceOptions {
        explicit UseFunctionServiceOptions(std::string_view fn) : functionName{fn} {}
        int limit{1}; //the limit for the services to be found. 0 -> unlimited
        const std::string functionName;
        celix::Filter filter{};
        long targetServiceId{-1L}; //note -1 means not targeting a specific service id.
        std::chrono::milliseconds waitFor{0}; //note not 0, means the use call with wait for 'waitFor' period of time for a service match to become available
        std::function<void(const F& func)> use{};
        std::function<void(const F& func, const celix::Properties &props)> useWithProperties{};
        std::function<void(const F& func, const celix::Properties &props, const celix::IResourceBundle &bundle)> useWithOwner{};
    };

    struct UseAnyServiceOptions {
        int limit{1}; //the limit for the services to be found. 0 -> unlimited
        celix::Filter filter{};
        long targetServiceId{-1L}; //note -1 means not targeting a specific service id.
        std::chrono::milliseconds waitFor{0}; //note not 0, means the use call with wait for 'waitFor' period of time for a service match to become available
        std::function<void(const std::shared_ptr<void>& svc)> use{};
        std::function<void(const std::shared_ptr<void>& svc, const celix::Properties &props)> useWithProperties{};
        std::function<void(const std::shared_ptr<void>& svc, const celix::Properties &props, const celix::IResourceBundle &bundle)> useWithOwner{};
    };

    template<typename I>
    struct ServiceTrackerOptions {
        celix::Filter filter{};

        std::function<void(const std::shared_ptr<I> &svc)> set{};
        std::function<void(const std::shared_ptr<I> &svc, const celix::Properties &props)> setWithProperties{};
        std::function<void(const std::shared_ptr<I> &svc, const celix::Properties &props, const celix::IResourceBundle &owner)> setWithOwner{};

        std::function<void(const std::shared_ptr<I> &svc)> add{};
        std::function<void(const std::shared_ptr<I> &svc, const celix::Properties &props)> addWithProperties{};
        std::function<void(const std::shared_ptr<I> &svc, const celix::Properties &props, const celix::IResourceBundle &owner)> addWithOwner{};

        std::function<void(const std::shared_ptr<I> &svc)> remove{};
        std::function<void(const std::shared_ptr<I> &svc, const celix::Properties &props)> removeWithProperties{};
        std::function<void(const std::shared_ptr<I> &svc, const celix::Properties &props, const celix::IResourceBundle &owner)> removeWithOwner{};

        std::function<void(std::vector<std::shared_ptr<I>> rankedServices)> update{};
        std::function<void(std::vector<std::tuple<std::shared_ptr<I>, const celix::Properties*>> rankedServices)> updateWithProperties{};
        std::function<void(std::vector<std::tuple<std::shared_ptr<I>, const celix::Properties*, const celix::IResourceBundle*>> rankedServices)> updateWithOwner{};

        //TODO lock free update calls atomics, rcu, hazard pointers ??

        /**
         * pre and post update hooks, can be used if a trigger is needed before or after an service update.
         */
        std::function<void()> preServiceUpdateHook{};
        std::function<void()> postServiceUpdateHook{};
    };

    template<typename F>
    struct FunctionServiceTrackerOptions {
        explicit FunctionServiceTrackerOptions(std::string fn) : functionName{std::move(fn)} {}
        const std::string functionName;

        celix::Filter filter{};

        std::function<void(F &func)> set{};
        std::function<void(F &func, const celix::Properties &props)> setWithProperties{};
        std::function<void(F &func, const celix::Properties &props, const celix::IResourceBundle &owner)> setWithOwner{};

        std::function<void(F &func)> add{};
        std::function<void(F &func, const celix::Properties &props)> addWithProperties{};
        std::function<void(F &func, const celix::Properties &props, const celix::IResourceBundle &owner)> addWithOwner{};

        std::function<void(F &func)> remove{};
        std::function<void(F &func, const celix::Properties &props)> removeWithProperties{};
        std::function<void(F &func, const celix::Properties &props, const celix::IResourceBundle &owner)> removeWithOwner{};

        std::function<void(std::vector<F> rankedServices)> update{};
        std::function<void(std::vector<std::tuple<F, const celix::Properties*>> rankedServices)> updateWithProperties{};
        std::function<void(std::vector<std::tuple<F, const celix::Properties*, const celix::IResourceBundle*>> rankedServices)> updateWithOwner{};

        std::function<void()> preServiceUpdateHook{};
        std::function<void()> postServiceUpdateHook{};
    };


    class ServiceRegistry {
    public:
        static std::shared_ptr<ServiceRegistry> create(std::string_view name);

        ServiceRegistry() = default;
        virtual ~ServiceRegistry() = default;
        ServiceRegistry(celix::ServiceRegistry &&rhs) = delete;
        ServiceRegistry& operator=(celix::ServiceRegistry &&rhs) = delete;
        ServiceRegistry& operator=(ServiceRegistry &rhs) = delete;
        ServiceRegistry(const ServiceRegistry &rhs) = delete;

        virtual const std::string& name() const = 0;

        template<typename I>
        celix::ServiceRegistration registerService(std::shared_ptr<I> svc, celix::Properties props = {}, const std::shared_ptr<celix::IResourceBundle>& owner = {});

        template<typename I>
        celix::ServiceRegistration registerServiceFactory(std::shared_ptr<celix::IServiceFactory<I>> factory, celix::Properties props = {}, const std::shared_ptr<celix::IResourceBundle>& owner = {});

        template<typename F>
        celix::ServiceRegistration registerFunctionService(const std::string &functionName, F function, celix::Properties props = {}, const std::shared_ptr<celix::IResourceBundle>& owner = {});

        template<typename I>
        celix::ServiceTracker trackServices(celix::ServiceTrackerOptions<I> opts, const std::shared_ptr<celix::IResourceBundle>& requester = {});

        template<typename F>
        celix::ServiceTracker trackFunctionServices(celix::FunctionServiceTrackerOptions<F> opts, const std::shared_ptr<celix::IResourceBundle>& requester = {});

        template<typename I>
        int useServices(celix::UseServiceOptions<I> opts, const std::shared_ptr<celix::IResourceBundle>& requester = {}) const;

        template<typename F>
        int useFunctionServices(celix::UseFunctionServiceOptions<F> opts, const std::shared_ptr<celix::IResourceBundle>& requester = {}) const;

        template<typename I>
        //NOTE C++17 typename std::enable_if<!std::is_callable<I>::value, long>::type
        long findService(const celix::Filter& filter = celix::Filter{}) const {
            auto services = findServices<I>(filter);
            return services.size() > 0 ? services[0] : -1L;
        }

        template<typename F>
        //NOTE C++17 typename std::enable_if<std::is_callable<I>::value, long>::type
        long findFunctionService(const std::string &functionName, const celix::Filter& filter = celix::Filter{}) const {
            auto services = findFunctionServices<F>(functionName, filter);
            return services.size() > 0 ? services[0] : -1L;
        }

        template<typename I>
        //NOTE C++17 typename std::enable_if<!std::is_callable<I>::value, std::vector<long>>::type
        std::vector<long> findServices(const celix::Filter& filter = celix::Filter{}) const {
            auto svcName = celix::typeName<I>();
            return findAnyServices(svcName, filter);
        }

        template<typename F>
        //NOTE C++17 typename std::enable_if<std::is_callable<I>::value, std::vector<long>>::type
        std::vector<long> findFunctionServices(const std::string &functionName, const celix::Filter& filter = celix::Filter{}) const {
            auto svcName = celix::functionServiceName<F>(functionName);
            return findAnyServices(svcName, filter);
        }


        //GENERIC / ANY calls. note these work on void use with care

        virtual int useAnyServices(const std::string &svcOrFunctionName, celix::UseAnyServiceOptions opts, const std::shared_ptr<celix::IResourceBundle>& requester = {}) const = 0;

        virtual celix::ServiceRegistration registerAnyService(const std::string& svcName, std::shared_ptr<void> service, celix::Properties props = {}, const std::shared_ptr<celix::IResourceBundle>& owner = {}) = 0;

        virtual celix::ServiceRegistration registerAnyServiceFactory(const std::string& svcName, std::shared_ptr<celix::IServiceFactory<void>> factory, celix::Properties props = {}, const std::shared_ptr<celix::IResourceBundle>& owner = {}) = 0;

        virtual celix::ServiceTracker trackAnyServices(const std::string &svcName, celix::ServiceTrackerOptions<void> opts, const std::shared_ptr<celix::IResourceBundle>& requester = {}) = 0;

        virtual std::vector<long> findAnyServices(const std::string &svcName, const celix::Filter& filter = celix::Filter{}) const = 0;

        //some additional registry info
        virtual std::vector<std::string> listAllRegisteredServiceNames() const = 0;

        virtual long nrOfRegisteredServices() const = 0;

        virtual long nrOfServiceTrackers() const = 0;
    };
}

std::ostream& operator<<(std::ostream &out, celix::ServiceRegistration& serviceRegistration);


/**********************************************************************************************************************
  Service Registration Template Implementation
 **********************************************************************************************************************/


template<typename I>
inline celix::ServiceRegistration celix::ServiceRegistry::registerService(std::shared_ptr<I> svc, celix::Properties props, const std::shared_ptr<celix::IResourceBundle>& owner) {
    auto svcName = celix::typeName<I>();
    std::shared_ptr<void> anySvc = std::static_pointer_cast<void>(svc);
    return registerAnyService(std::move(svcName), std::move(anySvc), std::move(props), owner);
}

template<typename I>
inline celix::ServiceRegistration celix::ServiceRegistry::registerServiceFactory(std::shared_ptr<celix::IServiceFactory<I>> outerFactory, celix::Properties props, const std::shared_ptr<celix::IResourceBundle>& owner) {
    class VoidServiceFactory : public celix::IServiceFactory<void> {
    public:
        explicit VoidServiceFactory(std::shared_ptr<celix::IServiceFactory<I>> _factory) : factory{std::move(_factory)} {}
        ~VoidServiceFactory() override = default;

        std::shared_ptr<void> createBundleSpecificService(const celix::IResourceBundle &bnd, const celix::Properties &props) override {
            std::shared_ptr<I> typedSvc = factory->createBundleSpecificService(bnd, props);
            return std::static_pointer_cast<void>(typedSvc);
        }
        void bundleSpecificServiceRemoved(const celix::IResourceBundle &bnd, const celix::Properties &props) override {
            factory->bundleSpecificServiceRemoved(bnd, props);
        }
    private:
        std::shared_ptr<celix::IServiceFactory<I>> factory;
    };
    auto anyFactory = std::make_shared<VoidServiceFactory>(outerFactory);
    auto svcName = celix::typeName<I>();
    return registerAnyService(std::move(svcName), std::move(anyFactory), std::move(props), owner);
}

template<typename F>
inline celix::ServiceRegistration celix::ServiceRegistry::registerFunctionService(const std::string& functionName, F outerFunction, celix::Properties props, const std::shared_ptr<celix::IResourceBundle>& owner) {
    auto functionPtr = std::shared_ptr<F>{new F{std::move(outerFunction)}};
    auto voidPtr = std::static_pointer_cast<void>(functionPtr);
    auto svcName = celix::functionServiceName<F>(functionName);
    return registerAnyService(svcName, std::move(voidPtr), std::move(props), owner);
}


template<typename I>
inline int celix::ServiceRegistry::useServices(celix::UseServiceOptions<I> opts, const std::shared_ptr<celix::IResourceBundle>& requester) const {
    celix::UseAnyServiceOptions anyOpts;
    anyOpts.limit = opts.limit;
    anyOpts.targetServiceId = opts.targetServiceId;
    anyOpts.waitFor = opts.waitFor;
    anyOpts.filter = std::move(opts.filter);

    if (opts.use) {
        //TODO improve potential 3x copy of opts
        anyOpts.use = [opts](const std::shared_ptr<void> svc) {
            std::shared_ptr<I> typedSvc = std::static_pointer_cast<I>(svc);
            opts.use(*typedSvc);
        };
    }
    if (opts.useWithProperties) {
        anyOpts.useWithProperties = [opts](const std::shared_ptr<void> svc, const celix::Properties &props) {
            std::shared_ptr<I> typedSvc = std::static_pointer_cast<I>(svc);
            opts.useWithProperties(*typedSvc, props);
        };
    }
    if (opts.useWithOwner) {
        anyOpts.useWithOwner = [opts](const std::shared_ptr<void> svc, const celix::Properties &props, const celix::IResourceBundle &bnd) {
            std::shared_ptr<I> typedSvc = std::static_pointer_cast<I>(svc);
            opts.useWithOwner(*typedSvc, props, bnd);
        };
    }

    auto svcName = celix::typeName<I>();
    return useAnyServices(std::move(svcName), std::move(anyOpts), requester);
}

template<typename F>
int celix::ServiceRegistry::useFunctionServices(celix::UseFunctionServiceOptions<F> opts, const std::shared_ptr<celix::IResourceBundle>& requester) const {
    celix::UseAnyServiceOptions anyOpts;
    anyOpts.limit = opts.limit;
    anyOpts.targetServiceId = opts.targetServiceId;
    anyOpts.waitFor = opts.waitFor;
    anyOpts.filter = std::move(opts.filter);

    if (opts.use) {
        //TODO improve potential 3x copy of opts
        anyOpts.use = [opts](const std::shared_ptr<void> rawFunction) {
            std::shared_ptr<F> function = std::static_pointer_cast<F>(rawFunction);
            opts.use(*function);
        };
    }
    if (opts.useWithProperties) {
        anyOpts.useWithProperties = [opts](const std::shared_ptr<void> rawFunction, const celix::Properties &props) {
            std::shared_ptr<F> function = std::static_pointer_cast<F>(rawFunction);
            opts.useWithProperties(*function, props);
        };
    }
    if (opts.useWithOwner) {
        anyOpts.useWithOwner = [opts](const std::shared_ptr<void> rawFunction, const celix::Properties &props, const celix::IResourceBundle &bnd) {
            std::shared_ptr<F> function = std::static_pointer_cast<F>(rawFunction);
            opts.useWithOwner(*function, props, bnd);
        };
    }

    auto svcName = celix::functionServiceName<F>(opts.functionName);
    return useAnyServices(std::move(svcName), std::move(anyOpts), requester);
}

template<typename I>
inline celix::ServiceTracker celix::ServiceRegistry::trackServices(celix::ServiceTrackerOptions<I> opts, const std::shared_ptr<celix::IResourceBundle>& requester) {

    ServiceTrackerOptions<void> anyOpts{};
    anyOpts.filter = std::move(opts.filter);

    if (opts.setWithOwner != nullptr) {
        auto set = std::move(opts.setWithOwner);
        anyOpts.setWithOwner = [set](const std::shared_ptr<void>& svc, const celix::Properties &props, const celix::IResourceBundle &owner){
            auto typedSvc = std::static_pointer_cast<I>(svc);
            set(typedSvc, props, owner);
        };
    }
    if (opts.setWithProperties != nullptr) {
        auto set = std::move(opts.setWithProperties);
        anyOpts.setWithProperties = [set](const std::shared_ptr<void>& svc, const celix::Properties &props){
            auto typedSvc = std::static_pointer_cast<I>(svc);
            set(typedSvc, props);
        };
    }
    if (opts.set != nullptr) {
        auto set = std::move(opts.set);
        anyOpts.set = [set](const std::shared_ptr<void>& svc){
            auto typedSvc = std::static_pointer_cast<I>(svc);
            set(typedSvc);
        };
    }

    if (opts.addWithOwner != nullptr) {
        auto add = std::move(opts.addWithOwner);
        anyOpts.addWithOwner = [add](const std::shared_ptr<void>& svc, const celix::Properties &props, const celix::IResourceBundle &bnd) {
            auto typedSvc = std::static_pointer_cast<I>(svc);
            add(typedSvc, props, bnd);
        };
    }
    if (opts.addWithProperties != nullptr) {
        auto add = std::move(opts.addWithProperties);
        anyOpts.addWithProperties = [add](const std::shared_ptr<void>& svc, const celix::Properties &props) {
            auto typedSvc = std::static_pointer_cast<I>(svc);
            add(typedSvc, props);
        };
    }
    if (opts.add != nullptr) {
        auto add = std::move(opts.add);
        anyOpts.add = [add](const std::shared_ptr<void>& svc) {
            auto typedSvc = std::static_pointer_cast<I>(svc);
            add(typedSvc);
        };
    }

    if (opts.removeWithOwner != nullptr) {
        auto rem = std::move(opts.removeWithOwner);
        anyOpts.removeWithOwner = [rem](const std::shared_ptr<void>& svc, const celix::Properties &props, const celix::IResourceBundle &bnd) {
            auto typedSvc = std::static_pointer_cast<I>(svc);
            rem(typedSvc, props, bnd);
        };
    }
    if (opts.removeWithProperties != nullptr) {
        auto rem = std::move(opts.removeWithProperties);
        anyOpts.removeWithProperties = [rem](const std::shared_ptr<void>& svc, const celix::Properties &props) {
            auto typedSvc = std::static_pointer_cast<I>(svc);
            rem(typedSvc, props);
        };
    }
    if (opts.remove != nullptr) {
        auto rem = std::move(opts.remove);
        anyOpts.remove = [rem](const std::shared_ptr<void>& svc) {
            auto typedSvc = std::static_pointer_cast<I>(svc);
            rem(typedSvc);
        };
    }

    if (opts.updateWithOwner != nullptr) {
        auto update = std::move(opts.updateWithOwner);
        anyOpts.updateWithOwner = [update](std::vector<std::tuple<std::shared_ptr<void>, const celix::Properties *, const celix::IResourceBundle*>> rankedServices) {
            std::vector<std::tuple<std::shared_ptr<I>, const celix::Properties*, const celix::IResourceBundle*>> typedServices{};
            typedServices.reserve(rankedServices.size());
            for (auto &tuple : rankedServices) {
                auto typedSvc = std::static_pointer_cast<I>(std::get<0>(tuple));
                typedServices.push_back(std::make_tuple(typedSvc, std::get<1>(tuple), std::get<2>(tuple)));
            }
            update(std::move(typedServices));
        };
    }
    if (opts.updateWithProperties != nullptr) {
        auto update = std::move(opts.updateWithProperties);
        anyOpts.updateWithProperties = [update](std::vector<std::tuple<std::shared_ptr<void>, const celix::Properties *>> rankedServices) {
            std::vector<std::tuple<std::shared_ptr<I>, const celix::Properties*>> typedServices{};
            typedServices.reserve(rankedServices.size());
            for (auto &tuple : rankedServices) {
                auto typedSvc = std::static_pointer_cast<I>(std::get<0>(tuple));
                typedServices.push_back(std::make_tuple(typedSvc, std::get<1>(tuple)));
            }
            update(std::move(typedServices));
        };
    }
    if (opts.update != nullptr) {
        auto update = std::move(opts.update);
        anyOpts.update = [update](std::vector<std::shared_ptr<void>> rankedServices) {
            std::vector<std::shared_ptr<I>> typedServices{};
            typedServices.reserve(rankedServices.size());
            for (auto &svc : rankedServices) {
                auto typedSvc = std::static_pointer_cast<I>(svc);
                typedServices.push_back(typedSvc);
            }
            update(std::move(typedServices));
        };
    }

    if (opts.preServiceUpdateHook) {
        anyOpts.preServiceUpdateHook = std::move(opts.preServiceUpdateHook);
    }
    if (opts.postServiceUpdateHook) {
        anyOpts.postServiceUpdateHook = std::move(opts.postServiceUpdateHook);
    }

    auto svcName = celix::typeName<I>();

    return trackAnyServices(svcName, std::move(anyOpts), requester);
}

template<typename F>
inline celix::ServiceTracker celix::ServiceRegistry::trackFunctionServices(celix::FunctionServiceTrackerOptions<F> opts, const std::shared_ptr<celix::IResourceBundle>& requester) {
    ServiceTrackerOptions<void> anyOpts{};
    auto svcName = celix::functionServiceName<F>(opts.functionName);
    anyOpts.filter = std::move(opts.filter);

    if (opts.setWithOwner != nullptr) {
        auto set = std::move(opts.setWithOwner);
        anyOpts.setWithOwner = [set](std::shared_ptr<void> svc, celix::Properties &props, celix::IResourceBundle &owner){
            auto typedSvc = std::static_pointer_cast<F>(svc);
            set(typedSvc, props, owner);
        };
    }
    if (opts.setWithProperties != nullptr) {
        auto set = std::move(opts.setWithProperties);
        anyOpts.setWithProperties = [set](std::shared_ptr<void> svc, celix::Properties &props){
            auto typedSvc = std::static_pointer_cast<F>(svc);
            set(*typedSvc, props);
        };
    }
    if (opts.set != nullptr) {
        auto set = std::move(opts.set);
        anyOpts.set = [set](std::shared_ptr<void> svc){
            auto typedSvc = std::static_pointer_cast<F>(svc);
            set(*typedSvc);
        };
    }

    if (opts.addWithOwner != nullptr) {
        auto add = std::move(opts.addWithOwner);
        anyOpts.addWithOwner = [add](std::shared_ptr<void> svc, celix::Properties &props, celix::IResourceBundle &bnd) {
            auto typedSvc = std::static_pointer_cast<F>(svc);
            add(*typedSvc, props, bnd);
        };
    }
    if (opts.addWithProperties != nullptr) {
        auto add = std::move(opts.addWithProperties);
        anyOpts.addWithProperties = [add](std::shared_ptr<void> svc, celix::Properties &props) {
            auto typedSvc = std::static_pointer_cast<F>(svc);
            add(*typedSvc, props);
        };
    }
    if (opts.add != nullptr) {
        auto add = std::move(opts.add);
        anyOpts.add = [add](std::shared_ptr<void> svc) {
            auto typedSvc = std::static_pointer_cast<F>(svc);
            add(*typedSvc);
        };
    }

    if (opts.removeWithOwner != nullptr) {
        auto rem = std::move(opts.removeWithOwner);
        anyOpts.removeWithOwner = [rem](std::shared_ptr<void> svc, celix::Properties &props, celix::IResourceBundle &bnd) {
            auto typedSvc = std::static_pointer_cast<F>(svc);
            rem(*typedSvc, props, bnd);
        };
    }
    if (opts.removeWithProperties != nullptr) {
        auto rem = std::move(opts.removeWithProperties);
        anyOpts.removeWithProperties = [rem](std::shared_ptr<void> svc, celix::Properties &props) {
            auto typedSvc = std::static_pointer_cast<F>(svc);
            rem(*typedSvc, props);
        };
    }
    if (opts.remove != nullptr) {
        auto rem = std::move(opts.remove);
        anyOpts.remove = [rem](std::shared_ptr<void> svc) {
            auto typedSvc = std::static_pointer_cast<F>(svc);
            rem(*typedSvc);
        };
    }

    if (opts.updateWithOwner != nullptr) {
        auto update = std::move(opts.updateWithOwner);
        anyOpts.updateWithOwner = [update](std::vector<std::tuple<std::shared_ptr<void>, celix::Properties *, celix::IResourceBundle*>> rankedServices) {
            std::vector<std::tuple<std::shared_ptr<F>, celix::Properties*, celix::IResourceBundle*>> typedServices{};
            typedServices.reserve(rankedServices.size());
            for (auto &tuple : rankedServices) {
                auto typedSvc = std::static_pointer_cast<F>(std::get<0>(tuple));
                typedServices.push_back(std::make_tuple(*typedSvc, std::get<1>(tuple), std::get<2>(tuple)));
            }
            update(std::move(typedServices));
        };
    }
    if (opts.updateWithProperties != nullptr) {
        auto update = std::move(opts.updateWithProperties);
        anyOpts.updateWithProperties = [update](std::vector<std::tuple<std::shared_ptr<void>, celix::Properties *>> rankedServices) {
            std::vector<std::tuple<std::shared_ptr<F>, celix::Properties*>> typedServices{};
            typedServices.reserve(rankedServices.size());
            for (auto &tuple : rankedServices) {
                auto typedSvc = std::static_pointer_cast<F>(std::get<0>(tuple));
                typedServices.push_back(std::make_tuple(*typedSvc, std::get<1>(tuple)));
            }
            update(std::move(typedServices));
        };
    }
    if (opts.update != nullptr) {
        auto update = std::move(opts.update);
        anyOpts.update = [update](std::vector<std::shared_ptr<void>> rankedServices) {
            std::vector<std::shared_ptr<F>> typedServices{};
            typedServices.reserve(rankedServices.size());
            for (auto &svc : rankedServices) {
                auto typedSvc = std::static_pointer_cast<F>(svc);
                typedServices.push_back(*typedSvc);
            }
            update(std::move(typedServices));
        };
    }

    if (opts.preServiceUpdateHook) {
        anyOpts.preServiceUpdateHook = std::move(opts.preServiceUpdateHook);
    }
    if (opts.postServiceUpdateHook) {
        anyOpts.postServiceUpdateHook = std::move(opts.postServiceUpdateHook);
    }

    return trackAnyServices(svcName, std::move(anyOpts), requester);
}