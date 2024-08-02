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
    etcdlib_result_t res = etcdlib_createWithOptions(&opts, &lib1);
    ASSERT_EQ(res.rc, ETCDLIB_RC_OK);
    ASSERT_NE(lib1, nullptr);

    opts.useMultiCurl = true;
    opts.server = "foo";
    opts.port = 1234;
    etcdlib_autoptr_t lib2 = nullptr;
    res = etcdlib_createWithOptions(&opts, &lib2);
    ASSERT_EQ(res.rc, ETCDLIB_RC_OK);
    ASSERT_NE(lib2, nullptr);
    EXPECT_STREQ(etcdlib_host(lib2), "foo");
    EXPECT_EQ(etcdlib_port(lib2), 1234);
}
