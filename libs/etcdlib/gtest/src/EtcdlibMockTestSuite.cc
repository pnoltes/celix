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

#include "etcdlib.h"
#include "etcdlib_private.h"
#include <curl/curl.h>
#include <mutex>
#include <queue>
#include <atomic>
#include <cstring>

extern "C" {

    std::atomic<int> g_curlGlobalInitCount{0};

    CURLcode __wrap_curl_global_init(int flags) {
        //no-op global init

        g_curlGlobalInitCount.fetch_add(1);

        (void)flags;
        return CURLE_OK;
    }
}

/**
 * @brief Test suite for the etcdlib using the linker wrap to mock the curl calls.
 */
class EtcdlibMockTestSuite : public ::testing::Test {
public:
    EtcdlibMockTestSuite() {
        g_curlGlobalInitCount = 0;
    }

    ~EtcdlibMockTestSuite() override = default;
};

TEST_F(EtcdlibMockTestSuite, CreateWithNoCurlGlobalInitTest) {
    EXPECT_EQ(g_curlGlobalInitCount, 0);
    etcdlib_autoptr_t etcd = etcdlib_create("localhost", 2379, ETCDLIB_NO_CURL_INITIALIZATION);
    EXPECT_EQ(g_curlGlobalInitCount, 0);
    ASSERT_TRUE(etcd != nullptr);

    const etcdlib_create_options_t opts{};
    etcdlib_autoptr_t etcd2 = nullptr;
    auto rc = etcdlib_createWithOptions(&opts, &etcd2);;
    EXPECT_EQ(rc, ETCDLIB_RC_OK);
    EXPECT_EQ(g_curlGlobalInitCount, 0);
}

TEST_F(EtcdlibMockTestSuite, CreateWithCurlGlobalInitTest) {
    EXPECT_EQ(g_curlGlobalInitCount, 0);
    etcdlib_autoptr_t etcd = etcdlib_create("localhost", 2379, 0);
    EXPECT_EQ(g_curlGlobalInitCount, 1);
    ASSERT_TRUE(etcd != nullptr);

    etcdlib_create_options_t opts{};
    opts.initializeCurl = true;
    etcdlib_autoptr_t etcd2 = nullptr;
    auto rc = etcdlib_createWithOptions(&opts, &etcd2);;
    EXPECT_EQ(rc, ETCDLIB_RC_OK);
    EXPECT_EQ(g_curlGlobalInitCount, 2);
}
