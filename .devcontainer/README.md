<!--
Licensed to the Apache Software Foundation (ASF) under one or more
contributor license agreements.  See the NOTICE file distributed with
this work for additional information regarding copyright ownership.
The ASF licenses this file to You under the Apache License, Version 2.0
(the "License"); you may not use this file except in compliance with
the License.  You may obtain a copy of the License at
   
    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

# Apache Celix Development Container

## Introduction

This directory contains a [DevContainer](https://containers.dev) setup for developing Apache Celix inside a container.

Although Apache Celix can be built using CMake with APT-installed dependencies or Conan with Conan-installed/built
dependencies, this DevContainer setup only supports Conan.

Please note, the DevContainer setup is not broadly tested and might not work on all systems.
It has been tested on Ubuntu 23.10 and Fedora 40.

## VSCode Usage

VSCode has built-in support for DevContainers.
Simply launch VSCode using the Celix workspace folder, and you will be prompted to open the workspace in a container.

VSCode ensures that your host `.gitconfig` file, `.gnupg` directory, and SSH agent forwarding are available in the
container.

## CLion Usage

CLion 2025.3.1 includes DevContainer support (including Podman), so you can open this repository directly
using the IDE's DevContainer workflow. Once the container is built, select the Conan-generated profile
from `CMakeUserPresets.json` in "Settings -> Build, Execution, Deployment -> CMake".

## Running tests
Tests can be run using ctest.
When building with Conan, run tests from the build directory after configuring/building:

```shell
cd build
ctest --output-on-failure
```
