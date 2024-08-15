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

class EtcdlibTestSuite : public ::testing::Test {};

TEST_F(EtcdlibTestSuite, CreateDestroyTest) {
    auto* lib = etcdlib_create("localhost", 2379, ETCDLIB_NO_CURL_INITIALIZATION);
    ASSERT_NE(lib, nullptr);
    etcdlib_destroy(lib);
}

TEST_F(EtcdlibTestSuite, GetHostAndPortTest) {
    auto* lib1 = etcdlib_create("localhost", 2379, ETCDLIB_NO_CURL_INITIALIZATION);
    auto* lib2 = etcdlib_create("my_host", 1234, ETCDLIB_NO_CURL_INITIALIZATION);
    ASSERT_NE(lib1, nullptr);
    ASSERT_NE(lib2, nullptr);

    EXPECT_STREQ(etcdlib_host(lib1), "localhost");
    EXPECT_EQ(etcdlib_port(lib1), 2379);

    EXPECT_STREQ(etcdlib_host(lib2), "my_host");
    EXPECT_EQ(etcdlib_port(lib2), 1234);

    etcdlib_destroy(lib1);
    etcdlib_destroy(lib2);
}

TEST_F(EtcdlibTestSuite, EtcdlibAutoPtrTest) {
    etcdlib_autoptr_t lib = etcdlib_create("localhost", 2379, ETCDLIB_NO_CURL_INITIALIZATION);
    ASSERT_NE(lib, nullptr);
    // note no destroy, the autoptr should handle this

    etcdlib_autoptr_t lib2 = etcdlib_create("localhost", 2379, ETCDLIB_NO_CURL_INITIALIZATION);
    ASSERT_NE(lib2, nullptr);
    etcdlib_t* stolenPtr = etcdlib_steal_ptr(lib2);
    ASSERT_EQ(lib2, nullptr);
    etcdlib_destroy(stolenPtr); // note lib2 is now a nullptr and therefore stolenPtr is the owner
}

TEST_F(EtcdlibTestSuite, CreateWithOptionsTest) {
    etcdlib_create_options_t opts{};
    etcdlib_autoptr_t lib1 = nullptr;
    auto rc = etcdlib_createWithOptions(&opts, &lib1);
    ASSERT_EQ(rc, ETCDLIB_RC_OK);
    ASSERT_NE(lib1, nullptr);

    opts.useMultiCurl = true;
    opts.server = "foo";
    opts.port = 1234;
    etcdlib_autoptr_t lib2 = nullptr;
    rc = etcdlib_createWithOptions(&opts, &lib2);
    ASSERT_EQ(rc, ETCDLIB_RC_OK);
    ASSERT_NE(lib2, nullptr);
    EXPECT_STREQ(etcdlib_host(lib2), "foo");
    EXPECT_EQ(etcdlib_port(lib2), 1234);
}

TEST_F(EtcdlibTestSuite, StatusStrErrorTest) {
    EXPECT_STREQ(etcdlib_strerror(ETCDLIB_RC_OK), "ETCDLIB OK");
    EXPECT_STREQ(etcdlib_strerror(ETCDLIB_RC_TIMEOUT), "ETCDLIB Timeout");
    EXPECT_STREQ(etcdlib_strerror(ETCDLIB_RC_EVENT_CLEARED), "ETCDLIB Event Cleared");
    EXPECT_STREQ(etcdlib_strerror(ETCDLIB_RC_ENOMEM), "ETCDLIB Out of memory or maximum number of curl handles reached");
    EXPECT_STREQ(etcdlib_strerror(42), "ETCDLIB Unknown error");

    //curlcode error
    const char* error = etcdlib_strerror(ETCDLIB_INTERNAL_CURLCODE_FLAG | CURLE_OPERATION_TIMEDOUT);
    EXPECT_TRUE(error != nullptr);
    EXPECT_TRUE(strcasecmp(error, "timeout"));

    //curlmcode error
    error = etcdlib_strerror(ETCDLIB_INTERNAL_CURLMCODE_FLAG | CURLM_BAD_SOCKET);
    EXPECT_TRUE(error != nullptr);
    EXPECT_TRUE(strcasecmp(error, "socker"));
}
