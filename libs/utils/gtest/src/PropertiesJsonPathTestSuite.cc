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
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <gtest/gtest.h>

#include "celix_err.h"
#include "celix_properties.h"
#include "celix_stdlib_cleanup.h"

class PropertiesJsonPathTestSuite : public ::testing::Test {
  public:
    PropertiesJsonPathTestSuite(const PropertiesJsonPathTestSuite&) = delete;
    PropertiesJsonPathTestSuite& operator=(const PropertiesJsonPathTestSuite&) = delete;

    PropertiesJsonPathTestSuite() {
        celix_err_resetErrors();
        constexpr const char* json = R"({
          "store": {
            "book": [
              {"category":"reference","author":"Nigel Rees","title":"Sayings","price":8.95},
              {"category":"fiction","author":"Evelyn Waugh","title":"Sword","price":12.99},
              {"category":"fiction","author":"Herman Melville","title":"Moby Dick","price":8.99},
              {"category":"fiction","author":"J. R. R. Tolkien","title":"The Lord","price":22.99}
            ],
            "bicycle":{"color":"red","price":19.95}
          },
          "weird":{"":"empty","a\"b":"quote","unicode":"ok"},
          "nothing":null
        })";
        EXPECT_EQ(CELIX_SUCCESS, celix_properties_loadFromString(json, 0, &properties));
    }

    ~PropertiesJsonPathTestSuite() override {
        celix_properties_destroy(properties);
        celix_err_resetErrors();
    }

    celix_properties_t* properties{nullptr};
};

TEST_F(PropertiesJsonPathTestSuite, RootNamesIndexesAndEscapes) {
    EXPECT_TRUE(celix_properties_hasPropertiesPath(properties, "$"));
    EXPECT_STREQ("Sayings", celix_properties_getStringByPath(properties, "$.store.book[0].title", nullptr));
    EXPECT_STREQ("The Lord", celix_properties_getStringByPath(properties, "$['store']['book'][-1]['title']", nullptr));
    EXPECT_STREQ("empty", celix_properties_getStringByPath(properties, "$['weird']['']", nullptr));
    EXPECT_STREQ("quote", celix_properties_getStringByPath(properties, R"($["weird"]["a\"b"])", nullptr));
    EXPECT_STREQ("ok", celix_properties_getStringByPath(properties, "$['weird']['unic\\u006fde']", nullptr));
    EXPECT_TRUE(celix_properties_hasNullPath(properties, "$.nothing"));
}

TEST_F(PropertiesJsonPathTestSuite, WildcardsUnionsAndTypedFirstResult) {
    celix_autoptr(celix_array_list_t) authors =
        celix_properties_getAllStringsByPath(properties, "$.store.book[*].author");
    ASSERT_NE(nullptr, authors);
    ASSERT_EQ(4, celix_arrayList_size(authors));
    EXPECT_STREQ("Nigel Rees", celix_arrayList_getString(authors, 0));
    EXPECT_STREQ("J. R. R. Tolkien", celix_arrayList_getString(authors, 3));

    celix_autoptr(celix_array_list_t) duplicateTitles =
        celix_properties_getAllStringsByPath(properties, "$.store.book[0,0,2]['title']");
    ASSERT_EQ(3, celix_arrayList_size(duplicateTitles));
    EXPECT_STREQ("Sayings", celix_arrayList_getString(duplicateTitles, 0));
    EXPECT_STREQ("Sayings", celix_arrayList_getString(duplicateTitles, 1));
    EXPECT_STREQ("Moby Dick", celix_arrayList_getString(duplicateTitles, 2));

    EXPECT_DOUBLE_EQ(8.95, celix_properties_getDoubleByPath(properties, "$.store.book[0]['title','price']", -1.0));
}

TEST_F(PropertiesJsonPathTestSuite, ForwardReverseAndOmittedSlices) {
    celix_autoptr(celix_array_list_t) forward =
        celix_properties_getAllStringsByPath(properties, "$.store.book[1:4:2].title");
    ASSERT_EQ(2, celix_arrayList_size(forward));
    EXPECT_STREQ("Sword", celix_arrayList_getString(forward, 0));
    EXPECT_STREQ("The Lord", celix_arrayList_getString(forward, 1));

    celix_autoptr(celix_array_list_t) reverse =
        celix_properties_getAllStringsByPath(properties, "$.store.book[::-1].title");
    ASSERT_EQ(4, celix_arrayList_size(reverse));
    EXPECT_STREQ("The Lord", celix_arrayList_getString(reverse, 0));
    EXPECT_STREQ("Sayings", celix_arrayList_getString(reverse, 3));
}

TEST_F(PropertiesJsonPathTestSuite, DescendantsAndStructuralMismatch) {
    celix_autoptr(celix_array_list_t) prices = celix_properties_getAllDoublesByPath(properties, "$..price");
    ASSERT_NE(nullptr, prices);
    EXPECT_EQ(5, celix_arrayList_size(prices));

    celix_autoptr(celix_array_list_t) titles = celix_properties_getAllStringsByPath(properties, "$..['title']");
    ASSERT_EQ(4, celix_arrayList_size(titles));

    celix_autoptr(celix_array_list_t) mismatch = celix_properties_getAllStringsByPath(properties, "$.store.book.title");
    ASSERT_NE(nullptr, mismatch);
    EXPECT_EQ(CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING, celix_arrayList_getElementType(mismatch));
    EXPECT_EQ(0, celix_arrayList_size(mismatch));
}

TEST_F(PropertiesJsonPathTestSuite, InvalidAndUnsupportedPathsReturnDocumentedFallbacks) {
    const char* invalid[] = {"", "store", "$.store.book[::0]", "$.store.book[?(@.price)]", "$@", "$.bad-name"};
    for (const char* path : invalid)
        EXPECT_FALSE(celix_properties_checkPath(path)) << path;

    EXPECT_STREQ("fallback", celix_properties_getStringByPath(properties, "$.store.book[", "fallback"));
    EXPECT_FALSE(celix_properties_hasPath(properties, "$.store.book["));
    celix_autoptr(celix_array_list_t) empty = celix_properties_getAllLongsByPath(properties, "$.store.book[");
    ASSERT_NE(nullptr, empty);
    EXPECT_EQ(CELIX_ARRAY_LIST_ELEMENT_TYPE_LONG, celix_arrayList_getElementType(empty));
    EXPECT_EQ(0, celix_arrayList_size(empty));
}

TEST_F(PropertiesJsonPathTestSuite, ResultLimitDoesNotReturnPartialResults) {
    celix_autoptr(celix_array_list_t) largeArray = celix_arrayList_createLongArray();
    ASSERT_NE(nullptr, largeArray);
    for (int i = 0; i <= 100000; ++i) {
        ASSERT_EQ(CELIX_SUCCESS, celix_arrayList_addLong(largeArray, i));
    }
    ASSERT_EQ(CELIX_SUCCESS, celix_properties_assignArrayList(properties, "large", celix_steal_ptr(largeArray)));

    celix_autoptr(celix_array_list_t) typed = celix_properties_getAllLongsByPath(properties, "$.large[*]");
    ASSERT_NE(nullptr, typed);
    EXPECT_EQ(0, celix_arrayList_size(typed));

    celix_autoptr(celix_array_list_t) variants = celix_properties_getAllValuesByPath(properties, "$.large[*]");
    ASSERT_NE(nullptr, variants);
    EXPECT_EQ(0, celix_arrayList_size(variants));
}

TEST_F(PropertiesJsonPathTestSuite, AllResultsOwnDeepCopies) {
    celix_autoptr(celix_array_list_t) books = celix_properties_getAllPropertiesByPath(properties, "$.store.book[0,1]");
    celix_autoptr(celix_array_list_t) values =
        celix_properties_getAllValuesByPath(properties, "$.store.book[0]['title','price']");
    ASSERT_EQ(2, celix_arrayList_size(books));
    ASSERT_EQ(2, celix_arrayList_size(values));
    celix_properties_destroy(properties);
    properties = nullptr;

    EXPECT_STREQ("Sayings", celix_properties_getString(celix_arrayList_getProperties(books, 0), "title"));
    EXPECT_EQ(CELIX_ARRAY_LIST_VARIANT_TYPE_STRING, celix_arrayList_getVariant(values, 0)->type);
    EXPECT_STREQ("Sayings", celix_arrayList_getVariant(values, 0)->value.stringValue);
    EXPECT_EQ(CELIX_ARRAY_LIST_VARIANT_TYPE_DOUBLE, celix_arrayList_getVariant(values, 1)->type);
}
