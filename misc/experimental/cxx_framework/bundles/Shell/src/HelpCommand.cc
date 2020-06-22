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

#include "commands.h"

#include "celix/Api.h"
#include "celix/IShellCommand.h"


namespace {

    void help(std::shared_ptr<celix::BundleContext> ctx, const std::string &, const std::vector<std::string> &commandArguments, std::ostream &out, std::ostream &) {

        if (commandArguments.empty()) { //only command -> overview

            std::string hasCommandNameFilter = std::string{"("} + celix::IShellCommand::COMMAND_NAME + "=*)";
            std::vector<std::string> commands{};
            ctx->buildUseService<celix::IShellCommand>()
                    .setLimit(0)
                    .setCallback([&](celix::IShellCommand & /*cmd*/, const celix::Properties &props) {
                        commands.push_back(props.get(celix::IShellCommand::COMMAND_NAME, "!Error!"));
                    })
                    .setFilter(hasCommandNameFilter)
                    .use();

            hasCommandNameFilter = std::string{"("} + celix::SHELL_COMMAND_FUNCTION_COMMAND_NAME + "=*)";

            auto use = [&](const celix::ShellCommandFunction&, const celix::Properties &props) {
                commands.push_back(props.get(celix::SHELL_COMMAND_FUNCTION_COMMAND_NAME, "!Error!"));
            };
            ctx->buildUseFunctionService<celix::ShellCommandFunction>(celix::SHELL_COMMAND_FUNCTION_SERVICE_NAME)
                    .setLimit(0)
                    .setFilter(hasCommandNameFilter)
                    .setCallback(use)
                .use();

            //TODO useCService with a shell command service struct

            out << "Available commands: " << std::endl;
            for (auto &name : commands) {
                out << "|- " << name << std::endl;
            }
        } else { //details
            for (auto &cmd : commandArguments) {
                std::string commandNameFilter = std::string{"("} + celix::IShellCommand::COMMAND_NAME + "=" + cmd + ")";
                bool found = ctx->buildUseService<celix::IShellCommand>()
                        .setFilter(commandNameFilter)
                        .setCallback([&](celix::IShellCommand &, const celix::Properties &props) {
                            out << "Command Name       : " << props.get(celix::IShellCommand::COMMAND_NAME, "!Error!") << std::endl;
                            out << "Command Usage      : " << props.get(celix::IShellCommand::COMMAND_USAGE, "!Error!") << std::endl;
                            out << "Command Description: " << props.get(celix::IShellCommand::COMMAND_DESCRIPTION, "!Error!") << std::endl;

                        })
                        .use();
                if (!found) {
                    commandNameFilter = std::string{"("} + celix::SHELL_COMMAND_FUNCTION_COMMAND_NAME + "=" + cmd + ")";
                    auto use = [&](const celix::ShellCommandFunction &, const celix::Properties &props) {
                        out << "Command Name       : " << props.get(celix::SHELL_COMMAND_FUNCTION_COMMAND_NAME, "!Error!") << std::endl;
                        out << "Command Usage      : " << props.get(celix::SHELL_COMMAND_FUNCTION_COMMAND_USAGE, "!Error!") << std::endl;
                        out << "Command Description: " << props.get(celix::SHELL_COMMAND_FUNCTION_COMMAND_DESCRIPTION, "!Error!") << std::endl;
                    };
                    ctx->buildUseFunctionService<celix::ShellCommandFunction>(celix::SHELL_COMMAND_FUNCTION_SERVICE_NAME)
                            .setFilter(commandNameFilter)
                            .setCallback(use)
                            .use();
                }
                if (!found) {
                    //TODO use C cmd service
                }
                if (!found) {
                    out << "Command '" << cmd << "' not available" << std::endl;
                }
                out << std::endl;
            }
        }
    }
}

celix::ServiceRegistration celix::impl::registerHelp(const std::shared_ptr<celix::BundleContext>& ctx) {
    using namespace std::placeholders;
    celix::ShellCommandFunction cmd = std::bind(&help, ctx, _1, _2, _3, _4);

    celix::Properties props{};
    props[celix::SHELL_COMMAND_FUNCTION_COMMAND_NAME] = "help";
    props[celix::SHELL_COMMAND_FUNCTION_COMMAND_USAGE] = "help [command name]";
    props[celix::SHELL_COMMAND_FUNCTION_COMMAND_DESCRIPTION] = "display available commands and description.";
    return ctx->registerFunctionService(celix::SHELL_COMMAND_FUNCTION_SERVICE_NAME, std::move(cmd), std::move(props));
}