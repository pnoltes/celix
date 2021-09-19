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
#include "celix/FrameworkFactory.h"
#include "get_bundle_name_service.h"

class GetBundleContextTestSuite : public ::testing::Test {
public:
    GetBundleContextTestSuite() {
        fw = celix::createFramework();
        ctx = fw->getFrameworkBundleContext();
    }

    std::shared_ptr<celix::Framework> fw{};
    std::shared_ptr<celix::BundleContext> ctx{};
};

TEST_F(GetBundleContextTestSuite, GetBundleContextFromFramework) {
    auto cCtx = celix_getBundleContext();
    ASSERT_TRUE(cCtx == nullptr);
}

TEST_F(GetBundleContextTestSuite, GetBundleContextWithinService) {
    long bndId1 = ctx->installBundle(GET_BUNDLECONTEXT_TEST_BUNDLE1);
    ASSERT_GT(bndId1, 0);
    auto callback1 = [](get_bundle_name_service& svc) {
        char* name = svc.getBundleName();
        EXPECT_STREQ("get_bundle_context_test_bundle1", name);
        free(name);
    };

    //rule: using celix_getBundleContext() should return the bundle context of bundle which calling code belongs to.
    auto count = ctx->useServices<get_bundle_name_service>(GET_BUNDLE_NAME_SERVICE_NAME)
            .addUseCallback(callback1)
            .build();
    EXPECT_EQ(1, count);

//   Note not possible, because of RTLD_LOCAL and SO_NAME behaviour
//    //rule: installing another bundle should not impact the result of celix_getBundleContext() for the first bundle.
//    long bndId2 = ctx->installBundle(GET_BUNDLECONTEXT_TEST_BUNDLE2);
//    ASSERT_GT(bndId2, 0);
//    count = ctx->useService<get_bundle_name_service>(GET_BUNDLE_NAME_SERVICE_NAME)
//            .addUseCallback(callback1)
//            .build();
//    EXPECT_EQ(1, count);
//
//    //uninstall bndId1 should result in calling the get_bundle_name_service of the second bundle.
//    ctx->uninstallBundle(bndId1);
//    auto callback2 = [](get_bundle_name_service& svc) {
//        char* name = svc.getBundleName();
//        EXPECT_STREQ("get_bundle_context_test_bundle2", name);
//        free(name);
//    };
//    count = ctx->useService<get_bundle_name_service>(GET_BUNDLE_NAME_SERVICE_NAME)
//            .addUseCallback(callback2)
//            .build();
//    EXPECT_EQ(1, count);
//
//    //rule: installing another bundle should not impact the result of celix_getBundleContext() for the first bundle.
//    long bndId3 = ctx->installBundle(GET_CXX_BUNDLECONTEXT_TEST_BUNDLE);
//    ASSERT_GT(bndId3, 0);
//    count = ctx->useService<get_bundle_name_service>(GET_BUNDLE_NAME_SERVICE_NAME)
//            .addUseCallback(callback2)
//            .build();
//    EXPECT_EQ(1, count);
//
//    //uninstall bndId2 should result in calling the get_bundle_name_service of the third bundle.
//    ctx->uninstallBundle(bndId2);
//    auto callback3 = [](get_bundle_name_service& svc) {
//        EXPECT_STREQ("get_cxx_bundle_context_test_bundle", svc.getBundleName());
//    };
//    count = ctx->useService<get_bundle_name_service>(GET_BUNDLE_NAME_SERVICE_NAME)
//            .addUseCallback(callback3)
//            .build();
//    EXPECT_EQ(1, count);

    //rule: installing another bundle should not impact the result of celix_getBundleContext() for the first bundle.
    long bndId2 = ctx->installBundle(GET_CXX_BUNDLECONTEXT_TEST_BUNDLE);
    ASSERT_GT(bndId2, 0);
    auto callback3 = [bndId1, bndId2](get_bundle_name_service& svc, const celix::Properties& /*props*/, const celix::Bundle& owner) {
        auto* name = svc.getBundleName();
        if (owner.getId() == bndId1) {
            EXPECT_STREQ("get_bundle_context_test_bundle1", name);
        } else {
            EXPECT_STREQ("get_cxx_bundle_context_test_bundle", name);
        }
        free(name);
    };
    count = ctx->useServices<get_bundle_name_service>(GET_BUNDLE_NAME_SERVICE_NAME)
            .addUseCallback(callback3)
            .build();
    EXPECT_EQ(2, count);
}

/* This will not work when using dlopen and the reuse of already loaded libaries with the same SO_NAME.
TEST_F(GetBundleContextTestSuite, GetBundleContextWithinServiceWith2Frameworks) {
    long bndId1 = ctx->installBundle(GET_BUNDLECONTEXT_TEST_BUNDLE1);
    ASSERT_GT(bndId1, 0);
    auto callback1 = [](get_bundle_name_service &svc) {
        char *name = svc.getBundleName();
        EXPECT_STREQ("get_bundle_context_test_bundle1", name);
        free(name);
    };

    //rule: using celix_getBundleContext() should return the bundle context of bundle which calling code belongs to.
    auto count = ctx->useServices<get_bundle_name_service>(GET_BUNDLE_NAME_SERVICE_NAME)
            .addUseCallback(callback1)
            .build();
    EXPECT_EQ(1, count);

    auto fw2 = celix::createFramework(celix::Properties{{
                                                                celix::FRAMEWORK_STORAGE, ".cache_fw2"
                                                        }});
    auto ctx2 = fw2->getFrameworkBundleContext();
    long bndId2 = ctx2->installBundle(GET_BUNDLECONTEXT_TEST_BUNDLE2);
    ASSERT_GT(bndId2, 0);

    //rule installing a bundle in another framework should not impact the bundles from first framework
    count = ctx->useServices<get_bundle_name_service>(GET_BUNDLE_NAME_SERVICE_NAME)
            .addUseCallback(callback1)
            .build();
    EXPECT_EQ(1, count);


    auto callback2 = [](get_bundle_name_service &svc) {
        char *name = svc.getBundleName();
        EXPECT_STREQ("get_bundle_context_test_bundle2", name);
        free(name);
    };
    count = ctx2->useServices<get_bundle_name_service>(GET_BUNDLE_NAME_SERVICE_NAME)
            .addUseCallback(callback2)
            .build();
    EXPECT_EQ(1, count);
}*/