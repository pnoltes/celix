# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

add_executable(test_etcdlib
        src/main.cc
        src/EtcdlibTestSuite.cc
)

target_link_libraries(test_etcdlib PRIVATE CURL::libcurl etcdlib_static GTest::gtest)
target_include_directories(test_etcdlib PRIVATE ../src)

add_test(NAME test_etcdlib COMMAND test_etcdlib)
setup_target_for_coverage(test_etcdlib SCAN_DIR ..)


if (EI_TESTS)
    add_executable(test_etcdlib_mock
            src/main.cc
            src/EtcdlibMockTestSuite.cc
    )

    target_link_libraries(test_etcdlib_mock PRIVATE CURL::libcurl etcdlib_static GTest::gtest)
    target_include_directories(test_etcdlib_mock PRIVATE ../src)
    target_link_options(test_etcdlib_mock PRIVATE
            LINKER:--wrap,curl_global_init
            LINKER:--wrap,curl_easy_perform
            LINKER:--wrap,curl_multi_perform
            LINKER:--wrap,curl_multi_info_read
            LINKER:--wrap,curl_multi_remove_handle
            LINKER:--wrap,curl_multi_add_handle
    )

    add_test(NAME test_etcdlib_mock COMMAND test_etcdlib_mock)
    setup_target_for_coverage(test_etcdlib_mock SCAN_DIR ..)
endif ()