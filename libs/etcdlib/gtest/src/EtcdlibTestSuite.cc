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
#include <jansson.h>

class EtcdlibTestSuite : public ::testing::Test {
public:
    EtcdlibTestSuite() = default;
    ~EtcdlibTestSuite() noexcept override = default;

    static void logMessage(void* data, const char* fmt, ...) {
        auto* count = static_cast<std::atomic<int>*>(data);
        const int c = count->fetch_add(1);

        (void)fprintf(stderr, "Error message nr %i: ", c);

        va_list args;
        va_start(args, fmt);
        (void)vfprintf(stderr, fmt, args);
        va_end(args);
        (void)fprintf(stderr, "\n");
    }

    static const char* createUrl(char* localBuf, size_t localBufSize, char** heapBufOut, const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        const char* url = etcdlib_createUrl(localBuf, localBufSize, heapBufOut, fmt, args);
        va_end(args);
        return url;
    }

    static std::atomic<int> errorMessageCount;
};

std::atomic<int> EtcdlibTestSuite::errorMessageCount{0};

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
    EXPECT_STREQ(etcdlib_strerror(ETCDLIB_RC_EVENT_INDEX_CLEARED), "ETCDLIB Event Index Cleared");
    EXPECT_STREQ(etcdlib_strerror(ETCDLIB_RC_ENOMEM), "ETCDLIB Out of memory or maximum number of curl handles reached");
    EXPECT_STREQ(etcdlib_strerror(ETCDLIB_RC_ETCD_ERROR), "ETCDLIB Etcd error");
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

TEST_F(EtcdlibTestSuite, ParseEtcdReplyTest) {
    //whitebox test for etcdlib_parseEtcdReply
    etcdlib_create_options_t opts{};
    opts.logErrorMessageData = &errorMessageCount;
    opts.logErrorMessageCallback = logMessage;
    etcdlib_autoptr_t etcdlib = nullptr;
    auto rc = etcdlib_createWithOptions(&opts, &etcdlib);
    ASSERT_EQ(ETCDLIB_RC_OK, rc);

    //When parsing a valid etcd reply
    etcdlib_reply_data_t reply{};
    reply.header = const_cast<char*>("X-Etcd-Index: 1234");
    reply.memory = const_cast<char*>(R"({"node": {"value": "test"}, "action": "get"})");
    json_auto_t* jsonRoot = nullptr;
    json_t* jsonNode = nullptr;
    const char* value = nullptr;
    long index = 0;
    rc = etcdlib_parseEtcdReply(etcdlib, &reply, "get", &jsonRoot, &jsonNode, &value, &index);

    //Then the reply should be parsed correctly
    EXPECT_EQ(ETCDLIB_RC_OK, rc);
    EXPECT_NE(jsonRoot, nullptr);
    EXPECT_NE(jsonNode, nullptr);
    EXPECT_STREQ(value, "test");
    EXPECT_EQ(index, 1234);
    EXPECT_EQ(0, errorMessageCount);

    //When parsing the reply with no checks and outputs
    rc = etcdlib_parseEtcdReply(etcdlib, &reply, nullptr, nullptr, nullptr, nullptr, nullptr);

    //Then the reply should be parsed correctly
    EXPECT_EQ(ETCDLIB_RC_OK, rc);

    //When parsing the reply with an invalid action
    json_auto_t* jsonRoot2 = nullptr;
    rc = etcdlib_parseEtcdReply(etcdlib, &reply, "set", &jsonRoot2, nullptr, nullptr, nullptr);

    //Then the reply returns an error code
    EXPECT_EQ(ETCDLIB_RC_INVALID_RESPONSE_CONTENT, rc);
    EXPECT_EQ(jsonRoot2, nullptr);
    EXPECT_EQ(1, errorMessageCount);

    //When parsing the reply with an invalid reply
    reply.memory = const_cast<char*>("plain text response");
    json_auto_t* jsonRoot3 = nullptr;
    rc = etcdlib_parseEtcdReply(etcdlib, &reply, "get", &jsonRoot3, nullptr, nullptr, nullptr);

    //Then the reply returns an error code
    EXPECT_EQ(ETCDLIB_RC_INVALID_RESPONSE_CONTENT, rc);
    EXPECT_EQ(2, errorMessageCount);

    //When parsing the reply with an invalid json
    reply.memory = const_cast<char*>(R"({"node":{}, "action": "get"})");
    json_auto_t* jsonRoot4 = nullptr;
    rc = etcdlib_parseEtcdReply(etcdlib, &reply, "get", &jsonRoot4, nullptr, &value, nullptr);

    //Then the reply returns an error code
    EXPECT_EQ(ETCDLIB_RC_INVALID_RESPONSE_CONTENT, rc);
    EXPECT_EQ(3, errorMessageCount);

    //When parsing the reply with an invalid json
    reply.memory = const_cast<char*>(R"({"action": "get"})");
    json_auto_t* jsonRoot5 = nullptr;
    rc = etcdlib_parseEtcdReply(etcdlib, &reply, "get", &jsonRoot4, &jsonNode, nullptr, nullptr);

    //Then the reply returns an error code
    EXPECT_EQ(ETCDLIB_RC_INVALID_RESPONSE_CONTENT, rc);
    EXPECT_EQ(4, errorMessageCount);

    //When parsing the reply with an invalid json
    reply.memory = const_cast<char*>("{}");
    json_auto_t* jsonRoot6 = nullptr;
    rc = etcdlib_parseEtcdReply(etcdlib, &reply, nullptr, &jsonRoot6, nullptr, &value, nullptr);

    //Then the reply returns an error code
    EXPECT_EQ(ETCDLIB_RC_INVALID_RESPONSE_CONTENT, rc);
    EXPECT_EQ(5, errorMessageCount);

    //When parsing the reply with an etcd error
    reply.memory = const_cast<char*>(R"({"errorCode": 100, "message": "error message"})");
    rc = etcdlib_parseEtcdReply(etcdlib, &reply, nullptr, nullptr, nullptr, nullptr, nullptr);

    //Then the reply returns an error code
    EXPECT_EQ(ETCDLIB_RC_ETCD_ERROR, rc);
    EXPECT_EQ(6, errorMessageCount);
}

TEST_F(EtcdlibTestSuite, CreateEtcdUrlTest) {
    //whitebox test for etcdlib_createUrl
    etcdlib_create_options_t opts{};
    etcdlib_autoptr_t etcdlib = nullptr;
    auto rc = etcdlib_createWithOptions(&opts, &etcdlib);
    ASSERT_EQ(ETCDLIB_RC_OK, rc);

    //When creating an url with a small buffer
    char localBuf[10];
    char* heapBuf = nullptr;
    const char* url = createUrl(localBuf, sizeof(localBuf), &heapBuf, "http://localhost:%i", 1234);

    //Then the url should be created correctly on the heap
    EXPECT_STREQ("http://localhost:1234", url);
    EXPECT_NE(url, localBuf);
    EXPECT_EQ(url, heapBuf);
    free(heapBuf);

    //When creating an url with a large enough buffer
    char localBuf2[100];
    char* heapBuf2 = nullptr;
    const char* url2 = createUrl(localBuf2, sizeof(localBuf2), &heapBuf2, "http://localhost:%i", 1234);

    //Then the url should be created correctly on the local buffer
    EXPECT_STREQ("http://localhost:1234", url2);
    EXPECT_EQ(url2, localBuf2);
    EXPECT_EQ(heapBuf2, nullptr);
}

//TODO test get, create, refresh, delete, and update with unknown host or invalid port.
