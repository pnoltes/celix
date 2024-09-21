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

#include <chrono>
#include <gtest/gtest.h>

#include "etcdlib.h"
#include "etcdlib_private.h"

#include <atomic>
#include <civetweb.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unistd.h>
#include <utility>

struct MgTestContext {
    constexpr static unsigned int randomSeed = 0x12345678;

    std::mutex mutex{}; // protects below
    int httpErrorCode{0}; // if > 0, the http error code to return
    std::string expectedUrl{}; // if empty, no expected url is checked
    std::string expectedQuery{}; // if empty, no expected query is checked
    std::string expectedMethod{}; // if empty, no expected method is checked
    std::string expectedData{}; // if empty, no expected data is checked
    std::string replyMineType{}; // if empty, application/json is used
    std::string sendData{};
    std::string replyData{};
    std::string replyEtcdIndex{}; //if empty, no X-Etcd-Index header is added
    int msSleep{0}; // if > 0, sleep for msSleep + random 0-5 milliseconds per request before replying
    std::future<void> sync{};
    std::future<void>* syncCanCompleteRequest{}; // if valid, wait for this future to complete before processing the request
    std::promise<void>* inRequestCallPromise{};

    void clear() {
        const std::lock_guard<std::mutex> lock{mutex};
        httpErrorCode = 0;
        expectedUrl.clear();
        expectedQuery.clear();
        expectedMethod.clear();
        expectedData.clear();
        replyMineType.clear();
        sendData.clear();
        replyData.clear();
        replyEtcdIndex.clear();
        msSleep = 0;
        syncCanCompleteRequest = {};
        inRequestCallPromise = {};

    }
};

/**
 * @brief Test suite for the etcdlib using a etcd stub implemented with civetweb
 */
class EtcdlibStubTestSuite : public ::testing::Test {
  public:
    static constexpr int port = 52379;
    static constexpr int httpsPort = 52377;

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
        const char* civetwebOptions[] = {
            "listening_ports", "52379", "num_threads", "10", "error_log_file", "civetweb.log", nullptr};
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

        if (ctx->inRequestCallPromise) {
            ctx->inRequestCallPromise->set_value();
        }

        if (ctx->sync.valid()) {
            ctx->sync.wait();
        }

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

        if (!ctx->expectedQuery.empty()) {
            EXPECT_STREQ(ctx->expectedQuery.c_str(), rInfo->query_string) << "Unexpected query";
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

        if (mgTestCtx->syncCanCompleteRequest) {
            mgTestCtx->syncCanCompleteRequest->wait();
            mgTestCtx->syncCanCompleteRequest = nullptr;
        }

        return 1;
    }

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

    static etcdlib_create_options_t createEtcdlibOptions() {
        etcdlib_create_options_t opts = {};
        opts.port = port;
        opts.timeoutInMs = 500;
        opts.logInvalidResponseReplyData = static_cast<void*>(&invalidContentLogCount);
        opts.logInvalidResponseReplyCallback = [](void* data, const char* reply) {
            auto* count = static_cast<std::atomic<int>*>(data);
            const int c  = count->fetch_add(1);
            (void)fprintf(stdout, "Invalid content nr %i: '%s'\n", c, reply);
        };
        opts.logErrorMessageData = static_cast<void*>(&errorMessageCount);
        opts.logErrorMessageCallback = logMessage;
        return opts;
    }

    static etcdlib_t* createEtcdlib() {
        //Given an etcdlib instance with no curl multi handle and port 52379
        etcdlib_create_options_t opts = createEtcdlibOptions();
        opts.mode = ETCDLIB_MODE_LOCAL_THREAD;
        etcdlib_t* etcdlib = nullptr;
        auto rc = etcdlib_createWithOptions(&opts, &etcdlib);
        EXPECT_EQ(ETCDLIB_RC_OK, rc);
        invalidContentLogCount = 0;
        errorMessageCount = 0;
        return etcdlib;
    }

    static etcdlib_t* createEtcdlibWithCurlMulti() {
        //Given an etcdlib instance with curl multi handle and port 52379
        etcdlib_create_options_t opts = createEtcdlibOptions();
        opts.mode = ETCDLIB_MODE_DEFAULT;
        etcdlib_t* etcdlib = nullptr;
        auto rc = etcdlib_createWithOptions(&opts, &etcdlib);
        EXPECT_EQ(ETCDLIB_RC_OK, rc);
        invalidContentLogCount = 0;
        errorMessageCount = 0;
        return etcdlib;
    }

    static void getEtcdEntryTest(etcdlib_t* etcdlib) {
        //When preparing an etcd stubbed reply
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->expectedMethod = "GET";
            mgTestCtx->expectedUrl = "/v2/keys/test";
            mgTestCtx->replyData = R"({"node": {"value": "test"}, "action": "get"})";
            mgTestCtx->replyEtcdIndex = "1";
        }

        //Then etcdlib_get should return the value and the index
        char* value = nullptr;
        long index;
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
            mgTestCtx->replyData = R"({"node": {"value": "test"}, "action": "get"})";
            mgTestCtx->replyEtcdIndex = "1";
            mgTestCtx->msSleep = 1;
        }

        //Then 1000x etcdlib_get should return the value and the index
        for (int i = 0; i < 1000; ++i) {
            char* value = nullptr;
            long index;
            auto rc = etcdlib_get(etcdlib, "test", &value, &index);
            ASSERT_EQ(ETCDLIB_RC_OK, rc);
            ASSERT_STREQ("test", value);
            ASSERT_EQ(1, index);
            free(value);
        }
        ASSERT_EQ(0, invalidContentLogCount);
    }

    static void getEtcdEntryWithServerFailureTest(etcdlib_t* etcdlib) {
        //When preparing an etcd stubbed error reply leading to a 405 error
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->httpErrorCode = 405;
        }

        //Then etcdlib_get should return an error
        char* value = nullptr;
        long index;
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
            mgTestCtx->replyData = R"({"node": {"value": "test"}, "action": "get"})";
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
        long index;
        auto rc = etcdlib_get(etcdlib, "test", &value, &index);
        ASSERT_EQ(rc, ETCDLIB_RC_INVALID_RESPONSE_CONTENT);
        ASSERT_EQ(value, nullptr);
        ASSERT_EQ(index, -1);
        ASSERT_EQ(1, errorMessageCount);
        ASSERT_EQ(1, invalidContentLogCount);

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
        ASSERT_EQ(2, errorMessageCount);
        ASSERT_EQ(2, invalidContentLogCount);
    }

    static void setEtcdEntryTest(etcdlib_t* etcdlib) {
        //When preparing an etcd stubbed reply
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->expectedMethod = "PUT";
            mgTestCtx->expectedUrl = "/v2/keys/test";
            mgTestCtx->expectedData = "value=myValue";
            mgTestCtx->replyData = R"({"node": {"value": "myValue"}, "action": "set"})";
        }

        //Then etcdlib_set should return ok
        auto rc = etcdlib_set(etcdlib, "test", "myValue", 0);
        ASSERT_EQ(ETCDLIB_RC_OK, rc);

        //When preparing an etcd stubbed reply, including a ttl
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->expectedData = "value=myValue;ttl=10";
        }

        //Then etcdlib_set should return ok
        rc = etcdlib_set(etcdlib, "test", "myValue", 10);
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
        auto rc = etcdlib_set(etcdlib, "test", "myValue", 0);
        EXPECT_EQ(rc, ETCDLIB_RC_INVALID_RESPONSE_CONTENT);
        ASSERT_EQ(1, errorMessageCount);
        ASSERT_EQ(1, invalidContentLogCount);
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
        auto rc = etcdlib_delete(etcdlib, "test");
        ASSERT_EQ(ETCDLIB_RC_OK, rc);
    }

    static void getEtcdDirTest(etcdlib_t* etcdlib) {
        //When preparing an etcd stubbed reply
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->expectedMethod = "GET";
            mgTestCtx->expectedUrl = "/v2/keys/test";

            mgTestCtx->replyData = R"({"node":{"nodes": [{"key":"test1","value": "value1"}, {"nodes" : [{"key":"test2", "value":"value2"}]}]}, "action": "get"})";
            mgTestCtx->replyEtcdIndex = "5";
        }

        //Then etcdlib_get should call the callbacks 2 times for the 2 leaf nodes.
        long index;
        std::atomic<int> count{0};
        auto callback = [](const char* key, const char* value, void *data)  {
            if (strcmp(key, "test1") == 0) {
                EXPECT_STREQ(value, "value1");
            } else if (strcmp(key, "test2") == 0) {
                EXPECT_STREQ(value, "value2");
            } else {
                FAIL() << "Unexpected key: " << key;
            }
            auto* c = static_cast<std::atomic<int>*>(data);
            c->fetch_add(1);
        };
        auto rc = etcdlib_getDir(etcdlib, "test", callback, &count, &index);
        ASSERT_EQ(ETCDLIB_RC_OK, rc);
        ASSERT_EQ(5, index);
        ASSERT_EQ(2, count);
    }

    static void watchEtcdDirTest(etcdlib_t* etcdlib) {
        //When preparing an etcd stubbed reply, set action
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->expectedMethod = "GET";
            mgTestCtx->expectedUrl = "/v2/keys/test";
            mgTestCtx->expectedQuery = "wait=true&recursive=true&waitIndex=10";
            mgTestCtx->replyData = R"(
{
    "action": "set",
    "node": {
        "createdIndex": 10,
        "key": "/test/key1",
        "modifiedIndex": 10,
        "value": "bar"
    }
}
)";
            mgTestCtx->replyEtcdIndex = "22";
        }

        //Then etcdlib_watchDir should return the set action
        const char* action = nullptr;
        char* key = nullptr;
        char* value = nullptr;
        char* prevValue = nullptr;
        bool isDir = false;
        long modifiedIndex = 0;
        auto rc = etcdlib_watchDir(etcdlib, "/test", 10, &action, &key, &value, &prevValue, &isDir, &modifiedIndex);
        ASSERT_EQ(ETCDLIB_RC_OK, rc);
        EXPECT_STREQ(ETCDLIB_ACTION_SET, action);
        EXPECT_STREQ("/test/key1", key);
        EXPECT_STREQ("bar", value);
        EXPECT_EQ(nullptr, prevValue);
        EXPECT_FALSE(isDir);
        EXPECT_EQ(10, modifiedIndex);
        free(key);
        free(value);

        //When preparing an etcd stubbed reply, update action
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->expectedQuery = "wait=true&recursive=true&waitIndex=11";
            mgTestCtx->replyData = R"(
{
    "action": "update",
    "node": {
        "createdIndex": 2,
        "key": "/test/key2",
        "modifiedIndex": 11,
        "value": "updated"
    },
    "prevNode": {
        "createdIndex": 2,
        "key": "/test/key2",
        "modifiedIndex": 4,
        "value": "original"
    }
}
)";
        }

        //Then etcdlib_watchDir should return the update action
        rc = etcdlib_watchDir(etcdlib, "/test", 11, &action, &key, &value, &prevValue, &isDir, &modifiedIndex);
        ASSERT_EQ(ETCDLIB_RC_OK, rc);
        EXPECT_STREQ(ETCDLIB_ACTION_UPDATE, action);
        EXPECT_STREQ("/test/key2", key);
        EXPECT_STREQ("updated", value);
        EXPECT_STREQ("original", prevValue);
        EXPECT_FALSE(isDir);
        EXPECT_EQ(11, modifiedIndex);
        free(key);
        free(value);
        free(prevValue);

        //When preparing an etcd stubbed reply, delete action
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->expectedQuery.clear();
            mgTestCtx->replyData = R"(
{
    "action": "delete",
    "node": {
        "createdIndex": 3,
        "key": "/test/key3",
        "modifiedIndex": 12
    },
    "prevNode": {
    	"key": "/test/key3",
    	"value": "test",
    	"modifiedIndex": 3,
    	"createdIndex": 3
    }
}

)";
        }

        //Then etcdlib_watchDir should return the delete action
        rc = etcdlib_watchDir(etcdlib, "/test", 12, &action, &key, &value, &prevValue, &isDir, &modifiedIndex);
        ASSERT_EQ(ETCDLIB_RC_OK, rc);
        EXPECT_STREQ(ETCDLIB_ACTION_DELETE, action);
        EXPECT_STREQ("/test/key3", key);
        EXPECT_EQ(nullptr, value);
        EXPECT_STREQ("test", prevValue);
        EXPECT_FALSE(isDir);
        EXPECT_EQ(12, modifiedIndex);
        free(key);
        free(value);
        free(prevValue);

        ///When preparing an etcd stubbed reply, compareAndSwap action
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->replyData = R"(
{
    "action": "compareAndSwap",
    "node": {
        "createdIndex": 4,
        "key": "/test/key4",
        "modifiedIndex": 13,
        "value": "two"
    },
    "prevNode": {
    	"createdIndex": 4,
    	"key": "/test/key4",
    	"modifiedIndex": 4,
    	"value": "one"
    }
}
)";
        }

        //Then etcdlib_watchDir should return the compareAndSwap action
        rc = etcdlib_watchDir(etcdlib, "/test", 13, &action, &key, &value, &prevValue, &isDir, &modifiedIndex);
        ASSERT_EQ(ETCDLIB_RC_OK, rc);
        EXPECT_STREQ(ETCDLIB_ACTION_COMPARE_AND_SWAP, action);
        EXPECT_STREQ("/test/key4", key);
        EXPECT_STREQ("two", value);
        EXPECT_STREQ("one", prevValue);
        EXPECT_FALSE(isDir);
        EXPECT_EQ(13, modifiedIndex);
        free(key);
        free(value);
        free(prevValue);

        ///When preparing an etcd stubbed reply, compareAndDelete action
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->replyData = R"(
{
        "action": "compareAndDelete",
        "node": {
                "key": "/test/key5",
                "modifiedIndex": 15,
                "createdIndex": 5
        },
        "prevNode": {
                "key": "/test/key5",
                "value": "one",
                "modifiedIndex": 5,
                "createdIndex": 5
        }
}
)";
        }

        //Then etcdlib_watchDir should return the compareAndDelete action
        rc = etcdlib_watchDir(etcdlib, "/test", 15, &action, &key, &value, &prevValue, &isDir, &modifiedIndex);
        ASSERT_EQ(ETCDLIB_RC_OK, rc);
        EXPECT_STREQ(ETCDLIB_ACTION_COMPARE_AND_DELETE, action);
        EXPECT_STREQ("/test/key5", key);
        EXPECT_EQ(nullptr, value);
        EXPECT_STREQ("one", prevValue);
        EXPECT_FALSE(isDir);
        EXPECT_EQ(15, modifiedIndex);
        free(key);
        free(value);
        free(prevValue);

        ///When preparing an etcd stubbed reply, expire action
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->replyData = R"(
{
  "action": "expire",
  "node": {
    "createdIndex": 6,
    "key": "/test/key6",
    "modifiedIndex": 16
  },
  "prevNode": {
    "createdIndex": 6,
    "key": "/test/key6",
    "value": "bar",
    "modifiedIndex": 6
  }
}
)";
        }

        //Then etcdlib_watchDir should return the expire action
        rc = etcdlib_watchDir(etcdlib, "/test", 16, &action, &key, &value, &prevValue, &isDir, &modifiedIndex);
        ASSERT_EQ(ETCDLIB_RC_OK, rc);
        EXPECT_STREQ(ETCDLIB_ACTION_EXPIRE, action);
        EXPECT_STREQ("/test/key6", key);
        EXPECT_EQ(nullptr, value);
        EXPECT_STREQ("bar", prevValue);
        EXPECT_FALSE(isDir);
        EXPECT_EQ(16, modifiedIndex);
        free(key);
        free(value);
        free(prevValue);

        //When preparing an etcd stubbed reply, delete dir action
        //Note if a dir is deleted, the is no event for the children
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->replyData = R"(
{
    "action": "delete",
    "node": {
        "createdIndex": 1,
        "dir": true,
        "key": "/test",
        "modifiedIndex": 17
    },
    "prevNode": {
    	"createdIndex": 1,
    	"dir": true,
    	"key": "/test",
    	"modifiedIndex": 1
    }
}
)";
        }


        //Then etcdlib_watchDir should return the delete action
        rc = etcdlib_watchDir(etcdlib, "/test", 17, &action, &key, &value, &prevValue, &isDir, &modifiedIndex);
        ASSERT_EQ(ETCDLIB_RC_OK, rc);
        EXPECT_STREQ(ETCDLIB_ACTION_DELETE, action);
        EXPECT_STREQ("/test", key);
        EXPECT_EQ(nullptr, value);
        EXPECT_EQ(nullptr, prevValue);
        EXPECT_TRUE(isDir);
        EXPECT_EQ(17, modifiedIndex);
        free(key);
        free(value);
        free(prevValue);
    }

    static void watchAndDestroyEtcd(etcdlib_t* etcdlib, bool multiCurl) {
        // When preparing an etcd stubbed reply, delete dir action

        auto completeCallPromise = std::promise<void>{};
        auto completeCallFuture = std::make_shared<std::future<void>>(std::move(completeCallPromise.get_future()));
        auto processingRequestPromise = std::promise<void>{};
        auto processingRequestFuture = processingRequestPromise.get_future();

        {
            std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->syncCanCompleteRequest = completeCallFuture.get();
            mgTestCtx->inRequestCallPromise = &processingRequestPromise;
        }

        std::atomic<bool> etcdWatchCalled{false};
        //call watchDir in a separate thread
        std::thread thread{[etcdlib, &etcdWatchCalled, multiCurl] {
            const char* action = nullptr;
            char* key = nullptr;
            char* value = nullptr;
            char* prevValue = nullptr;
            bool isDir = false;
            long modifiedIndex = 0;
            auto rc = etcdlib_watchDir(etcdlib, "/test", 1, &action, &key, &value, &prevValue, &isDir, &modifiedIndex);
            if (multiCurl) {
                EXPECT_EQ(ETCDLIB_RC_STOPPING, rc); //multi curl support stopping a watch
            } else {
                EXPECT_EQ(ETCDLIB_RC_TIMEOUT, rc); //single curl does not support stopping a watch
            }
            etcdWatchCalled = true;
        }};

        processingRequestFuture.wait_for(std::chrono::seconds(200));

        //destroy etcdlib, which should unblock the watchDir call
        etcdlib_destroy(etcdlib);
        completeCallPromise.set_value();
        thread.join();
        ASSERT_TRUE(etcdWatchCalled);
    }

    static void createDirTest(etcdlib_t*  etcdlib) {
        //When preparing an etcd stubbed reply
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->expectedMethod = "PUT";
            mgTestCtx->expectedUrl = "/v2/keys/test";
            mgTestCtx->expectedData = "dir=true";
            mgTestCtx->replyData = R"({"action": "set", "node": {"dir": true, "key": "/test"}})";
        }

        //Then etcdlib_createDir should return ok
        auto rc = etcdlib_createDir(etcdlib, "test", 0);
        ASSERT_EQ(ETCDLIB_RC_OK, rc);

        //When preparing an etcd stubbed reply with a tll
        {
            const std::lock_guard<std::mutex> lock{mgTestCtx->mutex};
            mgTestCtx->expectedData = "dir=true;ttl=10";
        }

        //Then etcdlib_createDir should return ok
        rc = etcdlib_createDir(etcdlib, "test", 10);
        ASSERT_EQ(ETCDLIB_RC_OK, rc);
    }


  protected:
    static std::shared_ptr<mg_context> mgCtx;
    static std::unique_ptr<MgTestContext> mgTestCtx;
    static std::atomic<int> invalidContentLogCount;
    static std::atomic<int> errorMessageCount;
};

std::shared_ptr<mg_context> EtcdlibStubTestSuite::mgCtx{};
std::unique_ptr<MgTestContext> EtcdlibStubTestSuite::mgTestCtx{};
std::atomic<int> EtcdlibStubTestSuite::invalidContentLogCount{0};
std::atomic<int> EtcdlibStubTestSuite::errorMessageCount{0};

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

TEST_F(EtcdlibStubTestSuite, GetEtcdDirTest) {
    //Given an etcdlib instance
    etcdlib_autoptr_t etcdlib = createEtcdlib();
    EXPECT_NE(etcdlib, nullptr);
    getEtcdDirTest(etcdlib);

    //Given an etcdlib instance with curl multi handle
    etcdlib_autoptr_t etcdlib2 = createEtcdlibWithCurlMulti();
    EXPECT_NE(etcdlib2, nullptr);
    getEtcdDirTest(etcdlib2);
}

//TODO test getDir with invalid content reply

TEST_F(EtcdlibStubTestSuite, WatchEtcdDirTest) {
    //Given an etcdlib instance
    etcdlib_autoptr_t etcdlib = createEtcdlib();
    EXPECT_NE(etcdlib, nullptr);
    watchEtcdDirTest(etcdlib);

    //Given an etcdlib instance with curl multi handle
    etcdlib_autoptr_t etcdlib2 = createEtcdlibWithCurlMulti();
    EXPECT_NE(etcdlib2, nullptr);
    watchEtcdDirTest(etcdlib2);
}

TEST_F(EtcdlibStubTestSuite, WatchAndDestroyTest) {
    // //Given an etcdlib instance
    etcdlib_t*  etcdlib = createEtcdlib();
    EXPECT_NE(etcdlib, nullptr);
    watchAndDestroyEtcd(etcdlib, false);

    //Given an etcdlib instance with curl multi handle
    etcdlib_t* etcdlib2 = createEtcdlibWithCurlMulti();
    EXPECT_NE(etcdlib2, nullptr);
    watchAndDestroyEtcd(etcdlib2, true);
}

//TODO test watch with invalid content reply


TEST_F(EtcdlibStubTestSuite, CreateDirTest) {
    //Given an etcdlib instance
    etcdlib_autoptr_t etcdlib = createEtcdlib();
    EXPECT_NE(etcdlib, nullptr);
    createDirTest(etcdlib);

    //Given an etcdlib instance with curl multi handle
    etcdlib_autoptr_t etcdlib2 = createEtcdlibWithCurlMulti();
    EXPECT_NE(etcdlib2, nullptr);
    createDirTest(etcdlib2);
}

//TODO test createDir with invalid content reply


//TODO test refreshDir
//TODO test deleteDir
