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



#include <memory>
#include <climits>

#include <gtest/gtest.h>

#include "celix/ServiceRegistry.h"

class RegistryTest : public ::testing::Test {
public:
    RegistryTest() {}
    ~RegistryTest(){}

    celix::ServiceRegistry& registry() { return *reg; }
private:
    std::shared_ptr<celix::ServiceRegistry> reg{celix::ServiceRegistry::create("test")};
};

class MarkerInterface1 {
};

class MarkerInterface2 {
};

class MarkerInterface3 {
};


TEST_F(RegistryTest, ServiceRegistrationTest) {
    //empty registry
    EXPECT_EQ(0, registry().nrOfRegisteredServices());

    {
        auto svc = std::make_shared<MarkerInterface1>();
        celix::Properties properties{};
        properties["TEST"] = "VAL";
        celix::ServiceRegistration reg = registry().registerService(svc, properties);
        reg.wait();
        //no copy possible celix::ServiceRegistration copy = reg;

        long svcId = reg.serviceId();
        EXPECT_GE(svcId, -1L);
        EXPECT_TRUE(reg.valid());
        EXPECT_EQ(reg.properties()[celix::SERVICE_NAME], celix::typeName<MarkerInterface1>());
        EXPECT_EQ("VAL", reg.properties()["TEST"]);

        EXPECT_EQ(1, registry().nrOfRegisteredServices());
        EXPECT_EQ(1, registry().findServices<MarkerInterface1>().size());



        celix::ServiceRegistration moved = std::move(reg); //moving is valid
        EXPECT_EQ(svcId, moved.serviceId());
        EXPECT_EQ(1, registry().nrOfRegisteredServices());

        celix::ServiceRegistration moved2{std::move(moved)};
        EXPECT_EQ(1, registry().nrOfRegisteredServices());

        //out of scope -> deregister
    }
    EXPECT_EQ(0, registry().nrOfRegisteredServices());
    EXPECT_EQ(0, registry().findServices<MarkerInterface1>().size());

    auto svc1 = std::make_shared<MarkerInterface1>();
    auto svc2 = std::make_shared<MarkerInterface2>();
    //auto svc3 = std::make_shared<MarkerInterface3>();

    {
        celix::ServiceRegistration reg1 = registry().registerService(svc1);
        reg1.wait();
        EXPECT_EQ(1, registry().nrOfRegisteredServices());
        celix::ServiceRegistration reg2 = registry().registerService(svc1);
        reg2.wait();
        EXPECT_EQ(2, registry().nrOfRegisteredServices());
        celix::ServiceRegistration reg3 = registry().registerService(svc2);
        reg3.wait();
        EXPECT_EQ(3, registry().nrOfRegisteredServices());

        EXPECT_GT(reg1.serviceId(), 0);
        EXPECT_GT(reg2.serviceId(), 0);
        EXPECT_GT(reg3.serviceId(), 0);
        EXPECT_TRUE(reg1.valid());
        EXPECT_TRUE(reg2.valid());
        EXPECT_TRUE(reg3.valid());

        EXPECT_EQ(3, registry().nrOfRegisteredServices());
        EXPECT_EQ(2, registry().findServices<MarkerInterface1>().size());
        EXPECT_EQ(1, registry().findServices<MarkerInterface2>().size());
        EXPECT_EQ(0, registry().findServices<MarkerInterface3>().size());

        //out of scope -> deregister
    }
    EXPECT_EQ(0, registry().nrOfRegisteredServices());
    EXPECT_EQ(0, registry().findServices<MarkerInterface1>().size());
    EXPECT_EQ(0, registry().findServices<MarkerInterface2>().size());
    EXPECT_EQ(0, registry().findServices<MarkerInterface3>().size());
}

TEST_F(RegistryTest, SimpleFindServicesTest) {
    auto svc1 = std::make_shared<MarkerInterface1>();
    auto svc2 = std::make_shared<MarkerInterface2>();


    auto reg1 = registry().registerService(svc1);
    auto reg2 = registry().registerService(svc2);
    reg1.wait();
    reg2.wait();

    long firstSvc = registry().findService<MarkerInterface1>();
    EXPECT_GT(firstSvc, 0);
    std::vector<long> services = registry().findServices<MarkerInterface1>();
    EXPECT_EQ(1, services.size());

    auto reg3 = registry().registerService(svc1);
    auto reg4 = registry().registerService(svc2);
    reg3.wait();
    reg4.wait();

    long foundSvc = registry().findService<MarkerInterface1>(); //should find the first services
    EXPECT_GT(foundSvc, 0);
    EXPECT_EQ(firstSvc, foundSvc);
    services = registry().findServices<MarkerInterface1>();
    EXPECT_EQ(2, services.size());
}

TEST_F(RegistryTest, FindServicesTest) {
    auto svc1 = std::make_shared<MarkerInterface1>();

    auto reg1 = registry().registerService(svc1); //note no properties
    reg1.wait();

    celix::Properties properties{};
    properties["loc"] = "front";
    properties["answer"] = "42";
    auto reg2 = registry().registerService(svc1, properties);
    reg2.wait();

    properties.clear();
    properties["loc"] = "back";
    auto reg3 = registry().registerService(svc1, properties);
    reg3.wait();

    auto find1 = registry().findServices<MarkerInterface1>("(loc=*)"); // expecting 2
    auto find2 = registry().findServices<MarkerInterface1>("(answer=42)"); // expecting 1
    auto find3 = registry().findServices<MarkerInterface1>("(!(loc=*))"); // expecting 1

    EXPECT_EQ(2, find1.size());
    EXPECT_EQ(1, find2.size());
    EXPECT_EQ(1, find3.size());
}

TEST_F(RegistryTest, UseServices) {
    auto svc1 = std::make_shared<MarkerInterface1>();

    auto reg1 = registry().registerService(svc1);
    auto reg2 = registry().registerService(svc1);
    auto reg3 = registry().registerService(svc1);
    reg1.wait();
    reg2.wait();
    reg3.wait();

    long svcId1 = reg1.serviceId();

    celix::UseServiceOptions<MarkerInterface1> useOpts{};
    useOpts.use = [&](MarkerInterface1& svc) -> void {
        EXPECT_EQ(svc1.get(), &svc);
        //nop
    };
    useOpts.useWithProperties = [&](MarkerInterface1& svc, const celix::Properties &props) -> void {
        EXPECT_EQ(svc1.get(), &svc);
        long id = props.getAsLong(celix::SERVICE_ID, 0);
        EXPECT_EQ(svcId1, id);
    };
    useOpts.useWithOwner = [&](MarkerInterface1& svc, const celix::Properties &props, const celix::IResourceBundle &bnd) -> void {
        EXPECT_EQ(svc1.get(), &svc);
        long id = props.getAsLong(celix::SERVICE_ID, 0L);
        EXPECT_EQ(svcId1, id);
        EXPECT_EQ(LONG_MAX, bnd.id()); //not nullptr -> use empty bundle (bndId 0)
    };

    int nrCalled = registry().useServices(useOpts);
    EXPECT_TRUE(nrCalled > 0);
}

TEST_F(RegistryTest, RankingTest) {
    auto svc1 = std::make_shared<MarkerInterface1>();


    auto reg1 = registry().registerService(svc1); //no props -> ranking 0
    auto reg2 = registry().registerService(svc1); //no props -> ranking 0
    reg1.wait();
    reg2.wait();

    {
        auto reg3 = registry().registerService(svc1); //no props -> ranking 0
        reg3.wait();
        auto find = registry().findServices<MarkerInterface1>(); // expecting 3
        EXPECT_EQ(3, find.size());
        EXPECT_EQ(reg1.serviceId(), find[0]); //first registered service should be found first
        EXPECT_EQ(reg2.serviceId(), find[1]); //second registered service should be found second
        EXPECT_EQ(reg3.serviceId(), find[2]); //third registered service should be found third

        //note reg 3 out of scope
    }


    celix::Properties properties{};
    properties[celix::SERVICE_RANKING] = "100";
    auto reg4 = registry().registerService(svc1, properties); // ranking 100 -> before services with ranking 0
    reg4.wait();

    {
        auto reg5 = registry().registerService(svc1, properties); // ranking 100 -> before services with ranking 0
        reg5.wait();
        auto find = registry().findServices<MarkerInterface1>();
        EXPECT_EQ(4, find.size());
        EXPECT_EQ(reg4.serviceId(), find[0]); //ranking 100, oldest (i.e. highest ranking with lowest svcId)
        EXPECT_EQ(reg5.serviceId(), find[1]); //ranking 100, newest
        EXPECT_EQ(reg1.serviceId(), find[2]); //ranking 0, oldest
        EXPECT_EQ(reg2.serviceId(), find[3]); //ranking 0, newest

        //note reg 5 out of scope
    }

    properties[celix::SERVICE_RANKING] = "-100";
    auto reg6 = registry().registerService(svc1, properties); // ranking -100 -> after the rest
    reg6.wait();
    auto reg7 = registry().registerService(svc1, properties); // ranking -100 -> after the rest
    reg7.wait();
    properties[celix::SERVICE_RANKING] = "110";
    auto reg8 = registry().registerService(svc1, properties); // ranking 110 -> before the rest
    reg8.wait();
    properties[celix::SERVICE_RANKING] = "80";
    auto reg9 = registry().registerService(svc1, properties); // ranking 80 -> between ranking 0 adn 100
    reg9.wait();
    {
        auto find = registry().findServices<MarkerInterface1>();
        EXPECT_EQ(7, find.size());
        EXPECT_EQ(reg8.serviceId(), find[0]); //ranking 110
        EXPECT_EQ(reg4.serviceId(), find[1]); //ranking 100
        EXPECT_EQ(reg9.serviceId(), find[2]); //ranking 80
        EXPECT_EQ(reg1.serviceId(), find[3]); //ranking 0, oldest
        EXPECT_EQ(reg2.serviceId(), find[4]); //ranking 0, newest
        EXPECT_EQ(reg6.serviceId(), find[5]); //ranking -100, oldest
        EXPECT_EQ(reg7.serviceId(), find[6]); //ranking -100, newest
    }
}

TEST_F(RegistryTest, StdFunctionTest) {
    int count = 0;
    std::function<void()> func1 = [&count]{
        count++;
    };

    auto reg1 = registry().registerFunctionService("count", func1);
    reg1.wait();
    EXPECT_TRUE(reg1.valid());
    EXPECT_EQ(1, registry().nrOfRegisteredServices());

    std::function<int(double a, double b, const std::string &ref)> funcWithReturnAndArgs = [](double a, double b, const std::string &ref) -> int {
        return (int)(a/b) + (int)ref.size();
    };
    auto reg2 = registry().registerFunctionService("yet another function", funcWithReturnAndArgs);
    reg2.wait();
    EXPECT_TRUE(reg2.valid());
    EXPECT_EQ(2, registry().nrOfRegisteredServices());

    auto use = [](const std::function<void()> &count) {
        count();
    };
    EXPECT_EQ(0, count);
    celix::UseFunctionServiceOptions<std::function<void()>> useOpts{"count"};
    useOpts.use = use;
    int nrCalled = registry().useFunctionServices(useOpts);
    EXPECT_TRUE(nrCalled > 0);
    EXPECT_EQ(1, count);
}

TEST_F(RegistryTest, ListServicesTest) {
    std::vector<std::string> serviceNames = registry().listAllRegisteredServiceNames();
    EXPECT_EQ(0, serviceNames.size());

    std::function<void()> nop = []{/*nop*/};
    class MarkerInterface1 {};

    {
        auto reg1 = registry().registerFunctionService("nop", nop);
        reg1.wait();
        serviceNames = registry().listAllRegisteredServiceNames();
        EXPECT_EQ(1, serviceNames.size());

        auto svc2 = std::make_shared<MarkerInterface1>();
        auto reg2 = registry().registerService(svc2);
        reg2.wait();
        serviceNames = registry().listAllRegisteredServiceNames();
        EXPECT_EQ(2, serviceNames.size());
    }
    serviceNames = registry().listAllRegisteredServiceNames();
    EXPECT_EQ(0, serviceNames.size());
}

//TODO function use with props and bnd
//TODO use with filter
//TODO use with sync test (see BundleContext tests)
