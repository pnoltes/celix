# Building and Testing

To build the project, use Conan. From the repository root run:

```bash
mkdir build
conan install . --build missing --profile debug \
  --options celix/*:build_all=True --options celix/*:enable_address_sanitizer=True \
  --options celix/*:enable_testing=True --options celix/*:enable_ccache=True \
  --conf:build tools.cmake.cmaketoolchain:generator=Ninja \
  --output-folder build
```

After building, run the tests:

```bash
cd build
ctest --output-on-failure --test-command ./workspaces/celix/build/conanrun.sh
```

Always run these commands before submitting changes that affect the build or tests.
