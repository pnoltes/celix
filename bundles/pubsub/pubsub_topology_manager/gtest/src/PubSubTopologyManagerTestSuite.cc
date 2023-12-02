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

class PubSubTopologyManagerTestSuite : public ::testing::Test {
  public:
    const int WAIT_TIME_IN_MS = 300;

    std::string frameworkReadyFilter =
        std::string{"("} + CELIX_CONDITION_ID + "=" + CELIX_CONDITION_ID_FRAMEWORK_READY + ")";
    std::string psaReadyFilter = std::string{"("} + CELIX_CONDITION_ID + "=" + PUBSUB_PSA_READY_CONDITION_ID +
                                 ")";

    PubSubTopologyManagerTestSuite() = default;

    void setupCelixFramework() {
        celix::Properties properties;
        properties.set("LOGHELPER_ENABLE_STDOUT_FALLBACK", "true");
        properties.set(CELIX_FRAMEWORK_CACHE_DIR, ".cachePstmTestSuite");
        properties.set("CELIX_LOGGING_DEFAULT_ACTIVE_LOG_LEVEL", "info");

        // setting to 100ms to speed up tests and align with WAIT_TIME_IN_MS
        properties.set(PUBSUB_TOPOLOGY_MANAGER_HANDLING_THREAD_SLEEPTIME_MS, 100L);

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

    void testPsaReadyWithSubscriber(bool psaMatchesWithSubscribers) {
        // Given a Celix framework with a pubsub topology manager bundle installed
        setupCelixFramework();

        // And a pubsub admin service stub is registered. The PSA will match subscribers/publishers based on
        // psaMatchesWithSubscribers
        registerPsaStub(psaMatchesWithSubscribers);

        // Then the framework.ready service will become available
        auto count = ctx->useService<celix_condition_t>()
                         .setFilter(frameworkReadyFilter)
                         .setTimeout(std::chrono::milliseconds{WAIT_TIME_IN_MS})
                         .build();
        ASSERT_EQ(count, 1);

        // But the psa.ready condition will not because available, because there is no subscriber provided or publisher
        // requested.
        count = ctx->useService<celix_condition_t>()
                    .setFilter(psaReadyFilter)
                    .setTimeout(std::chrono::milliseconds{WAIT_TIME_IN_MS})
                    .build();
        ASSERT_EQ(count, 0);

        // When a subscriber is registered
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
        auto reg = ctx->registerService<pubsub_subscriber_t>(sub, PUBSUB_SUBSCRIBER_SERVICE_NAME)
                       .addProperty(PUBSUB_SUBSCRIBER_TOPIC, "test")
                       .build();
        ASSERT_GE(reg->getServiceId(), 0);

        if (psaMatchesWithSubscribers) {
            // Then the psa.ready condition will become available, because the psa matches with the subscriber
            count = ctx->useService<celix_condition_t>()
                        .setFilter(psaReadyFilter)
                        .setTimeout(std::chrono::milliseconds{WAIT_TIME_IN_MS})
                        .build();
            ASSERT_EQ(count, 1);
        } else {
            // Then the psa.ready condition will not become available, because the psa does not match with the
            // subscriber
            count = ctx->useService<celix_condition_t>()
                        .setFilter(psaReadyFilter)
                        .setTimeout(std::chrono::milliseconds{WAIT_TIME_IN_MS})
                        .build();
            ASSERT_EQ(count, 0);
        }
    }

    void testPsaReadyWithPublisher(bool psaMatchesWithPublishers) {
        // Given a Celix framework with a pubsub topology manager bundle installed
        setupCelixFramework();

        // And a pubsub admin service stub is registered. The PSA will match subscribers/publishers based on
        // psaMatchesWithPublishers
        registerPsaStub(psaMatchesWithPublishers);

        // Then the framework.ready service will become available
        auto count = ctx->useService<celix_condition_t>()
                         .setFilter(frameworkReadyFilter)
                         .setTimeout(std::chrono::milliseconds{WAIT_TIME_IN_MS})
                         .build();
        ASSERT_EQ(count, 1);

        // But the psa.ready condition will not because available, because there is no subscriber provided or publisher
        // requested.
        count = ctx->useService<celix_condition_t>()
                    .setFilter(psaReadyFilter)
                    .setTimeout(std::chrono::milliseconds{WAIT_TIME_IN_MS})
                    .build();
        ASSERT_EQ(count, 0);

        // When a publisher is requested (service-on-demand)
        auto tracker =
            ctx->trackServices<pubsub_publisher_t>(PUBSUB_PUBLISHER_SERVICE_NAME).setFilter("(topic=test)").build();

        if (psaMatchesWithPublishers) {
            // Then the psa.ready condition will become available, because the psa matches with the requested publisher
            count = ctx->useService<celix_condition_t>()
                        .setFilter(psaReadyFilter)
                        .setTimeout(std::chrono::milliseconds{WAIT_TIME_IN_MS})
                        .build();
            ASSERT_EQ(count, 1);
        } else {
            // Then the psa.ready condition will not become available, because the psa does not match with the requested
            // publisher
            count = ctx->useService<celix_condition_t>()
                        .setFilter(psaReadyFilter)
                        .setTimeout(std::chrono::milliseconds{WAIT_TIME_IN_MS})
                        .build();
            ASSERT_EQ(count, 0);
        }
    }

    std::shared_ptr<celix::Framework> fw{};
    std::shared_ptr<celix::BundleContext> ctx{};

  private:
    std::shared_ptr<celix::ServiceRegistration> psaStubReg{};
};

TEST_F(PubSubTopologyManagerTestSuite, StartStopTest) {
    //Given a Celix framework with a pubsub topology manager bundle installed
    setupCelixFramework();

    //Then the framework can safely be stopped with a deadlock or memory leak.
}

TEST_F(PubSubTopologyManagerTestSuite, PsaNotReadyCheck) {
    //Given a Celix framework with a pubsub topology manager bundle installed
    setupCelixFramework();

    //Then the framework.ready service will become available
    auto count = ctx->useService<celix_condition_t>()
            .setFilter(frameworkReadyFilter)
            .setTimeout(std::chrono::milliseconds{WAIT_TIME_IN_MS})
            .build();
    ASSERT_EQ(count, 1);

    // But the psa.ready condition will not because available, because there is no subscriber provided or publisher
    // requested.
    count = ctx->useService<celix_condition_t>()
            .setFilter(psaReadyFilter)
            .setTimeout(std::chrono::milliseconds{WAIT_TIME_IN_MS})
            .build();
    ASSERT_EQ(count, 0);
}

TEST_F(PubSubTopologyManagerTestSuite, PsaReadyCheckForSubcriber) {
    testPsaReadyWithSubscriber(true);
}

TEST_F(PubSubTopologyManagerTestSuite, PsaNotReadyCheckForSubcriber) {
    testPsaReadyWithSubscriber(false);
}

TEST_F(PubSubTopologyManagerTestSuite, PsaReadyCheckForPublisher) {
    testPsaReadyWithPublisher(true);
}

TEST_F(PubSubTopologyManagerTestSuite, PsaNotReadyCheckForPublisher) {
    testPsaReadyWithPublisher(false);
}
