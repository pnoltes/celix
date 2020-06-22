/**
 *Licensed to the Apache Software Foundation (ASF) under one
 *or more contributor license agreements.  See the NOTICE file
 *distributed with this work for additional information
 *regarding copyright ownership.  The ASF licenses this file
 *to you under the Apache License, Version 2.0 (the
 *"License"); you may not use this file except in compliance
 *with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *Unless required by applicable law or agreed to in writing,
 *software distributed under the License is distributed on an
 *"AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 *specific language governing permissions and limitations
 *under the License.
 */




#include "celix/Api.h"
#include "celix/IShellCommand.h"
#include "celix/IShell.h"

#include "commands.h"

#ifdef __APPLE__
#include <mach-o/getsect.h>
#include <dlfcn.h>

static uint8_t *resources = nullptr;
#else
extern const uint8_t resources[]       asm("_binary_celix_cxx_shell_resources_zip_start");
extern const uint8_t resources_end[]   asm("_binary_celix_cxx_shell_resources_zip_end");
#endif

namespace {

    class Shell : public celix::IShell {
    public:
        explicit Shell(std::shared_ptr<celix::BundleContext> _ctx) : ctx{std::move(_ctx)} {}

        bool executeCommandLine(const std::string &commandLine, std::ostream &out, std::ostream &err) noexcept override {
            std::string cmdName{};
            std::vector<std::string> cmdArgs{};

            char *savePtr = nullptr;
            char *cl = strndup(commandLine.c_str(), 1024 * 1024);
            char *token = strtok_r(cl, " ", &savePtr);
            while (token != nullptr) {
                if (cmdName.empty()) {
                    cmdName = std::string{token};
                } else {
                    cmdArgs.emplace_back(std::string{token});
                }
                token = strtok_r(nullptr, " ", &savePtr);
            }
            free(cl);

            if (cmdName.empty()) {
                return true; /*nop, just enter and maybe some white spaces*/
            } else {
                return callShellCommands(cmdName, cmdArgs, out, err);
            }
        }

    private:
        bool callShellCommands(const std::string &cmdName, const std::vector<std::string> cmdArgs, std::ostream &out, std::ostream &err) {
            bool commandCalled;

            //first try to call IShellCommand services.
            std::string filter = std::string{"("} + celix::IShellCommand::COMMAND_NAME + "=" + cmdName + ")";
            commandCalled = ctx->buildUseService<celix::IShellCommand>()
                    .setCallback([&](celix::IShellCommand &cmd) {
                        cmd.executeCommand(cmdName, cmdArgs, out, err);
                    })
                    .setFilter(filter)
                    .use();

            if (!commandCalled) {
                //if no IShellCommand service is found for the command name, try calling a ShellCommandFunction service.
                std::string f = std::string{"("} + celix::SHELL_COMMAND_FUNCTION_COMMAND_NAME + "=" + cmdName + ")";
                auto use = [&](const celix::ShellCommandFunction &cmd) -> void {
                    cmd(cmdName, cmdArgs, out, err);
                };
                commandCalled = ctx->buildUseFunctionService<celix::ShellCommandFunction>(celix::SHELL_COMMAND_FUNCTION_SERVICE_NAME)
                        .setFilter(f)
                        .setCallback(use)
                        .use();
            }

            // TODO use C command service struct

            if (!commandCalled) {
                out << "Command '" << cmdName << "' not available. Type 'help' to see a list of available commands." << std::endl;
            }

            return commandCalled;
        }
    private:
        const std::shared_ptr<spdlog::logger> logger{celix::getLogger("celix::bundle::ShellActivator")};

        std::shared_ptr<celix::BundleContext> ctx;
    };

    class ShellBundleActivator : public celix::IBundleActivator {
    public:
        explicit ShellBundleActivator(const std::shared_ptr<celix::BundleContext>& ctx) {
            //TODO ensure fixed framework thread that call ctor/dtor bundle activators
            registrations.push_back(celix::impl::registerLb(ctx));
            registrations.push_back(celix::impl::registerHelp(ctx));
            registrations.push_back(celix::impl::registerStop(ctx));
            registrations.push_back(celix::impl::registerStart(ctx));
            registrations.push_back(celix::impl::registerInspect(ctx));
            registrations.push_back(celix::impl::registerQuery(ctx));
            registrations.push_back(celix::impl::registerVersion(ctx));
            registrations.push_back(celix::impl::registerLogging(ctx));

            registrations.push_back(ctx->registerService<celix::IShell>(std::make_shared<Shell>(ctx)));
        }
    private:
        std::vector<celix::ServiceRegistration> registrations{};
    };

    //NOTE that eventually the (ctor) bundle register will be generated by a CMake command (i.e. add_bundle)
    //This also applies for the resources, resources_end asm entries or the __APPLE__ variant
    __attribute__((constructor))
    void registerShellBundle() {

#ifdef __APPLE__
        //get library info using a addr in the lib (the bundle register function)
        Dl_info info;
        const void *addr = reinterpret_cast<void*>(&registerShellBundle);
        dladdr(addr, &info);

        //get mach header from Dl_info
        auto *header = static_cast<struct mach_header_64*>(info.dli_fbase);

        //get section from mach header based on the seg and sect name
        const struct section_64 *sect = getsectbynamefromheader_64(header, "resources", "celix_cxx_shell");

        //NOTE reading directly form the sect->addr is not possible (BAD_ACCESS), so copy sect part from dylib file.

        //alloc buffer to store resources zip
        size_t resourcesLen = sect->size;
        resources = (uint8_t*)malloc(resourcesLen);

        //read from dylib. note that the dylib location is in the Dl_info struct.
        errno = 0;
        FILE *dylib = fopen(info.dli_fname, "r");
        size_t read = 0;
        if (dylib != nullptr) {
            fseek(dylib, sect->offset, SEEK_SET);
            read = fread(resources, 1, sect->size, dylib);
        }
        if (dylib == nullptr || read != sect->size) {
            celix::getLogger("celix")->error("Error reading resources from dylib {}: {}", info.dli_fname, strerror(errno));
            free(resources);
            resources = nullptr;
            resourcesLen = 0;
        }
        if (dylib != nullptr) {
            fclose(dylib);
        }
#else
        size_t resourcesLen = resources_end - resources;
#endif


        celix::Properties manifest{};
        manifest[celix::MANIFEST_BUNDLE_NAME] = "Celix Shell";
        manifest[celix::MANIFEST_BUNDLE_GROUP] = "Celix";
        manifest[celix::MANIFEST_BUNDLE_VERSION] = "1.0.0";
        celix::registerStaticBundle<ShellBundleActivator>("celix::Shell", std::move(manifest), resources, resourcesLen);
    }

    __attribute__((destructor))
    void cleanupShellBundle() {
#ifdef __APPLE__
        free(resources);
#endif
    }
}
