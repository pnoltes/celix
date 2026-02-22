/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <gtest/gtest.h>

#include "celix/FrameworkFactory.h"
#include "celix/BundleContext.h"

class ShellNcuiTestSuite : public ::testing::Test {
public:
    void createFrameworkWithShellBundles(const celix::Properties& config = {}) {
        celix::Properties fwConfig{config};
        fwConfig.set("CELIX_LOGGING_DEFAULT_ACTIVE_LOG_LEVEL", "trace");

        fw = celix::createFramework(std::move(fwConfig));
        ctx = fw->getFrameworkBundleContext();

        shellBundleId = ctx->installBundle(SHELL_BUNDLE_LOCATION);
        EXPECT_GT(shellBundleId, 0);

        shellNcuiBundleId = ctx->installBundle(SHELL_NCUI_BUNDLE_LOCATION);
        EXPECT_GT(shellNcuiBundleId, 0);
    }

    std::shared_ptr<celix::Framework> fw{};
    std::shared_ptr<celix::BundleContext> ctx{};
    long shellBundleId{-1};
    long shellNcuiBundleId{-1};
};

TEST_F(ShellNcuiTestSuite, InstallAndStartBundleTest) {
    createFrameworkWithShellBundles();
}

TEST_F(ShellNcuiTestSuite, InstallAndStartWithAnsiColorsDisabledTest) {
    createFrameworkWithShellBundles({
        {"CELIX_SHELL_USE_ANSI_COLORS", "false"}
    });
}

TEST_F(ShellNcuiTestSuite, StopAndRestartNcuiBundleTest) {
    createFrameworkWithShellBundles();

    EXPECT_TRUE(ctx->stopBundle(shellNcuiBundleId));
    EXPECT_TRUE(ctx->startBundle(shellNcuiBundleId));
}
