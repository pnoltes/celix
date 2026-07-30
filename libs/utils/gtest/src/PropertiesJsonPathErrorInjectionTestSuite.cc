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

#include "celix_properties.h"
#include "celix_stdlib_cleanup.h"
#include "malloc_ei.h"

class PropertiesJsonPathErrorInjectionTestSuite : public ::testing::Test {
  public:
    ~PropertiesJsonPathErrorInjectionTestSuite() override { celix_ei_expect_realloc(nullptr, 0, nullptr); }
};

TEST_F(PropertiesJsonPathErrorInjectionTestSuite, AllResultReturnsNullOnlyForAllocationFailure) {
    celix_autoptr(celix_properties_t) props = celix_properties_create();
    ASSERT_EQ(CELIX_SUCCESS, celix_properties_setLong(props, "value", 42));

    // The first realloc grows the parsed segment list. The all-result API must preserve ENOMEM as NULL.
    celix_ei_expect_realloc(CELIX_EI_UNKNOWN_CALLER, 1, nullptr);
    celix_autoptr(celix_array_list_t) result = celix_properties_getAllLongsByPath(props, "$.value");
    EXPECT_EQ(nullptr, result);
}
