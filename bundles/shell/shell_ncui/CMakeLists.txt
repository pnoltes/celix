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

celix_subproject(SHELL_NCUI "Option to enable building the Shell NCurses User Interface bundle" ON)
if (SHELL_NCUI)
    find_package(Curses)
    if (CURSES_FOUND)
        add_celix_bundle(shell_ncui
            SYMBOLIC_NAME "apache_celix_shell_ncui"
            VERSION "1.0.0"
            NAME "Apache Celix Shell NCUI"
            FILENAME celix_shell_ncui
            GROUP "Celix/Shell"
            SOURCES
                src/activator.c
                src/shell_ncui.c
        )

        target_include_directories(shell_ncui PRIVATE src)
        target_link_libraries(shell_ncui PRIVATE Celix::shell_api Celix::framework ${CURSES_LIBRARIES})

        install_celix_bundle(shell_ncui EXPORT celix)

        add_library(Celix::shell_ncui ALIAS shell_ncui)

        if (ENABLE_TESTING)
            add_subdirectory(gtest)
        endif()
    else()
        message(WARNING "SHELL_NCUI enabled, but curses package not found. shell_ncui will not be built.")
    endif()
endif()
