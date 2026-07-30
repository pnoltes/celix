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

#include <cmath>
#include <gtest/gtest.h>
#include <jansson.h>

#include "celix/Properties.h"
#include "celix_err.h"
#include "celix_properties.h"
#include "celix_properties_private.h"
#include "celix_stdlib_cleanup.h"

class PropertiesSerializationTestSuite : public ::testing::Test {
  public:
    PropertiesSerializationTestSuite() { celix_err_resetErrors(); }
};

TEST_F(PropertiesSerializationTestSuite, SaveEmptyPropertiesTest) {
    // Given an empty properties object
    celix_autoptr(celix_properties_t) props = celix_properties_create();

    // And an in-memory stream
    celix_autofree char* buf = nullptr;
    size_t bufLen = 0;
    FILE* stream = open_memstream(&buf, &bufLen);

    // When saving the properties to the stream
    auto status = celix_properties_saveToStream(props, stream, 0);
    ASSERT_EQ(CELIX_SUCCESS, status);

    // Then the stream contains an empty JSON object
    fclose(stream);
    EXPECT_STREQ("{}", buf);
}

TEST_F(PropertiesSerializationTestSuite, SavePropertiesWithSingleValuesTest) {
    // Given a properties object with single values
    celix_autoptr(celix_properties_t) props = celix_properties_create();
    celix_properties_set(props, "key1", "value1");
    celix_properties_set(props, "key2", "value2");
    celix_properties_setLong(props, "key3", 3);
    celix_properties_setDouble(props, "key4", 4.0);
    celix_properties_setBool(props, "key5", true);
    celix_properties_assignVersion(props, "key6", celix_version_create(1, 2, 3, "qualifier"));

    // And an in-memory stream
    celix_autofree char* buf = nullptr;
    size_t bufLen = 0;
    FILE* stream = open_memstream(&buf, &bufLen);

    // When saving the properties to the stream
    auto status = celix_properties_saveToStream(props, stream, 0);
    ASSERT_EQ(CELIX_SUCCESS, status);

    // Then the stream contains the JSON representation snippets of the properties
    fclose(stream);
    EXPECT_NE(nullptr, strstr(buf, R"("key1":"value1")")) << "JSON: " << buf;
    EXPECT_NE(nullptr, strstr(buf, R"("key2":"value2")")) << "JSON: " << buf;
    EXPECT_NE(nullptr, strstr(buf, R"("key3":3)")) << "JSON: " << buf;
    EXPECT_NE(nullptr, strstr(buf, R"("key4":4.0)")) << "JSON: " << buf;
    EXPECT_NE(nullptr, strstr(buf, R"("key5":true)")) << "JSON: " << buf;
    EXPECT_NE(nullptr, strstr(buf, R"("key6":"version<1.2.3.qualifier>")")) << "JSON: " << buf;

    // And the buf is a valid JSON object
    json_error_t error;
    json_t* root = json_loads(buf, 0, &error);
    EXPECT_NE(nullptr, root) << "Unexpected JSON error: " << error.text;
    json_decref(root);
}

TEST_F(PropertiesSerializationTestSuite, SavePropertiesWithNaNAndInfValuesTest) {
    // Given a NAN, INF and -INF value
    auto keys = {"NAN", "INF", "-INF"};
    for (const auto& key : keys) {
        // For every value

        // Given a properties object with a NAN, INF or -INF value
        celix_autoptr(celix_properties_t) props = celix_properties_create();
        celix_properties_setDouble(props, key, strtod(key, nullptr));

        // Non-finite values are never valid JSON and are rejected independently of flags.
        char* output = nullptr;
        auto status = celix_properties_saveToString(props, 0, &output);
        ASSERT_EQ(CELIX_ILLEGAL_ARGUMENT, status);

        // And saving the properties to a string with the flag CELIX_PROPERTIES_ENCODE_ERROR_ON_NAN_INF fails
        celix_err_resetErrors();
        char* output2 = nullptr;
        status = celix_properties_saveToString(props, CELIX_PROPERTIES_ENCODE_ERROR_ON_NAN_INF, &output2);
        EXPECT_EQ(CELIX_ILLEGAL_ARGUMENT, status);

        // And an error msg is added to celix_err
        EXPECT_EQ(1, celix_err_getErrorCount());
    }
}

TEST_F(PropertiesSerializationTestSuite, SavePropertiesWithArrayListsContainingNaNAndInfValueTest) {
    auto keys = {"NAN", "INF", "-INF"};
    for (const auto& key : keys) {
        celix_autoptr(celix_properties_t) props = celix_properties_create();
        celix_autoptr(celix_array_list_t) list = celix_arrayList_createDoubleArray();
        celix_arrayList_addDouble(list, strtod(key, nullptr));
        celix_properties_assignArrayList(props, key, celix_steal_ptr(list));

        // JSON cannot represent NAN, INF and -INF, so encoding the array fails.
        celix_autofree char* output;
        auto status = celix_properties_saveToString(props, 0, &output);
        ASSERT_EQ(CELIX_ILLEGAL_ARGUMENT, status);

        // And saving the properties to a string with the flag CELIX_PROPERTIES_ENCODE_ERROR_ON_NAN_INF fails
        celix_err_resetErrors();
        char* output2;
        status = celix_properties_saveToString(props, CELIX_PROPERTIES_ENCODE_ERROR_ON_NAN_INF, &output2);
        EXPECT_EQ(CELIX_ILLEGAL_ARGUMENT, status);
        // And an error msg is added to celix_err
        EXPECT_EQ(3, celix_err_getErrorCount());

        celix_err_resetErrors();
        char* output3;
        status = celix_properties_saveToString(props, CELIX_PROPERTIES_ENCODE_ERROR_ON_EMPTY_ARRAYS, &output3);
        EXPECT_EQ(CELIX_ILLEGAL_ARGUMENT, status);
        EXPECT_EQ(3, celix_err_getErrorCount());
    }
}

TEST_F(PropertiesSerializationTestSuite, SavePropertiesWithArrayListsTest) {
    // Given a properties object with array list values
    celix_autoptr(celix_properties_t) props = celix_properties_create();

    celix_array_list_t* list1 = celix_arrayList_createStringArray();
    celix_arrayList_addString(list1, "value1");
    celix_arrayList_addString(list1, "value2");
    celix_properties_assignArrayList(props, "key1", list1);

    celix_array_list_t* list2 = celix_arrayList_createLongArray();
    celix_arrayList_addLong(list2, 1);
    celix_arrayList_addLong(list2, 2);
    celix_properties_assignArrayList(props, "key2", list2);

    celix_array_list_t* list3 = celix_arrayList_createDoubleArray();
    celix_arrayList_addDouble(list3, 1.0);
    celix_arrayList_addDouble(list3, 2.0);
    celix_properties_assignArrayList(props, "key3", list3);

    celix_array_list_t* list4 = celix_arrayList_createBoolArray();
    celix_arrayList_addBool(list4, true);
    celix_arrayList_addBool(list4, false);
    celix_properties_assignArrayList(props, "key4", list4);

    celix_array_list_t* list5 = celix_arrayList_createVersionArray();
    celix_arrayList_assignVersion(list5, celix_version_create(1, 2, 3, "qualifier"));
    celix_arrayList_assignVersion(list5, celix_version_create(4, 5, 6, "qualifier"));
    celix_properties_assignArrayList(props, "key5", list5);

    // And an in-memory stream
    celix_autofree char* buf = nullptr;
    size_t bufLen = 0;
    FILE* stream = open_memstream(&buf, &bufLen);

    // When saving the properties to the stream
    auto status = celix_properties_saveToStream(props, stream, 0);
    ASSERT_EQ(CELIX_SUCCESS, status);

    // Then the stream contains the JSON representation snippets of the properties
    fclose(stream);
    EXPECT_NE(nullptr, strstr(buf, R"("key1":["value1","value2"])")) << "JSON: " << buf;
    EXPECT_NE(nullptr, strstr(buf, R"("key2":[1,2])")) << "JSON: " << buf;
    EXPECT_NE(nullptr, strstr(buf, R"("key3":[1.0,2.0])")) << "JSON: " << buf;
    EXPECT_NE(nullptr, strstr(buf, R"("key4":[true,false])")) << "JSON: " << buf;
    EXPECT_NE(nullptr, strstr(buf, R"("key5":["version<1.2.3.qualifier>","version<4.5.6.qualifier>"])"))
        << "JSON: " << buf;

    // And the buf is a valid JSON object
    json_error_t error;
    json_t* root = json_loads(buf, 0, &error);
    EXPECT_NE(nullptr, root) << "Unexpected JSON error: " << error.text;
    json_decref(root);
}

TEST_F(PropertiesSerializationTestSuite, SaveEmptyArrayTest) {
    celix_autoptr(celix_properties_t) props = celix_properties_create();
    celix_array_list_t* arrays[] = {celix_arrayList_createStringArray(),
                                    celix_arrayList_createLongArray(),
                                    celix_arrayList_createDoubleArray(),
                                    celix_arrayList_createBoolArray(),
                                    celix_arrayList_createVersionArray()};
    const char* keys[] = {"key1", "key2", "key3", "key4", "key5"};
    for (int i = 0; i < 5; ++i) {
        ASSERT_EQ(CELIX_SUCCESS, celix_properties_assignArrayList(props, keys[i], arrays[i]));
    }
    celix_autofree char* output = nullptr;
    ASSERT_EQ(CELIX_SUCCESS, celix_properties_saveToString(props, 0, &output));
    for (const auto* key : keys) {
        std::string expected = std::string{"\""} + key + "\":[]";
        EXPECT_NE(nullptr, strstr(output, expected.c_str()));
    }
}

TEST_F(PropertiesSerializationTestSuite, SaveEmptyKeyTest) {
    celix_autoptr(celix_properties_t) props = celix_properties_create();
    celix_properties_setString(props, "", "value");

    celix_autofree char* output1;
    auto status = celix_properties_saveToString(props, 0, &output1);
    ASSERT_EQ(CELIX_SUCCESS, status);

    celix_autoptr(celix_properties_t) prop2 = nullptr;
    status = celix_properties_loadFromString(output1, 0, &prop2);
    ASSERT_EQ(CELIX_SUCCESS, status);

    ASSERT_TRUE(celix_properties_equals(props, prop2));
}

TEST_F(PropertiesSerializationTestSuite, SaveJSONPathKeysTest) {
    // Given a properties object with jpath keys
    celix_autoptr(celix_properties_t) props = celix_properties_create();
    celix_properties_set(props, "key1", "value1");
    celix_properties_set(props, "key2", "value2");
    celix_properties_set(props, "object1.key3", "value3");
    celix_properties_set(props, "object1.key4", "value4");
    celix_properties_set(props, "object2.key5", "value5");
    celix_properties_set(props, "object3.object4.key6", "value6");

    // And an in-memory stream
    celix_autofree char* output;

    // When saving the properties to the stream
    auto status = celix_properties_saveToString(props, CELIX_PROPERTIES_ENCODE_NESTED_STYLE, &output);
    ASSERT_EQ(CELIX_SUCCESS, status);

    // Then the stream contains the JSON representation snippets of the properties
    EXPECT_NE(nullptr, strstr(output, R"("key1":"value1")")) << "JSON: " << output;
    EXPECT_NE(nullptr, strstr(output, R"("key2":"value2")")) << "JSON: " << output;
    EXPECT_NE(nullptr, strstr(output, R"("object1.key3":"value3")")) << "JSON: " << output;
    EXPECT_NE(nullptr, strstr(output, R"("object1.key4":"value4")")) << "JSON: " << output;
    EXPECT_NE(nullptr, strstr(output, R"("object2.key5":"value5")")) << "JSON: " << output;
    EXPECT_NE(nullptr, strstr(output, R"("object3.object4.key6":"value6")")) << "JSON: " << output;

    // And the buf is a valid JSON object
    json_error_t error;
    json_auto_t* root = json_loads(output, 0, &error);
    EXPECT_NE(nullptr, root) << "Unexpected JSON error: " << error.text;
}

TEST_F(PropertiesSerializationTestSuite, SaveJPathKeysWithCollisionTest) {
    // note this tests depends on the key iteration order for properties and
    // properties key order is based on hash order of the keys, so this test can change if the string hash map
    // implementation changes.

    // Given a properties object with jpath keys that collide
    celix_autoptr(celix_properties_t) props = celix_properties_create();
    celix_properties_set(props, "key1.key2.key3", "value1");
    celix_properties_set(props, "key1.key2", "value2"); // collision with object "key1/key2/key3" -> overwrite
    celix_properties_set(props, "key4.key5.key6.key7", "value4");
    celix_properties_set(props, "key4.key5.key6", "value3"); // collision with field "key4/key5/key6/key7" -> overwrite

    // When saving the properties to a string
    celix_autofree char* output = nullptr;
    auto status = celix_properties_saveToString(props, CELIX_PROPERTIES_ENCODE_NESTED_STYLE, &output);
    ASSERT_EQ(CELIX_SUCCESS, status);

    // Literal member names never collide through separator interpretation.
    EXPECT_NE(nullptr, strstr(output, R"("key1.key2.key3":"value1")")) << "JSON: " << output;
    EXPECT_NE(nullptr, strstr(output, R"("key1.key2":"value2")")) << "JSON: " << output;
    EXPECT_NE(nullptr, strstr(output, R"("key4.key5.key6.key7":"value4")")) << "JSON: " << output;
    EXPECT_NE(nullptr, strstr(output, R"("key4.key5.key6":"value3")")) << "JSON: " << output;

    // And the buf is a valid JSON object
    json_error_t error;
    json_t* root = json_loads(output, 0, &error);
    EXPECT_NE(nullptr, root) << "Unexpected JSON error: " << error.text;
    json_decref(root);
}

TEST_F(PropertiesSerializationTestSuite, SavePropertiesWithNestedEndErrorOnCollisionsFlagsTest) {
    celix_autoptr(celix_properties_t) props = celix_properties_create();
    celix_properties_set(props, "key1", "value1");
    celix_properties_set(props, "key2", "value2");
    celix_properties_set(props, "object1.key3", "value3");
    celix_properties_set(props, "object1.key4", "value4");
    celix_properties_set(props, "object2.key5", "value5");
    celix_properties_set(props, "object3.object4.key6", "value6");

    // And an in-memory stream
    celix_autofree char* output;

    // When saving the properties to the stream
    auto status = celix_properties_saveToString(
        props, CELIX_PROPERTIES_ENCODE_NESTED_STYLE | CELIX_PROPERTIES_ENCODE_ERROR_ON_COLLISIONS, &output);
    ASSERT_EQ(CELIX_SUCCESS, status);
}

TEST_F(PropertiesSerializationTestSuite, SavePropertiesWithKeyNamesWithDotsTest) {
    // Given a properties set with key names with dots
    celix_autoptr(celix_properties_t) props = celix_properties_create();
    celix_properties_set(props, "a.key.name.with.dots", "value1");
    celix_properties_set(props, ".keyThatStartsWithDot", "value3");
    celix_properties_set(props, "keyThatEndsWithDot.", "value5");
    celix_properties_set(props, "keyThatEndsWithDoubleDots..", "value6");
    celix_properties_set(props, "key..With..Double..Dots", "value7");
    celix_properties_set(props, "object.keyThatEndsWithDot.", "value8");
    celix_properties_set(props, "object.keyThatEndsWithDoubleDots..", "value9");
    celix_properties_set(props, "object.key..With..Double..Dots", "value10");

    // When saving the properties to a string
    celix_autofree char* output;
    auto status = celix_properties_saveToString(props, CELIX_PROPERTIES_ENCODE_NESTED_STYLE, &output);
    ASSERT_EQ(CELIX_SUCCESS, status);

    // The output is valid and every dotted key is literal.
    json_error_t error;
    json_t* root = json_loads(output, 0, &error);
    ASSERT_NE(nullptr, root) << "Unexpected JSON error: " << error.text;
    EXPECT_STREQ("value1", json_string_value(json_object_get(root, "a.key.name.with.dots")));
    EXPECT_STREQ("value3", json_string_value(json_object_get(root, ".keyThatStartsWithDot")));
    EXPECT_STREQ("value10", json_string_value(json_object_get(root, "object.key..With..Double..Dots")));

    json_decref(root);
}

TEST_F(PropertiesSerializationTestSuite, SavePropertiesWithKeyCollision) {
    // note this tests depends on the key iteration order for properties and
    // properties key order is based on hash order of the keys, so this test can change if the string hash map
    // implementation changes.

    // Given a properties that contains keys that will collide with an existing JSON object
    celix_autoptr(celix_properties_t) props = celix_properties_create();
    celix_properties_set(props, "key1.key2.key3", "value1");
    celix_properties_set(props, "key1.key2", "value2"); // collision with object "key1.key2" -> overwrite

    // When saving the properties to a string
    celix_autofree char* output1;
    auto status = celix_properties_saveToString(props, CELIX_PROPERTIES_ENCODE_NESTED_STYLE, &output1);

    // Then the save succeeds
    ASSERT_EQ(CELIX_SUCCESS, status);

    // And both literal keys are serialized.
    EXPECT_NE(nullptr, strstr(output1, R"("key1.key2.key3":"value1")")) << "JSON: " << output1;
    EXPECT_NE(nullptr, strstr(output1, R"("key1.key2":"value2")")) << "JSON: " << output1;

    // When saving the properties to a string with the error on key collision flag
    char* output2;
    status = celix_properties_saveToString(
        props, CELIX_PROPERTIES_ENCODE_NESTED_STYLE | CELIX_PROPERTIES_ENCODE_ERROR_ON_COLLISIONS, &output2);

    // The legacy collision/style flags are no-ops.
    celix_autofree char* output2Guard = output2;
    ASSERT_EQ(CELIX_SUCCESS, status);
}

TEST_F(PropertiesSerializationTestSuite, SavePropertiesWithAndWithoutStrictFlagTest) {
    // Given a properties set with an empty array list
    celix_autoptr(celix_properties_t) props = celix_properties_create();
    auto* list = celix_arrayList_createStringArray();
    celix_properties_assignArrayList(props, "key1", list);

    // When saving the properties to a string without the strict flag
    celix_autofree char* output;
    auto status = celix_properties_saveToString(props, 0, &output);

    // Then the save succeeds
    ASSERT_EQ(CELIX_SUCCESS, status);

    // When saving the properties to a string with the strict flag
    celix_autofree char* output2 = nullptr;
    status = celix_properties_saveToString(props, CELIX_PROPERTIES_ENCODE_STRICT, &output2);

    // Then the retained strict flag is a no-op for valid empty arrays.
    ASSERT_EQ(CELIX_SUCCESS, status);
    EXPECT_STREQ(output, output2);
}

TEST_F(PropertiesSerializationTestSuite, SavePropertiesWithPrettyPrintTest) {
    // Given a properties set with 2 keys
    celix_autoptr(celix_properties_t) props = celix_properties_create();
    celix_properties_set(props, "key1", "value1");
    celix_properties_set(props, "key2", "value2");

    // When saving the properties to a string with pretty print
    celix_autofree char* output;
    auto status = celix_properties_saveToString(props, CELIX_PROPERTIES_ENCODE_PRETTY, &output);

    // Then the save succeeds
    ASSERT_EQ(CELIX_SUCCESS, status);

    // And the output contains the JSON representation snippets of the properties with pretty print (2 indent spaces and
    // newlines)
    auto* expected = "{\n  \"key2\": \"value2\",\n  \"key1\": \"value1\"\n}";
    EXPECT_STREQ(expected, output);
}

TEST_F(PropertiesSerializationTestSuite, SaveWithInvalidStreamTest) {
    celix_autoptr(celix_properties_t) properties = celix_properties_create();
    celix_properties_set(properties, "key", "value");

    // Saving properties with invalid stream will fail
    auto status = celix_properties_save(properties, "/non-existing/no/rights/file.json", 0);
    EXPECT_EQ(CELIX_FILE_IO_EXCEPTION, status);
    EXPECT_EQ(1, celix_err_getErrorCount());

    auto* readStream = fopen("/dev/null", "r");
    status = celix_properties_saveToStream(properties, readStream, 0);
    EXPECT_EQ(CELIX_FILE_IO_EXCEPTION, status);
    EXPECT_EQ(2, celix_err_getErrorCount());
    fclose(readStream);

    celix_err_printErrors(stderr, "Test Error: ", "\n");
}

TEST_F(PropertiesSerializationTestSuite, SaveCxxPropertiesTest) {
    // Given a C++ Properties object with 2 keys
    celix::Properties props{};
    props.set("key1", "value1");
    props.set("key2", 42);
    props.setVector("key3", std::vector<bool>{}); // empty vector

    // When saving the properties to a string
    std::string result = props.saveToString();

    // Then the result contains the JSON representation snippets of the properties
    EXPECT_NE(std::string::npos, result.find("\"key1\":\"value1\""));
    EXPECT_NE(std::string::npos, result.find("\"key2\":42"));

    // When saving the properties to a string using a flat style
    std::string result2 = props.saveToString(celix::Properties::EncodingFlags::FlatStyle);

    // The result is equals to a default save
    EXPECT_EQ(result, result2);

    // Deprecated error/style flags are no-ops for valid JSON values.
    EXPECT_EQ(result, props.saveToString(celix::Properties::EncodingFlags::Strict));

    // When saving the properties to a string using combined flags
    EXPECT_NO_THROW(props.saveToString(
        celix::Properties::EncodingFlags::Pretty | celix::Properties::EncodingFlags::ErrorOnEmptyArrays |
        celix::Properties::EncodingFlags::ErrorOnCollisions | celix::Properties::EncodingFlags::ErrorOnNanInf |
        celix::Properties::EncodingFlags::NestedStyle));

    // When saving the properties to an invalid filename location
    EXPECT_THROW(props.save("/non-existing/no/rights/file.json"), celix::IOException);
}

TEST_F(PropertiesSerializationTestSuite, LoadEmptyPropertiesTest) {
    // Given an empty JSON object
    const char* json = "{}";

    // When loading the properties from the stream
    celix_autoptr(celix_properties_t) props = nullptr;
    auto status = celix_properties_loadFromString(json, 0, &props);
    ASSERT_EQ(CELIX_SUCCESS, status);

    // Then the properties object is empty
    EXPECT_EQ(0, celix_properties_size(props));
}

TEST_F(PropertiesSerializationTestSuite, LoadPropertiesWithSingleValuesTest) {
    constexpr const char* input =
        R"({"strKey":"strValue","longKey":42,"doubleKey":2.0,"boolKey":true,"versionKey":"version<1.2.3.qualifier>"})";
    celix_autoptr(celix_properties_t) props = nullptr;
    ASSERT_EQ(CELIX_SUCCESS, celix_properties_loadFromString(input, 0, &props));
    EXPECT_EQ(5, celix_properties_size(props));
    EXPECT_STREQ("strValue", celix_properties_getString(props, "strKey"));
    EXPECT_EQ(42, celix_properties_getLong(props, "longKey", -1));
    EXPECT_DOUBLE_EQ(2.0, celix_properties_getDouble(props, "doubleKey", NAN));
    EXPECT_TRUE(celix_properties_getBool(props, "boolKey", false));
    EXPECT_EQ(CELIX_PROPERTIES_VALUE_TYPE_STRING, celix_properties_getType(props, "versionKey"));
    EXPECT_STREQ("version<1.2.3.qualifier>", celix_properties_getString(props, "versionKey"));
}

TEST_F(PropertiesSerializationTestSuite, LoadPropertiesWithArrayListsTest) {
    // Given a JSON object with array values for types string, long, double, bool and version
    const char* jsonInput = R"({
        "strArr":["value1","value2"],
        "intArr":[1,2],
        "realArr":[1.0,2.0],
        "boolArr":[true,false],
        "versionArr":["version<1.2.3.qualifier>","version<4.5.6.qualifier>"],
        "mixedRealAndIntArr1":[1,2.0,2,3.0],
        "mixedRealAndIntArr2":[1.0,2,2.0,3]
    })";

    // And a stream with the JSON object
    FILE* stream = fmemopen((void*)jsonInput, strlen(jsonInput), "r");

    // When loading the properties from the stream
    celix_autoptr(celix_properties_t) props = nullptr;
    auto status = celix_properties_loadFromStream(stream, 0, &props);
    ASSERT_EQ(CELIX_SUCCESS, status);

    // Then the properties object contains the array values
    EXPECT_EQ(7, celix_properties_size(props));

    // And the string array is correctly loaded
    auto* strArr = celix_properties_getArrayList(props, "strArr");
    ASSERT_NE(nullptr, strArr);
    EXPECT_EQ(CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING, celix_arrayList_getElementType(strArr));
    EXPECT_EQ(2, celix_arrayList_size(strArr));
    EXPECT_STREQ("value1", celix_arrayList_getString(strArr, 0));
    EXPECT_STREQ("value2", celix_arrayList_getString(strArr, 1));

    // And the long array is correctly loaded
    auto* intArr = celix_properties_getArrayList(props, "intArr");
    ASSERT_NE(nullptr, intArr);
    EXPECT_EQ(CELIX_ARRAY_LIST_ELEMENT_TYPE_LONG, celix_arrayList_getElementType(intArr));
    EXPECT_EQ(2, celix_arrayList_size(intArr));
    EXPECT_EQ(1, celix_arrayList_getLong(intArr, 0));
    EXPECT_EQ(2, celix_arrayList_getLong(intArr, 1));

    // And the double array is correctly loaded
    auto* realArr = celix_properties_getArrayList(props, "realArr");
    ASSERT_NE(nullptr, realArr);
    EXPECT_EQ(CELIX_ARRAY_LIST_ELEMENT_TYPE_DOUBLE, celix_arrayList_getElementType(realArr));
    EXPECT_EQ(2, celix_arrayList_size(realArr));
    EXPECT_DOUBLE_EQ(1.0, celix_arrayList_getDouble(realArr, 0));
    EXPECT_DOUBLE_EQ(2.0, celix_arrayList_getDouble(realArr, 1));

    // And the bool array is correctly loaded
    auto* boolArr = celix_properties_getArrayList(props, "boolArr");
    ASSERT_NE(nullptr, boolArr);
    EXPECT_EQ(CELIX_ARRAY_LIST_ELEMENT_TYPE_BOOL, celix_arrayList_getElementType(boolArr));
    EXPECT_EQ(2, celix_arrayList_size(boolArr));
    EXPECT_TRUE(celix_arrayList_getBool(boolArr, 0));
    EXPECT_FALSE(celix_arrayList_getBool(boolArr, 1));

    // JSON strings stay strings; version-looking strings are not inferred as versions.
    auto* versionArr = celix_properties_getArrayList(props, "versionArr");
    ASSERT_NE(nullptr, versionArr);
    EXPECT_EQ(CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING, celix_arrayList_getElementType(versionArr));
    EXPECT_EQ(2, celix_arrayList_size(versionArr));
    EXPECT_STREQ("version<1.2.3.qualifier>", celix_arrayList_getString(versionArr, 0));
    EXPECT_STREQ("version<4.5.6.qualifier>", celix_arrayList_getString(versionArr, 1));

    // Mixed JSON integer/real arrays preserve each numeric tag and value.
    auto* mixedRealAndIntArr1 = celix_properties_getArrayList(props, "mixedRealAndIntArr1");
    ASSERT_NE(nullptr, mixedRealAndIntArr1);
    EXPECT_EQ(CELIX_ARRAY_LIST_ELEMENT_TYPE_VARIANT, celix_arrayList_getElementType(mixedRealAndIntArr1));
    EXPECT_EQ(4, celix_arrayList_size(mixedRealAndIntArr1));
    EXPECT_EQ(CELIX_ARRAY_LIST_VARIANT_TYPE_LONG, celix_arrayList_getVariant(mixedRealAndIntArr1, 0)->type);
    EXPECT_EQ(1, celix_arrayList_getVariant(mixedRealAndIntArr1, 0)->value.longValue);
    EXPECT_EQ(CELIX_ARRAY_LIST_VARIANT_TYPE_DOUBLE, celix_arrayList_getVariant(mixedRealAndIntArr1, 1)->type);
    EXPECT_DOUBLE_EQ(2.0, celix_arrayList_getVariant(mixedRealAndIntArr1, 1)->value.doubleValue);

    auto* mixedRealAndIntArr2 = celix_properties_getArrayList(props, "mixedRealAndIntArr2");
    ASSERT_NE(nullptr, mixedRealAndIntArr2);
    EXPECT_EQ(CELIX_ARRAY_LIST_ELEMENT_TYPE_VARIANT, celix_arrayList_getElementType(mixedRealAndIntArr2));
    EXPECT_EQ(4, celix_arrayList_size(mixedRealAndIntArr2));
    EXPECT_EQ(CELIX_ARRAY_LIST_VARIANT_TYPE_DOUBLE, celix_arrayList_getVariant(mixedRealAndIntArr2, 0)->type);
    EXPECT_EQ(CELIX_ARRAY_LIST_VARIANT_TYPE_LONG, celix_arrayList_getVariant(mixedRealAndIntArr2, 1)->type);
}

TEST_F(PropertiesSerializationTestSuite, LoadPropertiesWithInvalidInputTest) {
    auto invalidInputs = {
        R"({)",  // invalid JSON (caught by jansson)
        R"([])", // unsupported JSON (top level array not supported)
        R"(42)", // invalid JSON (caught by jansson)
    };
    for (auto& invalidInput : invalidInputs) {
        // Given an invalid JSON object
        FILE* stream = fmemopen((void*)invalidInput, strlen(invalidInput), "r");

        // When loading the properties from the stream
        celix_autoptr(celix_properties_t) props = nullptr;
        auto status = celix_properties_loadFromStream(stream, 0, &props);

        // Then loading fails
        EXPECT_NE(CELIX_SUCCESS, status);

        // And at least one error message is added to celix_err
        EXPECT_GE(celix_err_getErrorCount(), 1);
        celix_err_printErrors(stderr, "Test Error: ", "\n");

        fclose(stream);
    }
}

TEST_F(PropertiesSerializationTestSuite, LoadPropertiesWithEmptyArrayTest) {
    // Given a JSON object with an empty array
    auto* inputJSON = R"({"key1":[]})";

    // When loading the properties from string
    celix_autoptr(celix_properties_t) props = nullptr;
    auto status = celix_properties_loadFromString(inputJSON, 0, &props);

    // Then loading succeeds
    ASSERT_EQ(CELIX_SUCCESS, status);

    // Empty arrays are retained as real, typed array-list values.
    ASSERT_EQ(1, celix_properties_size(props));
    const auto* empty = celix_properties_getArrayList(props, "key1");
    ASSERT_NE(nullptr, empty);
    EXPECT_EQ(CELIX_ARRAY_LIST_ELEMENT_TYPE_VARIANT, celix_arrayList_getElementType(empty));
    EXPECT_EQ(0, celix_arrayList_size(empty));

    // When loading the properties from string with a strict flag
    celix_autoptr(celix_properties_t) props2 = nullptr;
    status = celix_properties_loadFromString(inputJSON, CELIX_PROPERTIES_DECODE_ERROR_ON_EMPTY_ARRAYS, &props2);

    // The legacy flag is a no-op and the valid array remains present.
    ASSERT_EQ(CELIX_SUCCESS, status);
    EXPECT_EQ(0, celix_arrayList_size(celix_properties_getArrayList(props2, "key1")));
}

TEST_F(PropertiesSerializationTestSuite, LoadPropertiesWithNestedObjectsTest) {
    constexpr const char* input =
        R"({"key1":"value1","object1":{"key3":"value3","key4":true},"object2":{"key5":5.0},"object3":{"object4":{"key6":6}}})";
    celix_autoptr(celix_properties_t) props = nullptr;
    ASSERT_EQ(CELIX_SUCCESS, celix_properties_loadFromString(input, 0, &props));
    EXPECT_EQ(4, celix_properties_size(props));
    const auto* object1 = celix_properties_getProperties(props, "object1");
    ASSERT_NE(nullptr, object1);
    EXPECT_STREQ("value3", celix_properties_getString(object1, "key3"));
    EXPECT_TRUE(celix_properties_getBool(object1, "key4", false));
    const auto* object4 = celix_properties_getProperties(celix_properties_getProperties(props, "object3"), "object4");
    ASSERT_NE(nullptr, object4);
    EXPECT_EQ(6, celix_properties_getLong(object4, "key6", 0));
}

TEST_F(PropertiesSerializationTestSuite, LoadPropertiesWithDuplicatesTest) {
    // Given a complex JSON object with duplicate keys
    const char* jsonInput = R"({
        "key":2,
        "key":3
    })";

    // When loading the properties from a string.
    celix_autoptr(celix_properties_t) props = nullptr;
    auto status = celix_properties_loadFromString(jsonInput, 0, &props);

    // Properties cannot represent duplicate object member names, so duplicates are always rejected.
    EXPECT_EQ(CELIX_ILLEGAL_ARGUMENT, status);
    EXPECT_GE(celix_err_getErrorCount(), 1);
    celix_err_printErrors(stderr, "Test Error: ", "\n");

    // The retained legacy flag has the same behavior.
    celix_properties_t* props2;
    status = celix_properties_loadFromString(jsonInput, CELIX_PROPERTIES_DECODE_ERROR_ON_DUPLICATES, &props2);

    // Then loading fails, because of a duplicate key
    EXPECT_EQ(CELIX_ILLEGAL_ARGUMENT, status);

    // And at least one error message is added to celix_err
    EXPECT_GE(celix_err_getErrorCount(), 1);
    celix_err_printErrors(stderr, "Test Error: ", "\n");
}

TEST_F(PropertiesSerializationTestSuite, NestedDuplicateMembersAreRejected) {
    for (const char* input : {R"({"nested":{"key":1,"key":2}})",
                              R"({"array":[{"key":1,"key":2}]})",
                              R"({"array":[[null,{"key":1,"key":2}]]})"}) {
        celix_properties_t* props = nullptr;
        EXPECT_EQ(CELIX_ILLEGAL_ARGUMENT, celix_properties_loadFromString(input, 0, &props)) << input;
        EXPECT_EQ(nullptr, props);
        celix_err_resetErrors();
    }
}

TEST_F(PropertiesSerializationTestSuite, LoadPropertiesEscapedDotsTest) {
    constexpr const char* input = R"({"object.key":"literal","object":{"key":"nested"}})";
    celix_autoptr(celix_properties_t) props = nullptr;
    ASSERT_EQ(CELIX_SUCCESS, celix_properties_loadFromString(input, CELIX_PROPERTIES_DECODE_STRICT, &props));
    EXPECT_STREQ("literal", celix_properties_getString(props, "object.key"));
    const auto* nested = celix_properties_getProperties(props, "object");
    ASSERT_NE(nullptr, nested);
    EXPECT_STREQ("nested", celix_properties_getString(nested, "key"));
}

TEST_F(PropertiesSerializationTestSuite, LoadPropertiesWithAndWithoutStrictFlagTest) {
    auto invalidInputs = {
        R"({"key1":null})",   // Null value gives error on strict
        R"({"":"value"})",    // "" key gives error on strict
        R"({"emptyArr":[]})", // Empty array gives error on strict
    };

    for (auto& invalidInput : invalidInputs) {
        // Given an invalid JSON object
        FILE* stream = fmemopen((void*)invalidInput, strlen(invalidInput), "r");

        // When loading the properties from the stream with an empty flags
        celix_autoptr(celix_properties_t) props = nullptr;
        auto status = celix_properties_loadFromStream(stream, 0, &props);

        // Then decoding succeeds, because strict is disabled
        ASSERT_EQ(CELIX_SUCCESS, status);
        EXPECT_GE(celix_err_getErrorCount(), 0);

        // But the properties size is 0 or 1, because the all invalid inputs are ignored, except the duplicate key
        auto size = celix_properties_size(props);
        EXPECT_TRUE(size == 0 || size == 1);

        fclose(stream);
    }

    for (auto& invalidInput : invalidInputs) {
        // Given an invalid JSON object
        FILE* stream = fmemopen((void*)invalidInput, strlen(invalidInput), "r");

        // When loading the properties from the stream with a strict flag
        celix_autoptr(celix_properties_t) props = nullptr;
        auto status = celix_properties_loadFromStream(stream, CELIX_PROPERTIES_DECODE_STRICT, &props);

        // Strict is retained as a compatibility no-op for these valid JSON constructs.
        EXPECT_EQ(CELIX_SUCCESS, status);
        EXPECT_EQ(1, celix_properties_size(props));

        fclose(stream);
    }
}

TEST_F(PropertiesSerializationTestSuite, LoadPropertiesWithUnsupportedArrayTypesTest) {
    auto supportedArrays = {R"({"objArray":[{"obj1": true}, {"obj2": true}]})",
                            R"({"arrayArray":[[1,2], [2,4]]})",
                            R"({"nullArr":[null,null]})"};

    for (auto* input : supportedArrays) {
        celix_autoptr(celix_properties_t) props = nullptr;
        ASSERT_EQ(CELIX_SUCCESS, celix_properties_loadFromString(input, CELIX_PROPERTIES_DECODE_STRICT, &props));
        EXPECT_EQ(1, celix_properties_size(props));
    }
}

TEST_F(PropertiesSerializationTestSuite, LoadPropertiesWithDotsInTheKeysTest) {
    constexpr const char* input = R"({".":"value1","key..":"value2","object":{".":"value3","key..":"value4"}})";
    celix_autoptr(celix_properties_t) props = nullptr;
    ASSERT_EQ(CELIX_SUCCESS, celix_properties_loadFromString(input, 0, &props));
    EXPECT_STREQ("value1", celix_properties_getString(props, "."));
    EXPECT_STREQ("value2", celix_properties_getString(props, "key.."));
    const auto* object = celix_properties_getProperties(props, "object");
    ASSERT_NE(nullptr, object);
    EXPECT_STREQ("value3", celix_properties_getString(object, "."));
    EXPECT_STREQ("value4", celix_properties_getString(object, "key.."));
}

TEST_F(PropertiesSerializationTestSuite, LoadPropertiesWithInvalidVersionsTest) {
    constexpr const char* input = R"({"key":"version<1.2.3.<qualifier>>"})";
    celix_autoptr(celix_properties_t) props = nullptr;
    ASSERT_EQ(CELIX_SUCCESS, celix_properties_loadFromString(input, 0, &props));
    EXPECT_EQ(CELIX_PROPERTIES_VALUE_TYPE_STRING, celix_properties_getType(props, "key"));
    EXPECT_STREQ("version<1.2.3.<qualifier>>", celix_properties_getString(props, "key"));
}

TEST_F(PropertiesSerializationTestSuite, LegacyVersionStringsRequireExplicitFlagAndPropagateRecursively) {
    constexpr const char* input =
        R"({"version":"version<1.2.3>","nested":{"version":"version<2.3.4>"},"versions":["version<3.4.5>"]})";
    celix_autoptr(celix_properties_t) standard = nullptr;
    ASSERT_EQ(CELIX_SUCCESS, celix_properties_loadFromString(input, 0, &standard));
    EXPECT_EQ(CELIX_PROPERTIES_VALUE_TYPE_STRING, celix_properties_getType(standard, "version"));
    EXPECT_EQ(CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING,
              celix_arrayList_getElementType(celix_properties_getArrayList(standard, "versions")));

    celix_autoptr(celix_properties_t) legacy = nullptr;
    ASSERT_EQ(CELIX_SUCCESS,
              celix_properties_loadFromString(input, CELIX_PROPERTIES_DECODE_LEGACY_VERSION_STRINGS, &legacy));
    EXPECT_EQ(CELIX_PROPERTIES_VALUE_TYPE_VERSION, celix_properties_getType(legacy, "version"));
    EXPECT_EQ(CELIX_PROPERTIES_VALUE_TYPE_VERSION,
              celix_properties_getType(celix_properties_getProperties(legacy, "nested"), "version"));
    EXPECT_EQ(CELIX_ARRAY_LIST_ELEMENT_TYPE_VERSION,
              celix_arrayList_getElementType(celix_properties_getArrayList(legacy, "versions")));

    celix_autoptr(celix_properties_t) empty = nullptr;
    EXPECT_EQ(
        CELIX_SUCCESS,
        celix_properties_loadFromString(R"({"empty":""})", CELIX_PROPERTIES_DECODE_LEGACY_VERSION_STRINGS, &empty));
    EXPECT_STREQ("", celix_properties_getString(empty, "empty"));
}

TEST_F(PropertiesSerializationTestSuite, LoadWithInvalidStreamTest) {
    celix_properties_t* dummyProps = nullptr;

    // Loading properties with invalid stream will fail
    auto status = celix_properties_load("non_existing_file.json", 0, &dummyProps);
    EXPECT_EQ(CELIX_FILE_IO_EXCEPTION, status);
    EXPECT_EQ(1, celix_err_getErrorCount());

    char* buf = nullptr;
    size_t size = 0;
    FILE* stream = open_memstream(&buf, &size); // empty stream
    status = celix_properties_loadFromStream(stream, 0, &dummyProps);
    EXPECT_EQ(CELIX_ILLEGAL_ARGUMENT, status);
    EXPECT_EQ(2, celix_err_getErrorCount());

    fclose(stream);
    free(buf);
    celix_err_printErrors(stderr, "Test Error: ", "\n");
}

TEST_F(PropertiesSerializationTestSuite, LoadCxxPropertiesTest) {
    // Given a JSON object
    auto jsonInput = R"({"key1":"value1","key2":42,"key2":43})";

    // Duplicate JSON object member names are rejected even without a strict flag.
    EXPECT_THROW(celix::Properties::loadFromString(jsonInput), celix::IllegalArgumentException);

    // When loading the properties from the JSON object with a strict flag
    EXPECT_THROW(celix::Properties::loadFromString(jsonInput, celix::Properties::DecodeFlags::Strict),
                 celix::IllegalArgumentException);

    // When loading the properties from the JSON object with a flag combined
    EXPECT_THROW(
        celix::Properties::loadFromString(
            jsonInput,
            celix::Properties::DecodeFlags::ErrorOnCollisions | celix::Properties::DecodeFlags::ErrorOnDuplicates |
                celix::Properties::DecodeFlags::ErrorOnEmptyArrays | celix::Properties::DecodeFlags::ErrorOnEmptyKeys |
                celix::Properties::DecodeFlags::ErrorOnUnsupportedArrays |
                celix::Properties::DecodeFlags::ErrorOnNullValues | celix::Properties::DecodeFlags::ErrorOnNullValues),
        celix::IllegalArgumentException);

    EXPECT_THROW(celix::Properties::load2("non_existing_file.json"), celix::IOException);
}

TEST_F(PropertiesSerializationTestSuite, SaveAndLoadFlatProperties) {
    // Given a properties object with all possible types (but no empty arrays)
    celix_autoptr(celix_properties_t) props = celix_properties_create();

    celix_properties_set(props, "single/strKey", "strValue");
    celix_properties_setLong(props, "single/longKey", 42);
    celix_properties_setDouble(props, "single/doubleKey", 2.0);
    celix_properties_setBool(props, "single/boolKey", true);
    celix_properties_assignVersion(props, "single/versionKey", celix_version_create(1, 2, 3, "qualifier"));

    celix_array_list_t* strArr = celix_arrayList_createStringArray();
    celix_arrayList_addString(strArr, "value1");
    celix_arrayList_addString(strArr, "value2");
    celix_properties_assignArrayList(props, "array/stringArr", strArr);

    celix_array_list_t* longArr = celix_arrayList_createLongArray();
    celix_arrayList_addLong(longArr, 1);
    celix_arrayList_addLong(longArr, 2);
    celix_properties_assignArrayList(props, "array/longArr", longArr);

    celix_array_list_t* doubleArr = celix_arrayList_createDoubleArray();
    celix_arrayList_addDouble(doubleArr, 1.0);
    celix_arrayList_addDouble(doubleArr, 2.0);
    celix_properties_assignArrayList(props, "array/doubleArr", doubleArr);

    celix_array_list_t* boolArr = celix_arrayList_createBoolArray();
    celix_arrayList_addBool(boolArr, true);
    celix_arrayList_addBool(boolArr, false);
    celix_properties_assignArrayList(props, "array/boolArr", boolArr);

    celix_array_list_t* versionArr = celix_arrayList_createVersionArray();
    celix_arrayList_assignVersion(versionArr, celix_version_create(1, 2, 3, "qualifier"));
    celix_arrayList_assignVersion(versionArr, celix_version_create(4, 5, 6, "qualifier"));
    celix_properties_assignArrayList(props, "array/versionArr", versionArr);

    // When saving the properties to a properties_test.json file
    const char* filename = "properties_test.json";
    auto status = celix_properties_save(props, filename, CELIX_PROPERTIES_ENCODE_PRETTY);

    // Then saving succeeds
    ASSERT_EQ(CELIX_SUCCESS, status);

    // When loading the properties from the properties_test.json file
    celix_autoptr(celix_properties_t) loadedProps = nullptr;
    status = celix_properties_load(filename, 0, &loadedProps);

    // Then loading succeeds
    ASSERT_EQ(CELIX_SUCCESS, status);

    // JSON strings are not reinterpreted as Celix versions.
    EXPECT_FALSE(celix_properties_equals(props, loadedProps));
    const auto* loadedVersionArr = celix_properties_getArrayList(loadedProps, "array/versionArr");
    ASSERT_NE(nullptr, loadedVersionArr);
    EXPECT_EQ(CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING, celix_arrayList_getElementType(loadedVersionArr));
}

TEST_F(PropertiesSerializationTestSuite, SaveAndLoadCxxProperties) {
    // Given a filename
    std::string filename = "properties_test.json";

    // And a Properties object with 1 key
    celix::Properties props{};
    props.set("key1", "value1");

    // When saving the properties to the filename
    props.save(filename);

    // And reloading the properties from the filename
    auto props2 = celix::Properties::load2(filename);

    // Then the reloaded properties are equal to the original properties
    EXPECT_TRUE(props == props2);
}

TEST_F(PropertiesSerializationTestSuite, KeyCollision) {
    celix_autoptr(celix_properties_t) props = celix_properties_create();
    // pick keys such that key1 appears before key2 when iterating over the properties
    celix_properties_set(props, "a.b.haha.arbifdadfsfa", "value1");
    celix_properties_set(props, "a.b.haha", "value2");

    celix_autofree char* output = nullptr;
    auto status = celix_properties_saveToString(
        props, CELIX_PROPERTIES_ENCODE_NESTED_STYLE | CELIX_PROPERTIES_ENCODE_ERROR_ON_COLLISIONS, &output);
    EXPECT_EQ(CELIX_SUCCESS, status);
    EXPECT_NE(nullptr, strstr(output, R"("a.b.haha.arbifdadfsfa":"value1")"));
    EXPECT_NE(nullptr, strstr(output, R"("a.b.haha":"value2")"));

    celix_autoptr(celix_properties_t) props2 = celix_properties_create();
    // pick keys such that key1 appears before key2 when iterating over the properties
    celix_properties_set(props2, "a.b.c", "value1");
    celix_properties_set(props2, "a.b.c.d", "value2");
    celix_autofree char* output2 = nullptr;
    status = celix_properties_saveToString(
        props2, CELIX_PROPERTIES_ENCODE_NESTED_STYLE | CELIX_PROPERTIES_ENCODE_ERROR_ON_COLLISIONS, &output2);
    EXPECT_EQ(CELIX_SUCCESS, status);
    EXPECT_NE(nullptr, strstr(output2, R"("a.b.c":"value1")"));
    EXPECT_NE(nullptr, strstr(output2, R"("a.b.c.d":"value2")"));

    celix_autofree char* output3 = nullptr;
    status = celix_properties_saveToString(props2, CELIX_PROPERTIES_ENCODE_NESTED_STYLE, &output3);
    EXPECT_EQ(CELIX_SUCCESS, status);
    EXPECT_NE(nullptr, strstr(output3, R"("a.b.c":"value1")"));
    EXPECT_NE(nullptr, strstr(output3, R"("a.b.c.d":"value2")"));
}
TEST_F(PropertiesSerializationTestSuite, RecursiveJsonObjectRoundTripTest) {
    constexpr const char* input = R"({"a.b":1,"a":{"b":2,"n":null,"items":[{},[1],null]}})";
    celix_autoptr(celix_properties_t) props = nullptr;
    ASSERT_EQ(CELIX_SUCCESS, celix_properties_loadFromString(input, 0, &props));
    EXPECT_EQ(1, celix_properties_getLong(props, "a.b", -1));
    const auto* nested = celix_properties_getProperties(props, "a");
    ASSERT_NE(nullptr, nested);
    EXPECT_EQ(2, celix_properties_getLong(nested, "b", -1));
    EXPECT_TRUE(celix_properties_isNull(nested, "n"));

    celix_autofree char* output = nullptr;
    ASSERT_EQ(CELIX_SUCCESS, celix_properties_saveToString(props, 0, &output));
    celix_autoptr(celix_properties_t) decoded = nullptr;
    ASSERT_EQ(CELIX_SUCCESS, celix_properties_loadFromString(output, 0, &decoded));
    EXPECT_TRUE(celix_properties_equals(props, decoded));
}
