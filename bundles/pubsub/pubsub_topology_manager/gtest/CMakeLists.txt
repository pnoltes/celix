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

add_executable(test_pubsub_topology_manager
        src/PubSubTopologyManagerTestSuite.cc
)

target_link_libraries(test_pubsub_topology_manager PRIVATE Celix::framework Celix::pubsub_spi Celix::shell_api GTest::gtest GTest::gtest_main)
celix_target_bundle_set_definition(test_pubsub_topology_manager NAME TEST_BUNDLES Celix::celix_pubsub_topology_manager)

add_test(NAME test_pubsub_topology_manager COMMAND test_pubsub_topology_manager)
setup_target_for_coverage(test_pubsub_topology_manager SCAN_DIR ..)
