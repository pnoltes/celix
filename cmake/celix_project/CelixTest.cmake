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

option(CELIX_GEN_XML_TEST_REPORTS "Generate xml test reports when running tests" FALSE)

#Special target to run all tests on CI, so that a special log command can be used to run test with xml output and
#add special log command to splits test in the Github Actions UI
add_custom_target(test_on_ci)

#[[
]]
function(add_celix_test)
    set(OPTIONS )
    set(ONE_VAL_ARGS NAME)
    set(MULTI_VAL_ARGS COMMAND)
    cmake_parse_arguments(CELIX_TEST "${OPTIONS}" "${ONE_VAL_ARGS}" "${MULTI_VAL_ARGS}" ${ARGN})
    set(COMMAND_ARG "")

    set(COMMAND_ARG ${CELIX_TEST_COMMAND})

    list(LENGTH CELIX_TEST_COMMAND CELIX_TEST_COMMAND_LEN)
    if (CELIX_TEST_COMMAND_LEN EQUAL 1)
        list(GET CELIX_TEST_COMMAND 0 CMD_FIRST_ELEMENT)
        if (TARGET ${CMD_FIRST_ELEMENT})
            message(STATUS "Adding --gtest_output to target ${CMD_FIRST_ELEMENT}")
            add_test(NAME ${CELIX_TEST_NAME} COMMAND ${CMD_FIRST_ELEMENT} --gtest_output=xml:${CMAKE_BINARY_DIR}/test_reports/ ${CELIX_TEST_UNPARSED_ARGUMENTS})
            add_custom_target(${CELIX_TEST_NAME}_on_ci
                    COMMAND echo "::group::Running ${CELIX_TEST_NAME}"
                    COMMAND ${CMAKE_CTEST_COMMAND} -V --output-on-failure -R ${CELIX_TEST_NAME}
                    COMMAND echo "::endgroup::"
                    DEPENDS ${CMD_FIRST_ELEMENT} #depend on test target, so that it is build before running the test
            )
            add_dependencies(test_on_ci ${CELIX_TEST_NAME}_on_ci)
            return()
        endif ()

    endif ()

    add_test(NAME ${CELIX_TEST_NAME} COMMAND ${COMMAND_ARG} ${CELIX_TEST_UNPARSED_ARGUMENTS})
endfunction()
