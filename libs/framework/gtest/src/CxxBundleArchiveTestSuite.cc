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
#include "bundle_archive.h"
#include "celix_constants.h"
#include "celix_file_utils.h"

//declare private functions used to test the bundle archive
extern "C" bundle_archive_t* celix_bundle_getArchive(celix_bundle_t *bundle);
extern "C" celix_status_t celix_bundleArchive_getLastModified(bundle_archive_pt archive, struct timespec* lastModified);

class CxxBundleArchiveTestSuite : public ::testing::Test {
public:
};

/**
 * Test == operator to compare timespec values
 */
bool operator==(const timespec& lhs, const timespec& rhs) {
    return lhs.tv_sec == rhs.tv_sec && lhs.tv_nsec == rhs.tv_nsec;
}

/**
 * Test != operator to compare timespec values
 */
bool operator!=(const timespec& lhs, const timespec& rhs) {
    return !(lhs == rhs);
}

TEST_F(CxxBundleArchiveTestSuite, BundleArchiveReusedTest) {
    auto fw = celix::createFramework({
         {"CELIX_LOGGING_DEFAULT_ACTIVE_LOG_LEVEL", "trace"},
         {CELIX_FRAMEWORK_CACHE_ALWAYS_UPDATE_BUNDLE_ARCHIVES, "false"}
    });
    auto ctx = fw->getFrameworkBundleContext();

    std::mutex m; //protects installTime
    timespec installTime{};

    auto tracker = ctx->trackBundles()
        .addOnInstallCallback([&](const celix::Bundle& b) {
            std::lock_guard<std::mutex> lock{m};
            auto *archive = celix_bundle_getArchive(b.getCBundle());
            EXPECT_EQ(CELIX_SUCCESS, celix_bundleArchive_getLastModified(archive, &installTime));
        }).build();

    long bndId1 = ctx->installBundle(SIMPLE_TEST_BUNDLE1_LOCATION);
    EXPECT_GT(bndId1, -1);

    std::unique_lock<std::mutex> lock{m};
    EXPECT_GT(installTime.tv_sec, 0);
    auto firstBundleRevisionTime = installTime;
    lock.unlock();

    //uninstall and reinstall
    ctx->uninstallBundle(bndId1);
    std::this_thread::sleep_for(std::chrono::milliseconds{100}); //wait so that the zip <-> archive dir modification time is different
    long bndId2 = ctx->installBundle(SIMPLE_TEST_BUNDLE1_LOCATION);
    EXPECT_GT(bndId2, -1);
    EXPECT_EQ(bndId1, bndId2); //bundle id should be reused.

    lock.lock();
    EXPECT_GT(installTime.tv_sec, 0);
    //bundle archive should not be updated
    EXPECT_EQ(installTime, firstBundleRevisionTime);
    lock.unlock();


    auto secondBundleRevisionTime = installTime;
    ctx->uninstallBundle(bndId1);
    std::this_thread::sleep_for(std::chrono::milliseconds{100}); //wait so that the zip <-> archive dir modification time is different
    celix_utils_touch(SIMPLE_TEST_BUNDLE1_LOCATION); //touch the bundle zip file to force an update
    long bndId3 = ctx->installBundle(SIMPLE_TEST_BUNDLE1_LOCATION);
    EXPECT_GT(bndId3, -1);
    EXPECT_EQ(bndId1, bndId3); //bundle id should be reused.

    lock.lock();
    EXPECT_GT(installTime.tv_sec, 0);
    //bundle archive should be updated, because the zip file is touched
    EXPECT_NE(installTime, secondBundleRevisionTime);
    lock.unlock();
}

TEST_F(CxxBundleArchiveTestSuite, BundleArchiveAlwaysUpdatedTest) {
    auto fw = celix::createFramework({
        {"CELIX_LOGGING_DEFAULT_ACTIVE_LOG_LEVEL", "trace"},
        {CELIX_FRAMEWORK_CACHE_ALWAYS_UPDATE_BUNDLE_ARCHIVES, "true"}
    });
    auto ctx = fw->getFrameworkBundleContext();

    std::mutex m; //protects installTime
    timespec installTime{};

    auto tracker = ctx->trackBundles()
            .addOnInstallCallback([&](const celix::Bundle& b) {
                std::lock_guard<std::mutex> lock{m};
                auto *archive = celix_bundle_getArchive(b.getCBundle());
                EXPECT_EQ(CELIX_SUCCESS, celix_bundleArchive_getLastModified(archive, &installTime));
            }).build();

    long bndId1 = ctx->installBundle(SIMPLE_TEST_BUNDLE1_LOCATION);
    EXPECT_GT(bndId1, -1);

    std::unique_lock<std::mutex> lock{m};
    EXPECT_GT(installTime.tv_sec, 0);
    auto firstBundleRevisionTime = installTime;
    lock.unlock();

    //uninstall and reinstall
    ctx->uninstallBundle(bndId1);
    std::this_thread::sleep_for(std::chrono::milliseconds{100}); //wait so that the zip <-> archive dir modification time  is different
    long bndId2 = ctx->installBundle(SIMPLE_TEST_BUNDLE1_LOCATION);
    EXPECT_GT(bndId2, -1);
    EXPECT_EQ(bndId1, bndId2); //bundle id should be reused.

    lock.lock();
    EXPECT_GT(installTime.tv_sec, 0);
    //bundle archive should be updated
    EXPECT_NE(installTime, firstBundleRevisionTime);
    lock.unlock();
}

TEST_F(CxxBundleArchiveTestSuite, BundleArchivesCreatedBeforeStarting) {
    //TODO
}
