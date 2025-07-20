# Building and Testing

## Preparation

Install the build dependencies using apt:

```bash
sudo apt-get update
sudo apt-get install -yq --no-install-recommends \
          build-essential \
          ninja-build \
          curl \
          uuid-dev \
          libzip-dev \
          libjansson-dev \
          libcurl4-openssl-dev \
          default-jdk \
          cmake \
          libffi-dev \
          libxml2-dev \
          rapidjson-dev \
          libavahi-compat-libdnssd-dev \
          libcivetweb-dev \
          civetweb \
          ccache
```

Set up the cmake build directory:

```bash
mkdir build
cmake \
  -DENABLE_TESTING=ON \
  -DRSA_JSON_RPC=ON \
  -DRSA_REMOTE_SERVICE_ADMIN_SHM_V2=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  -G Ninja \ 
  -S . -B build
```

## Building

Run cmake, but use the offline mode for fetchcontent: 

```bash
cmake \
  -DCMAKE_FETCHCONTENT_FULLY_DISCONNECTED=ON \
  -DENABLE_TESTING=ON \
  -DRSA_JSON_RPC=ON \
  -DRSA_REMOTE_SERVICE_ADMIN_SHM_V2=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  -G Ninja \ 
  -S . -B build
```

Run the build using ninja build. The build can take a long time, so allow for a
long wait (about 10 minutes) when invoking the build:

```bash
ninja -C build
```

After building, run the tests for the components you changed. Run `ctest` from
the appropriate `build` subdirectory when possible.
For example, to test the shell bundles:

```bash
ctest --output-on-failure --test-dir build/bundles/shell
```

When working on core libraries such as the framework or utils, run the tests
from the main build directory, which can take more than four minutes:

```bash
ctest --output-on-failure --test-dir build
```

With exception of documentation changes, always build and run the test before submitting changes that affect the build or tests.

# Coding Style

Refer to the [development guide](documents/development/README.md) for the project's coding conventions. 
New files should be formatted with the project's `.clang-format` configuration.
