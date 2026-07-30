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

#include <celix_err.h>
#include <celix_properties.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

int propertiesJsonPathFuzzOneInput(const uint8_t* data, size_t size) {
    char* path = static_cast<char*>(malloc(size + 1));
    if (!path)
        return 0;
    memcpy(path, data, size);
    path[size] = '\0';

    celix_properties_t* props = nullptr;
    celix_properties_loadFromString(R"({"a":{"b":[null,1,"two",{"c":3}]}})", 0, &props);
    celix_properties_checkPath(path);
    celix_array_list_t* values = celix_properties_getAllValuesByPath(props, path);
    celix_arrayList_destroy(values);
    celix_properties_destroy(props);
    celix_err_resetErrors();
    free(path);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    return propertiesJsonPathFuzzOneInput(data, size);
}
