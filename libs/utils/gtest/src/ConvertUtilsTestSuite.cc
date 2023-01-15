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

#include "celix_convert_utils.h"

class ConvertUtilsTestSuite : public ::testing::Test {
public:
    void checkVersion(const celix_version_t* version, int major, int minor, int micro, const char* qualifier) {
        EXPECT_TRUE(version != nullptr);
        if (version) {
            EXPECT_EQ(major, celix_version_getMajor(version));
            EXPECT_EQ(minor, celix_version_getMinor(version));
            EXPECT_EQ(micro, celix_version_getMicro(version));
            if (qualifier) {
                EXPECT_STREQ(qualifier, celix_version_getQualifier(version));
            } else {
                EXPECT_STREQ("", celix_version_getQualifier(version));
            }
        }
    }
};

TEST_F(ConvertUtilsTestSuite, ConvertToLongTest) {
    bool converted;
    //test for a valid string
    long result = celix_utils_convertStringToLong("10", 0, &converted);
    EXPECT_EQ(10, result);
    EXPECT_TRUE(converted);

    //test for an invalid string
    result = celix_utils_convertStringToLong("A", 0, &converted);
    EXPECT_EQ(0, result);
    EXPECT_FALSE(converted);

    //test for a string with a number
    result = celix_utils_convertStringToLong("10A", 0, &converted);
    EXPECT_EQ(10, result);
    EXPECT_TRUE(converted);

    //test for a string with a number and a negative sign
    result = celix_utils_convertStringToLong("-10", 0, &converted);
    EXPECT_EQ(-10, result);
    EXPECT_TRUE(converted);

    //test for a string with a number and a positive sign
    result = celix_utils_convertStringToLong("+10", 0, &converted);
    EXPECT_EQ(10, result);
    EXPECT_TRUE(converted);

    //TODO TBD, support hex and octal?
//    //test for a string with a hex number
//    result = celix_utils_convertStringToLong("0xA", 0, &converted);
//    EXPECT_EQ(10, result);
//    EXPECT_TRUE(converted);
//
//    //test for a string with an octal number
//    result = celix_utils_convertStringToLong("012", 0, &converted);
//    EXPECT_EQ(10, result);
//    EXPECT_TRUE(converted);

    //test for a convert with a nullptr for the converted parameter
    result = celix_utils_convertStringToLong("10", 0, nullptr);
    EXPECT_EQ(10, result);
}

TEST_F(ConvertUtilsTestSuite, ConvertToDoubleTest) {
    bool converted;
    //test for a valid string
    double result = celix_utils_convertStringToDouble("10.5", 0, &converted);
    EXPECT_EQ(10.5, result);
    EXPECT_TRUE(converted);

    //test for an invalid string
    result = celix_utils_convertStringToDouble("A", 0, &converted);
    EXPECT_EQ(0, result);
    EXPECT_FALSE(converted);

    //test for a string with a number
    result = celix_utils_convertStringToDouble("10.5A", 0, &converted);
    EXPECT_EQ(10.5, result);
    EXPECT_TRUE(converted);

    //test for a string with a number and a negative sign
    result = celix_utils_convertStringToDouble("-10.5", 0, &converted);
    EXPECT_EQ(-10.5, result);
    EXPECT_TRUE(converted);

    //test for a string with a number and a positive sign
    result = celix_utils_convertStringToDouble("+10.5", 0, &converted);
    EXPECT_EQ(10.5, result);
    EXPECT_TRUE(converted);

    //test for a string with a scientific notation
    result = celix_utils_convertStringToDouble("1.0e-10", 0, &converted);
    EXPECT_EQ(1.0e-10, result);
    EXPECT_TRUE(converted);

    //test for a convert with a nullptr for the converted parameter
    result = celix_utils_convertStringToDouble("10.5", 0, nullptr);
    EXPECT_EQ(10.5, result);
}

TEST_F(ConvertUtilsTestSuite, ConvertToBoolTest) {
    bool converted;
    //test for a valid string
    bool result = celix_utils_convertStringToBool("true", false, &converted);
    EXPECT_EQ(true, result);
    EXPECT_TRUE(converted);

    //test for an invalid string
    result = celix_utils_convertStringToBool("A", false, &converted);
    EXPECT_EQ(false, result);
    EXPECT_FALSE(converted);

    //test for a almost valid string
    result = celix_utils_convertStringToBool("trueA", false, &converted);
    EXPECT_EQ(false, result);
    EXPECT_FALSE(converted);

    //test for a almost valid string
    result = celix_utils_convertStringToBool("falseA", true, &converted);
    EXPECT_EQ(true, result);
    EXPECT_FALSE(converted);

    //test for a convert with a nullptr for the converted parameter
    result = celix_utils_convertStringToBool("true", false, nullptr);
    EXPECT_EQ(true, result);
}

TEST_F(ConvertUtilsTestSuite, ConvertToVersionTest) {
    //test for a valid string
    celix_version_t* result = celix_utils_convertStringToVersion("1.2.3");
    checkVersion(result, 1, 2, 3, nullptr);
    celix_version_destroy(result);

    //test for an invalid string
    result = celix_utils_convertStringToVersion("A");
    EXPECT_EQ(nullptr, result);

    //test for a string with a number
    result = celix_utils_convertStringToVersion("1.2.3.A");
    checkVersion(result, 1, 2, 3, "A");
    celix_version_destroy(result);

    //test for a string with a partly (strict) version
    result = celix_utils_convertStringToVersion("1");
    EXPECT_EQ(nullptr, result);

    //test for a string with a partly (strict) version
    result = celix_utils_convertStringToVersion("1.2");
    EXPECT_EQ(nullptr, result);
}