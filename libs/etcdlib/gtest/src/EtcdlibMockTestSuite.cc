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

extern "C" {
    CURLcode __wrap_curl_global_init(int flags) {
        (void)flags;
        return CURLE_OK;
    }

    const char* g_etcdlibMockedReplyData = nullptr;
    const char* g_etcdlibMockedReplyHeader = nullptr;

    CURLcode __wrap_curl_easy_perform(CURL* curl) {
        etcdlib_reply_data_t* data;
        CURLcode cc = curl_easy_getinfo(curl, CURLINFO_PRIVATE, (char**)&data);
        if (cc != CURLE_OK) {
            return cc;
        }
        if (data) {
            data->memory = g_etcdlibMockedReplyData ? strdup(g_etcdlibMockedReplyData) : nullptr;
            data->header = g_etcdlibMockedReplyHeader ? strdup(g_etcdlibMockedReplyHeader) : nullptr;
        }
        return CURLE_OK;
    }
}

/**
 * @brief Test suite for the etcdlib using the linker wrap to mock the curl calls.
 */
class EtcdlibMockTestSuite : public ::testing::Test {
public:
    EtcdlibMockTestSuite() {
        g_etcdlibMockedReplyData = nullptr;
        g_etcdlibMockedReplyHeader = nullptr;
    }

    ~EtcdlibMockTestSuite() override = default;
};

TEST_F(EtcdlibMockTestSuite, CreateWithCurlGlobalInitTest) {
    etcdlib_autoptr_t etcd = etcdlib_create("localhost", 2379, 0);
    ASSERT_TRUE(etcd != nullptr);
}

TEST_F(EtcdlibMockTestSuite, GetEtcdEntryTest) {
    //Given an etcdlib instance with no curl multi handle
    etcdlib_create_options_t opts = {};
    etcdlib_autoptr_t etcdlib = nullptr;
    etcdlib_result_t res = etcdlib_createWithOptions(&opts, &etcdlib);
    ASSERT_EQ(ETCDLIB_RC_OK, res.rc);

    //When preparing a curl easy handle mocked reply and header
    g_etcdlibMockedReplyData = R"({"node": {"value": "test", "modifiedIndex": 1}})";
    g_etcdlibMockedReplyHeader = R"(HTTP/1.1 200 OK; Content-Type: application/json; X-Etcd-Index: 1)";

    //Then etcdlib_get should return the value and the index
    char* value = nullptr;
    long long index;
    int rc = etcdlib_get(etcdlib, "/test", &value, &index);
    ASSERT_EQ(ETCDLIB_RC_OK, rc);
    ASSERT_STREQ("test", value);
    ASSERT_EQ(1, index);
}
