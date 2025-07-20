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

Run the build using ninja build:

```bash
ninja -C build
```

After building, run the tests:

```bash
ctest --output-on-failure --test-dir build
```

With exception of documentation changes, always build and run the test before submitting changes that affect the build or tests.

# Coding Style

Refer to the [development guide](documents/development/README.md) for the project's coding conventions. 
New files should be formatted with the project's `.clang-format` configuration.
