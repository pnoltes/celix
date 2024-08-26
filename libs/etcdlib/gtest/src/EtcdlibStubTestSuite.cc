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

#include <civetweb.h>
#include <memory>
#include <string>
#include <mutex>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>

struct MgTestContext {
    constexpr static unsigned int randomSeed = 0x12345678;

    std::mutex mutex{}; // protects below
    int httpErrorCode{0}; // if > 0, the http error code to return
    std::string expectedUrl{}; // if empty, no expected url is checked
    std::string expectedMethod{}; // if empty, no expected method is checked
    std::string expectedData{}; // if empty, no expected data is checked
    std::string replyMineType{}; // if empty, application/json is used
    std::string sendData{};
    std::string replyData{};
    std::string replyEtcdIndex{}; //if empty, no X-Etcd-Index header is added
    int msSleep{0}; // if > 0, sleep for msSleep + random 0-5 milliseconds per request before replying

    void clear() {
        const std::lock_guard<std::mutex> lock{mutex};
        httpErrorCode = 0;
        expectedUrl.clear();
        expectedMethod.clear();
        expectedData.clear();
        replyMineType.clear();
        sendData.clear();
        replyData.clear();
        replyEtcdIndex.clear();
        msSleep = 0;
    }
};

/**
 * @brief Test suite for the etcdlib using a etcd stub implemented with civetweb
 */
class EtcdlibStubTestSuite : public ::testing::Test {
  public:
    static constexpr int port = 52379;

    EtcdlibStubTestSuite() {
        mgTestCtx->clear();
    }

    ~EtcdlibStubTestSuite() override = default;

    static void SetUpTestSuite() {
        if (!mgCtx) {
            auto* ctx = createCivetwebServer();
            ASSERT_NE(ctx, nullptr) << "Error creating civetweb server";
            mgCtx = std::shared_ptr<mg_context>{ctx, [](mg_context* c) { mg_stop(c); }};
        }
    }

    static mg_context* createCivetwebServer() {
        mgTestCtx = std::make_unique<MgTestContext>();
        srand(MgTestContext::randomSeed);
        const char* civetwebOptions[] = {"listening_ports", "52379", "num_threads", "10", nullptr};
        mg_callbacks callbacks{};
        callbacks.log_message = [](const mg_connection* conn, const char* message) -> int {
            (void)conn;
            fprintf(stderr, "Civetweb: %s\n", message);
            return 0;
        };
        callbacks.begin_request = [](mg_connection* conn) -> int {
            return EtcdlibStubTestSuite::stubbedBeginRequest(conn);
        };
        return mg_start(&callbacks, nullptr, civetwebOptions);
    }

    static void TearDownTestSuite() {
        mgCtx.reset();
    }

    static int stubbedBeginRequest(mg_connection* conn) {
        auto* ctx = mgTestCtx.get();
        std::unique_lock<std::mutex> lock{ctx->mutex};
        const auto* rInfo = mg_get_request_info(conn);

        if (ctx->httpErrorCode > 0) {
            mg_send_http_error(conn, ctx->httpErrorCode, "Error");
            return 1;
        }

        if (!ctx->expectedMethod.empty()) {
            EXPECT_STREQ(ctx->expectedMethod.c_str(), rInfo->request_method) << "Unexpected method";
        }

        if (!ctx->expectedUrl.empty()) {
            EXPECT_STREQ(ctx->expectedUrl.c_str(), rInfo->request_uri) << "Unexpected url";
        }

        if (!mgTestCtx->expectedData.empty()) {
            char buffer[1024]; //Note assuming that 1024 is enough for the request body used in this test suite
            mg_read(conn, buffer, sizeof(buffer));
            EXPECT_STREQ(mgTestCtx->expectedData.c_str(), buffer) << "Unexpected data";
        }

        mg_response_header_start(conn, 200);
        const auto mimeType = ctx->replyData.empty() ? "application/json" : ctx->replyMineType;
        mg_response_header_add(conn, "Content-Type", mimeType.c_str(), -1);
        if (!ctx->replyEtcdIndex.empty()) {
            mg_response_header_add(conn, "X-Etcd-Index", ctx->replyEtcdIndex.c_str(), -1);
        }
        mg_response_header_send(conn);
        mg_write(conn, ctx->replyData.c_str(), ctx->replyData.size());

        const int msSleep  = ctx->msSleep;
        lock.unlock();

        if (msSleep > 0) {
            const int msRand = (5 * rand()) / RAND_MAX;
            usleep((msSleep + msRand) * 1000);
        }

        return 1;
    }

    static etcdlib_t* createEtcdlib() {
        //Given an etcdlib instance with no curl multi handle and port 52379
        etcdlib_create_options_t opts = {};
        opts.port = port;
        etcdlib_t* etcdlib = nullptr;
        auto rc = etcdlib_createWithOptions(&opts, &etcdlib);
        EXPECT_EQ(ETCDLIB_RC_OK, rc);
        return etcdlib;
    }

    static etcdlib_t* createEtcdlibWithCurlMulti() {
        //Given an etcdlib instance with curl multi handle and port 52379
        etcdlib_create_options_t opts = {};
        opts.port = port;
        opts.useMultiCurl = true;
        etcdlib_t* etcdlib = nullptr;
        auto rc = etcdlib_createWithOptions(&opts, &etcdlib);
        EXPECT_EQ(ETCDLIB_RC_OK, rc);
        return etcdlib;
    }

    static void getEtcdEntryTest(etcdlib_t* etcdlib) {
        //When preparing an etcd stubbed reply
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->expectedMethod = "GET";
            mgTestCtx->expectedUrl = "/v2/keys/test";
            mgTestCtx->replyData = R"({"node": {"value": "test"}})";
            mgTestCtx->replyEtcdIndex = "1";
        }

        //Then etcdlib_get should return the value and the index
        char* value = nullptr;
        long long index;
        auto rc = etcdlib_get(etcdlib, "test", &value, &index);
        ASSERT_EQ(ETCDLIB_RC_OK, rc);
        ASSERT_STREQ("test", value);
        ASSERT_EQ(1, index);
        free(value);

        //When preparing an etcd stubbed reply without an X-Etcd-Index header
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->replyEtcdIndex = "";
        }

        //Then etcdlib_get should return the value and a -1 index
        rc = etcdlib_get(etcdlib, "test", &value, &index);
        ASSERT_EQ(ETCDLIB_RC_OK, rc);
        ASSERT_STREQ("test", value);
        ASSERT_EQ(-1, index);
        free(value);
    }

    static void getEtcdEntryParallelTest(etcdlib_t* etcdlib) {
        //When preparing an etcd stubbed reply
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->expectedMethod = "GET";
            mgTestCtx->expectedUrl = "/v2/keys/test";
            mgTestCtx->replyData = R"({"node": {"value": "test"}})";
            mgTestCtx->replyEtcdIndex = "1";
            mgTestCtx->msSleep = 1;
        }

        //Then 1000x etcdlib_get should return the value and the index
        for (int i = 0; i < 1000; ++i) {
            char* value = nullptr;
            long long index;
            auto rc = etcdlib_get(etcdlib, "test", &value, &index);
            ASSERT_EQ(ETCDLIB_RC_OK, rc);
            ASSERT_STREQ("test", value);
            ASSERT_EQ(1, index);
            free(value);
        }
    }

    static void getEtcdEntryWithServerFailureTest(etcdlib_t* etcdlib) {
        //When preparing an etcd stubbed error reply leading to a 405 error
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->httpErrorCode = 405;
        }

        //Then etcdlib_get should return an error
        char* value = nullptr;
        long long index;
        auto rc = etcdlib_get(etcdlib, "test", &value, &index);
        ASSERT_NE(rc, 0);
        ASSERT_TRUE(ETCDLIB_INTERNAL_HTTPCODE_FLAG & rc);
        ASSERT_EQ(etcdlib_getHttpCodeFromStatus(rc), 405);
        ASSERT_EQ(value, nullptr);
        ASSERT_EQ(index, -1);

        //When preparing an etcd stubbed error reply leading to a 404 error
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->httpErrorCode = 404;
        }

        //Then etcdlib_get should return an error
        rc = etcdlib_get(etcdlib, "test", &value, &index);
        ASSERT_NE(rc, 0);
        ASSERT_TRUE(ETCDLIB_INTERNAL_HTTPCODE_FLAG & rc);
        ASSERT_EQ(etcdlib_getHttpCodeFromStatus(rc), 404);
        ASSERT_EQ(value, nullptr);
        ASSERT_EQ(index, -1);

        //When preparing an etcd stubbed reply with an invalid etcd index
        {
            mgTestCtx->clear();
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->expectedMethod = "GET";
            mgTestCtx->expectedUrl = "/v2/keys/test";
            mgTestCtx->replyData = R"({"node": {"value": "test"}})";
            mgTestCtx->replyEtcdIndex = "not-a-number";
        }

        //Then etcdlib_get should return an ok, but a -1 index
        rc = etcdlib_get(etcdlib, "test", &value, &index);
        ASSERT_EQ(rc, ETCDLIB_RC_OK);
        ASSERT_STREQ(value, "test");
        ASSERT_EQ(index, -1);
        free(value);
    }

    static void getEtcdEntryWithInvalidContentTest(etcdlib_t* etcdlib) {
        //When preparing a stubbed reply non-etcd reply
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->expectedMethod = "GET";
            mgTestCtx->expectedUrl = "/v2/keys/test";
            mgTestCtx->replyData = "plain text response. i.e. a reply of a non-etcd server";
            mgTestCtx->replyMineType = "text/plain";
            mgTestCtx->replyEtcdIndex = "1";
        }

        //Then etcdlib_get should return an invalid content error
        char* value = nullptr;
        long long index;
        auto rc = etcdlib_get(etcdlib, "test", &value, &index);
        ASSERT_EQ(rc, ETCDLIB_RC_INVALID_RESPONSE_CONTENT);
        ASSERT_EQ(value, nullptr);
        ASSERT_EQ(index, -1);

        //When preparing a stubbed reply with invalid json
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->replyData = R"({"node": {}})"; // missing value
            mgTestCtx->replyMineType = "application/json";
        }

        //Then etcdlib_get should return an invalid content error
        rc = etcdlib_get(etcdlib, "test", &value, &index);
        ASSERT_EQ(rc, ETCDLIB_RC_INVALID_RESPONSE_CONTENT);
        ASSERT_EQ(value, nullptr);
        ASSERT_EQ(index, -1);

        // When preparing a stubbed reply with invalid json
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->replyData = "{}"; // missing node (and value)
            mgTestCtx->replyMineType = "application/json";
        }

        // Then etcdlib_get should return an invalid content error
        rc = etcdlib_get(etcdlib, "test", &value, &index);
        ASSERT_EQ(rc, ETCDLIB_RC_INVALID_RESPONSE_CONTENT);
        ASSERT_EQ(value, nullptr);
        ASSERT_EQ(index, -1);
    }

    static void setEtcdEntryTest(etcdlib_t* etcdlib) {
        //When preparing an etcd stubbed reply
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->expectedMethod = "PUT";
            mgTestCtx->expectedUrl = "/v2/keys/test";
            mgTestCtx->expectedData = "value=myValue";
            mgTestCtx->replyData = R"({"node": {"value": "myValue"}})";
        }

        //Then etcdlib_set should return ok
        auto rc = etcdlib_set(etcdlib, "test", "myValue", 0, false);
        ASSERT_EQ(ETCDLIB_RC_OK, rc);

        //When preparing an etcd stubbed reply, including a ttl and prevExist
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->expectedData = "value=myValue;ttl=10;prevExist=true";
        }

        //Then etcdlib_set should return ok
        rc = etcdlib_set(etcdlib, "test", "myValue", 10, true);
        ASSERT_EQ(ETCDLIB_RC_OK, rc);

        //When preparing an etcd stubbed reply, including a ttl
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->expectedData = "value=myValue;ttl=10";
        }

        //Then etcdlib_set should return ok
        rc = etcdlib_set(etcdlib, "test", "myValue", 10, false);
        ASSERT_EQ(ETCDLIB_RC_OK, rc);

        //When preparing an etcd stubbed reply, including a prevExists
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->expectedData = "value=myValue;prevExist=true";
        }

        //Then etcdlib_set should return ok
        rc = etcdlib_set(etcdlib, "test", "myValue", 0, true);
        ASSERT_EQ(ETCDLIB_RC_OK, rc);
    }

    static void setEtcdEntryWithInvalidReplyTest(etcdlib_t* etcdlib) {
        //When preparing a stubbed reply non-etcd reply
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->expectedMethod = "PUT";
            mgTestCtx->expectedUrl = "/v2/keys/test";
            mgTestCtx->replyData = "plain text response. i.e. a reply of a non-etcd server";
            mgTestCtx->replyMineType = "text/plain";
            mgTestCtx->replyEtcdIndex = "1";
        }

        //Then etcdlib_set should return an invalid content error
        auto rc = etcdlib_set(etcdlib, "test", "myValue", 0, true);
        EXPECT_EQ(rc, ETCDLIB_RC_INVALID_RESPONSE_CONTENT);

        //When preparing a stubbed reply with invalid json
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->replyData = R"({"node": {}})"; // missing value
            mgTestCtx->replyMineType = "application/json";
        }

        //Then etcdlib_set should return an invalid content error
        rc = etcdlib_set(etcdlib, "test", "myValue", 0, true);
        EXPECT_EQ(rc, ETCDLIB_RC_INVALID_RESPONSE_CONTENT);

        // When preparing a stubbed reply with invalid json
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->replyData = "{}"; // missing node (and value)
            mgTestCtx->replyMineType = "application/json";
        }

        //Then etcdlib_set should return an invalid content error
        rc = etcdlib_set(etcdlib, "test", "myValue", 0, true);
        EXPECT_EQ(rc, ETCDLIB_RC_INVALID_RESPONSE_CONTENT);
    }

    static void refreshEtcdEntryTest(etcdlib_t* etcdlib) {
        //When preparing an etcd stubbed reply
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->expectedMethod = "PUT";
            mgTestCtx->expectedUrl = "/v2/keys/test";
            mgTestCtx->expectedData = "prevExist=true;refresh=true;ttl=10";
            mgTestCtx->replyData =
                R"({"action":"update","node":{"key":"/test","value":"val1","ttl":1,"modifiedIndex":1,"createdIndex":1},"prevNode":{"key":"/test","value":"val1","ttl":10,"modifiedIndex":2,"createdIndex":1}})";
        }

        //Then etcdlib_refresh should return ok
        auto rc = etcdlib_refresh(etcdlib, "test", 10);
        ASSERT_EQ(ETCDLIB_RC_OK, rc);
    }

    static void deleteEtcdEntryTest(etcdlib_t* etcdlib) {
        //When preparing an etcd stubbed reply
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->expectedMethod = "DELETE";
            mgTestCtx->expectedUrl = "/v2/keys/test";
            mgTestCtx->replyData =
                R"({"action":"delete","node":{"createdIndex":1,"key":"/test","modifiedIndex":2},"prevNode":{"createdIndex":1,"key":"/test","value":"val1","modifiedIndex":1}})";
        }

        //Then etcdlib_refresh should return ok
        auto rc = etcdlib_del(etcdlib, "test");
        ASSERT_EQ(ETCDLIB_RC_OK, rc);
    }

  protected:
    static std::shared_ptr<mg_context> mgCtx;
    static std::unique_ptr<MgTestContext> mgTestCtx;
};

std::shared_ptr<mg_context> EtcdlibStubTestSuite::mgCtx{};
std::unique_ptr<MgTestContext> EtcdlibStubTestSuite::mgTestCtx{};

TEST_F(EtcdlibStubTestSuite, GetEtcdEntryTest) {
    //Given an etcdlib instance
    etcdlib_autoptr_t etcdlib = createEtcdlib();
    EXPECT_NE(etcdlib, nullptr);
    getEtcdEntryTest(etcdlib);

    //Given an etcdlib instance with curl multi handle
    etcdlib_autoptr_t etcdlib2 = createEtcdlibWithCurlMulti();
    EXPECT_NE(etcdlib2, nullptr);
    getEtcdEntryTest(etcdlib2);
}

TEST_F(EtcdlibStubTestSuite, GetEtcdEntryParrallelTest) {
    //Given an etcdlib instance
    etcdlib_autoptr_t etcdlib = createEtcdlib();
    EXPECT_NE(etcdlib, nullptr);
    getEtcdEntryParallelTest(etcdlib);

    //Given an etcdlib instance with curl multi handle
    etcdlib_autoptr_t etcdlib2 = createEtcdlibWithCurlMulti();
    EXPECT_NE(etcdlib2, nullptr);
    getEtcdEntryParallelTest(etcdlib2);
}

TEST_F(EtcdlibStubTestSuite, GetEtcdEntryWithServerFailureTest) {
    //Given an etcdlib instance
    etcdlib_autoptr_t etcdlib = createEtcdlib();
    EXPECT_NE(etcdlib, nullptr);
    getEtcdEntryWithServerFailureTest(etcdlib);

    //Given an etcdlib instance with curl multi handle
    etcdlib_autoptr_t etcdlib2 = createEtcdlibWithCurlMulti();
    EXPECT_NE(etcdlib2, nullptr);
    getEtcdEntryWithServerFailureTest(etcdlib2);
}

TEST_F(EtcdlibStubTestSuite, GetEtcdEntryWithInvalidContentTest) {
    //Given an etcdlib instance
    etcdlib_autoptr_t etcdlib = createEtcdlib();
    EXPECT_NE(etcdlib, nullptr);
    getEtcdEntryWithInvalidContentTest(etcdlib);

    //Given an etcdlib instance with curl multi handle
    etcdlib_autoptr_t etcdlib2 = createEtcdlibWithCurlMulti();
    EXPECT_NE(etcdlib2, nullptr);
    getEtcdEntryWithInvalidContentTest(etcdlib2);
}

TEST_F(EtcdlibStubTestSuite, SetEtcdEntryTest) {
    //Given an etcdlib instance
    etcdlib_autoptr_t etcdlib = createEtcdlib();
    EXPECT_NE(etcdlib, nullptr);
    setEtcdEntryTest(etcdlib);

    //Given an etcdlib instance with curl multi handle
    etcdlib_autoptr_t etcdlib2 = createEtcdlibWithCurlMulti();
    EXPECT_NE(etcdlib2, nullptr);
    setEtcdEntryTest(etcdlib2);
}

TEST_F(EtcdlibStubTestSuite, SetEtcdEntryWithInvalidReplyTest) {
    //Given an etcdlib instance
    etcdlib_autoptr_t etcdlib = createEtcdlib();
    EXPECT_NE(etcdlib, nullptr);
    setEtcdEntryWithInvalidReplyTest(etcdlib);

    //Given an etcdlib instance with curl multi handle
    etcdlib_autoptr_t etcdlib2 = createEtcdlibWithCurlMulti();
    EXPECT_NE(etcdlib2, nullptr);
    setEtcdEntryWithInvalidReplyTest(etcdlib2);
}

TEST_F(EtcdlibStubTestSuite, RefreshEtcdEntryTest) {
    //Given an etcdlib instance
    etcdlib_autoptr_t etcdlib = createEtcdlib();
    EXPECT_NE(etcdlib, nullptr);
    refreshEtcdEntryTest(etcdlib);

    //Given an etcdlib instance with curl multi handle
    etcdlib_autoptr_t etcdlib2 = createEtcdlibWithCurlMulti();
    EXPECT_NE(etcdlib2, nullptr);
    refreshEtcdEntryTest(etcdlib2);
}

//TODO test refresh with invalid content reply

TEST_F(EtcdlibStubTestSuite, DeleteEtcdEntryTest) {
    //Given an etcdlib instance
    etcdlib_autoptr_t etcdlib = createEtcdlib();
    EXPECT_NE(etcdlib, nullptr);
    deleteEtcdEntryTest(etcdlib);

    //Given an etcdlib instance with curl multi handle
    etcdlib_autoptr_t etcdlib2 = createEtcdlibWithCurlMulti();
    EXPECT_NE(etcdlib2, nullptr);
    deleteEtcdEntryTest(etcdlib2);
}

//TODO test delete with invalid content reply

//TODO test watch
