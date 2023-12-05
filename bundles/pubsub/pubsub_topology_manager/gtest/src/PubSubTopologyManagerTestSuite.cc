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

#include <gtest/gtest.h>

#include <chrono>

#include "celix/FrameworkFactory.h"
#include "celix/FrameworkUtils.h"
#include "celix_condition.h"
#include "celix_constants.h"

#include "pubsub/publisher.h"
#include "pubsub/subscriber.h"
#include "pubsub_admin.h"
#include "pubsub_constants.h"
#include "celix_shell_command.h"

class PubSubTopologyManagerTestSuite : public ::testing::Test {
  public:
    const int WAIT_TIME_IN_MS = 300;

    std::string frameworkReadyFilter =
        std::string{"("} + CELIX_CONDITION_ID + "=" + CELIX_CONDITION_ID_FRAMEWORK_READY + ")";
    std::string psaReadyFilter = std::string{"("} + CELIX_CONDITION_ID + "=" + PUBSUB_PSA_READY_CONDITION_ID +
                                 ")";
    std::string pstmShellCommandFilter = std::string{"("} + CELIX_SHELL_COMMAND_NAME + "=celix::pstm)";

    PubSubTopologyManagerTestSuite() = default;

    void setupCelixFramework() {
        celix::Properties properties;
        properties.set("LOGHELPER_ENABLE_STDOUT_FALLBACK", "true");
        properties.set(CELIX_FRAMEWORK_CACHE_DIR, ".cachePstmTestSuite");
        properties.set("CELIX_LOGGING_DEFAULT_ACTIVE_LOG_LEVEL", "debug");

        // setting to 100ms to speed up tests and align with WAIT_TIME_IN_MS
        properties.set(PUBSUB_TOPOLOGY_MANAGER_HANDLING_THREAD_SLEEPTIME_MS, 25L);

        fw = celix::createFramework(properties);
        ctx = fw->getFrameworkBundleContext();

        size_t nr = celix::installBundleSet(*fw, TEST_BUNDLES);
        ASSERT_EQ(nr, 1); // pubsub topology manager bundle
    }

    void registerPsaStub(bool psaMatchesWithSubscribersAndPublishers) {
        auto psaStub = std::make_shared<pubsub_admin_service_t>();
        psaStub->handle = static_cast<void*>(&psaStub);

        psaStub->matchPublisher = [](void* /*handle*/,
                                     long /*svcRequesterBndId*/,
                                     const celix_filter_t* /*svcFilter*/,
                                     celix_properties_t** /*outTopicProperties*/,
                                     double* outScore,
                                     long* outSerializerSvcId,
                                     long* outProtocolSvcId) -> celix_status_t {
            *outScore = PUBSUB_ADMIN_FULL_MATCH_SCORE;
            *outSerializerSvcId = 42L;
            *outProtocolSvcId = 43L;
            return CELIX_SUCCESS;
        };
        if (!psaMatchesWithSubscribersAndPublishers) {
            psaStub->matchPublisher = [](void* /*handle*/,
                                         long /*svcRequesterBndId*/,
                                         const celix_filter_t* /*svcFilter*/,
                                         celix_properties_t** /*outTopicProperties*/,
                                         double* outScore,
                                         long* outSerializerSvcId,
                                         long* outProtocolSvcId) -> celix_status_t {
                *outScore = PUBSUB_ADMIN_NO_MATCH_SCORE;
                *outSerializerSvcId = 42L;
                *outProtocolSvcId = 43L;
                return CELIX_SUCCESS;
            };
        }

        psaStub->matchSubscriber = [](void* /*handle*/,
                                      long /*svcProviderBndId*/,
                                      const celix_properties_t* /*svcProperties*/,
                                      celix_properties_t** /*outTopicProperties*/,
                                      double* outScore,
                                      long* outSerializerSvcId,
                                      long* outProtocolSvcId) -> celix_status_t {
            *outScore = PUBSUB_ADMIN_FULL_MATCH_SCORE;
            *outSerializerSvcId = 42L;
            *outProtocolSvcId = 43L;
            return CELIX_SUCCESS;
        };
        if (!psaMatchesWithSubscribersAndPublishers) {
            psaStub->matchSubscriber = [](void* /*handle*/,
                                          long /*svcProviderBndId*/,
                                          const celix_properties_t* /*svcProperties*/,
                                          celix_properties_t** /*outTopicProperties*/,
                                          double* outScore,
                                          long* outSerializerSvcId,
                                          long* outProtocolSvcId) -> celix_status_t {
                *outScore = PUBSUB_ADMIN_NO_MATCH_SCORE;
                *outSerializerSvcId = 42L;
                *outProtocolSvcId = 43L;
                return CELIX_SUCCESS;
            };
        }

        psaStub->matchDiscoveredEndpoint =
            [](void* /*handle*/, const celix_properties_t* /*endpoint*/, bool* match) -> celix_status_t {
            *match = true;
            return CELIX_SUCCESS;
        };

        psaStub->setupTopicSender = [](void* /*handle*/,
                                       const char* /*scope*/,
                                       const char* /*topic*/,
                                       const celix_properties_t* /*topicProperties*/,
                                       long /*serializerSvcId*/,
                                       long /*protocolSvcId*/,
                                       celix_properties_t** publisherEndpoint) -> celix_status_t {
            *publisherEndpoint = celix_properties_create();
            return CELIX_SUCCESS;
        };

        psaStub->teardownTopicSender = [](void* /*handle*/,
                                          const char* /*scope*/,
                                          const char* /*topic*/) -> celix_status_t { return CELIX_SUCCESS; };

        psaStub->setupTopicReceiver = [](void* /*handle*/,
                                         const char* /*scope*/,
                                         const char* /*topic*/,
                                         const celix_properties_t* /*topicProperties*/,
                                         long /*serializerSvcId*/,
                                         long /*protocolSvcId*/,
                                         celix_properties_t** subscriberEndpoint) -> celix_status_t {
            *subscriberEndpoint = celix_properties_create();
            return CELIX_SUCCESS;
        };

        psaStub->teardownTopicReceiver = [](void* /*handle*/,
                                            const char* /*scope*/,
                                            const char* /*topic*/) -> celix_status_t { return CELIX_SUCCESS; };

        psaStub->addDiscoveredEndpoint =
            [](void* /*handle*/, const celix_properties_t* /*endpoint*/) -> celix_status_t { return CELIX_SUCCESS; };

        psaStub->removeDiscoveredEndpoint =
            [](void* /*handle*/, const celix_properties_t* /*endpoint*/) -> celix_status_t { return CELIX_SUCCESS; };

        psaStubReg = ctx->registerService<pubsub_admin_service_t>(psaStub, PUBSUB_ADMIN_SERVICE_NAME)
                         .addProperty(PUBSUB_ADMIN_SERVICE_TYPE, "stub")
                         .build();
    }

    void registerSubscriber() {
        auto sub = std::make_shared<pubsub_subscriber_t>();
        sub->handle = static_cast<void*>(&sub);
        sub->receive = [](void* /*handle*/,
                          const char* /*msgType*/,
                          unsigned int /*msgTypeId*/,
                          void* /*msg*/,
                          const celix_properties_t* /*metadata*/,
                          bool* /*release*/) -> int {
            // nop
            return 0;
        };
        subscriberReg = ctx->registerService<pubsub_subscriber_t>(sub, PUBSUB_SUBSCRIBER_SERVICE_NAME)
                       .addProperty(PUBSUB_SUBSCRIBER_TOPIC, "test")
                       .build();
        ASSERT_GE(subscriberReg->getServiceId(), 0);
    }

    void unregisterSubscriber() {
        subscriberReg.reset();
    }

    void unregisterPsaStub() {
        psaStubReg.reset();
    }

    void checkFrameworkReadyBecomesAvailable() {
        auto count = ctx->useService<celix_condition_t>()
                         .setFilter(frameworkReadyFilter)
                         .setTimeout(std::chrono::milliseconds{WAIT_TIME_IN_MS})
                         .build();
        ASSERT_EQ(count, 1);
    }

    void checkPsaReadyBecomesAvailable() {
        auto count = ctx->useService<celix_condition_t>()
                         .setFilter(psaReadyFilter)
                         .setTimeout(std::chrono::milliseconds{WAIT_TIME_IN_MS})
                         .build();
        ASSERT_EQ(count, 1);
    }

    void checkPsaReadyStaysUnavailable() {
        auto count = ctx->useService<celix_condition_t>()
                         .setFilter(psaReadyFilter)
                         .setTimeout(std::chrono::milliseconds{WAIT_TIME_IN_MS})
                         .build();
        ASSERT_EQ(count, 0);
    }

    void checkPsaReadyBecomesUnavailable() {
        size_t count = 0;
        auto now = std::chrono::steady_clock::now();
        auto waitUntil = now + std::chrono::milliseconds{WAIT_TIME_IN_MS};
        while (now <= waitUntil) {
            count = ctx->useService<celix_condition_t>()
                        .setFilter(psaReadyFilter)
                        .setTimeout(std::chrono::milliseconds{WAIT_TIME_IN_MS})
                        .build();
            if (count == 0) {
                return;
            }
            now = std::chrono::steady_clock::now();
        }
        ASSERT_EQ(count, 0);
    }

    void requestPublisher() {
        publisherTracker =
            ctx->trackServices<pubsub_publisher_t>(PUBSUB_PUBLISHER_SERVICE_NAME).setFilter("(topic=test)").build();
    }

    void cancelPublisherRequest() {
        publisherTracker->close();
        publisherTracker.reset();
    }

    void checkPsaReadyCommand(bool ready) {
        auto count = ctx->useService<celix_shell_command_t>(CELIX_SHELL_COMMAND_SERVICE_NAME)
                    .setFilter(pstmShellCommandFilter)
                    .addUseCallback([&](auto& cmd) {
                        char* buf;
                        size_t bufLen;
                        FILE* stream = open_memstream(&buf, &bufLen);
                        cmd.executeCommand(cmd.handle, "pstm", stream, stream);
                        fclose(stream);
                        auto checkStr =
                            ready ? std::string{"PSA ready       = true"} : std::string{"PSA ready       = false"};
                        ASSERT_TRUE(strstr(buf, checkStr.c_str()) != nullptr)
                            << "Expected to find '" << checkStr << "' in output, but got " << buf;
                    })
                    .build();
        ASSERT_EQ(count, 1);
    }

    std::shared_ptr<celix::Framework> fw{};
    std::shared_ptr<celix::BundleContext> ctx{};

  private:
    std::shared_ptr<celix::ServiceRegistration> psaStubReg{};
    std::shared_ptr<celix::ServiceRegistration> subscriberReg{};
    std::shared_ptr<celix::ServiceTracker<pubsub_publisher_t>> publisherTracker{};
};

TEST_F(PubSubTopologyManagerTestSuite, StartStopTestTest) {
    //Given a Celix framework with a pubsub topology manager bundle installed
    setupCelixFramework();

    //Then the framework can safely be stopped with a deadlock or memory leak.
}

TEST_F(PubSubTopologyManagerTestSuite, PsaNotReadyCheckTest) {
    //Given a Celix framework with a pubsub topology manager bundle installed
    setupCelixFramework();

    //Then the framework.ready service will become available
    checkFrameworkReadyBecomesAvailable();

    // But the psa.ready condition will not become available
    checkPsaReadyStaysUnavailable();

    // And the pstm shell command will print psa ready is false
    checkPsaReadyCommand(false);
}

TEST_F(PubSubTopologyManagerTestSuite, PsaReadyCheckForSubcriberTest) {
    //Given a Celix framework with a pubsub topology manager bundle installed
    setupCelixFramework();

    //Then the framework.ready service will become available
    checkFrameworkReadyBecomesAvailable();

    // But the psa.ready condition will not because available, because there is no subscriber provided or publisher
    // requested.
    checkPsaReadyStaysUnavailable();

    // When a subscriber is registered
    registerSubscriber();

    // And a PSA Stub is registered that matches with the subscriber
    registerPsaStub(true);

    // Then the psa.ready condition will become available
    checkPsaReadyBecomesAvailable();

    // And the pstm shell command will print psa ready is true
    checkPsaReadyCommand(true);
}

TEST_F(PubSubTopologyManagerTestSuite, PsaNotReadyCheckForSubcriberTest) {
    //Given a Celix framework with a pubsub topology manager bundle installed
    setupCelixFramework();

    //Then the framework.ready service will become available
    checkFrameworkReadyBecomesAvailable();

    // But the psa.ready condition will not because available, because there is no subscriber provided or publisher
    // requested.
    checkPsaReadyStaysUnavailable();

    // When a subscriber is registered
    registerSubscriber();

    // And a PSA Stub is registered that **will not** match with the subscriber
    registerPsaStub(false);

    // Then the psa.ready condition will not become available
    checkPsaReadyStaysUnavailable();

    // And the pstm shell command will print psa ready is false
    checkPsaReadyCommand(false);
}

TEST_F(PubSubTopologyManagerTestSuite, PsaReadyCheckForPublisherTest) {
    //Given a Celix framework with a pubsub topology manager bundle installed
    setupCelixFramework();

    //Then the framework.ready service will become available
    checkFrameworkReadyBecomesAvailable();

    // But the psa.ready condition will not because available, because there is no subscriber provided or publisher
    // requested.
    checkPsaReadyStaysUnavailable();

    // When a PSA Stub is registered that matches with the publisher
    registerPsaStub(true);

    // And a publisher is requested
    requestPublisher();

    // Then the psa.ready condition will become available
    checkPsaReadyBecomesAvailable();
}

TEST_F(PubSubTopologyManagerTestSuite, PsaNotReadyCheckForPublisherTest) {
    //Given a Celix framework with a pubsub topology manager bundle installed
    setupCelixFramework();

    //Then the framework.ready service will become available
    checkFrameworkReadyBecomesAvailable();

    // But the psa.ready condition will not because available, because there is no subscriber provided or publisher
    // requested.
    checkPsaReadyStaysUnavailable();

    // When a PSA Stub is registered that **will not** match with the publisher
    registerPsaStub(false);

    // And a publisher is requested
    requestPublisher();

    // Then the psa.ready condition will not become available
    checkPsaReadyStaysUnavailable();
}

TEST_F(PubSubTopologyManagerTestSuite, PsaReadyToggleBecauseOfPsaTest) {
    //Given a Celix framework with a pubsub topology manager bundle installed
    setupCelixFramework();

    //Then the framework.ready service will become available
    checkFrameworkReadyBecomesAvailable();

    // But the psa.ready condition will not because available, because there is no subscriber provided or publisher
    // requested.
    checkPsaReadyStaysUnavailable();

    // When a publisher is requested
    requestPublisher();

    // And a PSA Stub is registered that matches with the subscriber
    registerPsaStub(true);

    // Then the psa.ready condition will become available
    checkPsaReadyBecomesAvailable();

    // When a PSA Stub is removed
    unregisterPsaStub();

    // Then the psa.ready condition will become unavailable
    checkPsaReadyBecomesUnavailable();

    // When the PSA Stub is registered again, but now with a PSA that does not match with the subscriber
    registerPsaStub(false);

    // Then the psa.ready condition will **still** not become available
    checkPsaReadyStaysUnavailable();

    // When the PSA Stub is registered again, but now with a PSA that match with the publisher
    unregisterPsaStub();
    registerPsaStub(true);

    // Then the psa.ready condition will become available
    checkPsaReadyBecomesAvailable();
}

TEST_F(PubSubTopologyManagerTestSuite, PsaReadyToggleBecauseOfPublisherSubscriberTest) {
    //Given a Celix framework with a pubsub topology manager bundle installed
    setupCelixFramework();

    //Then the framework.ready service will become available
    checkFrameworkReadyBecomesAvailable();

    // But the psa.ready condition will not because available, because there is no subscriber provided or publisher
    // requested.
    checkPsaReadyStaysUnavailable();

    // When a PSA Stub is registered that matches with the subscriber
    registerPsaStub(true);

    // And a subscriber is registered
    registerSubscriber();

    // Then the psa.ready condition will become available
    checkPsaReadyBecomesAvailable();

    // When a publisher is requested
    requestPublisher();

    // Then the psa.ready condition will still available
    checkPsaReadyBecomesAvailable();

    // When the subscriber is unregistered
    unregisterSubscriber();

    // Then the psa.ready condition will still available (because of the publisher)
    checkPsaReadyBecomesAvailable();

    // When the publisher request is cancelled
    cancelPublisherRequest();

    // Then the psa.ready condition will become unavailable
    checkPsaReadyBecomesUnavailable();

    // When the subscriber is registered again
    registerSubscriber();

    // Then the psa.ready condition will become available
    checkPsaReadyBecomesAvailable();

    // When the subscriber is unregistered again
    unregisterSubscriber();

    // Then the psa.ready condition will become unavailable
    checkPsaReadyBecomesUnavailable();

    // When the publisher is requested again
    requestPublisher();

    // Then the psa.ready condition will become available
    checkPsaReadyBecomesAvailable();
}
