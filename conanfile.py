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

from conan import ConanFile, conan_version
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain
from conan.tools.scm import Version
from conan.tools.files import copy
import os


required_conan_version = ">=1.32.0"


class CelixConan(ConanFile):
    name = "celix"
    version = "2.3.0"
    homepage = "https://celix.apache.org"
    url = "https://github.com/apache/celix.git"
    topics = ("conan", "celix", "osgi", "embedded", "linux", "C/C++")
    exports_sources = ("CMakeLists.txt", "bundles*", "cmake*", "!cmake-build*", "examples*", "libs*", "misc*",
                       "LICENSE", "!examples/conan_test_package*")
    generators = "CMakeDeps", "VirtualRunEnv"
    settings = "os", "arch", "compiler", "build_type"
    license = " Apache-2.0"
    description = "Apache Celix is an implementation of the OSGi specification adapted to C and C++ (C++17). " \
                  "It is a framework to develop (dynamic) modular software applications " \
                  "using component and/or service-oriented programming."

    _celix_defaults = {
        "enable_testing": False,
        "enable_code_coverage": False,
        "enable_address_sanitizer": False,
        "enable_undefined_sanitizer": False,
        "enable_thread_sanitizer": False,
        "enable_testing_dependency_manager_for_cxx11": False,
        "enable_testing_for_cxx14": False,
        "build_all": False,
        "build_deployment_admin": False,
        "build_http_admin": False,
        "build_log_service": False,
        "build_log_helper": False,
        "build_log_service_api": False,
        "build_syslog_writer": False,
        "build_pubsub": False,
        "build_pubsub_wire_protocol_v1": False,
        "build_pubsub_wire_protocol_v2": False,
        "build_pubsub_json_serializer": False,
        "build_pubsub_avrobin_serializer": False,
        "build_pubsub_psa_zmq": False,
        "build_pubsub_examples": False,
        "build_pubsub_integration": False,
        "build_pubsub_psa_tcp": False,
        "build_pubsub_psa_udp_mc": False,
        "build_pubsub_psa_ws": False,
        "build_pubsub_discovery_etcd": False,
        "build_cxx_remote_service_admin": False,
        "build_cxx_rsa_integration": False,
        "build_remote_service_admin": False,
        "build_rsa_remote_service_admin_dfi": False,
        "build_rsa_discovery_common": False,
        "build_rsa_discovery_configured": False,
        "build_rsa_discovery_etcd": False,
        "build_rsa_remote_service_admin_shm_v2": False,
        "build_rsa_json_rpc": False,
        "build_rsa_discovery_zeroconf": False,
        "build_shell": False,
        "build_shell_api": False,
        "build_remote_shell": False,
        "build_shell_bonjour": False,
        "build_shell_tui": False,
        "build_shell_wui": False,
        "build_components_ready_check": False,
        "build_examples": False,
        "build_celix_etcdlib":  False,
        "build_launcher": False,
        "build_promises": False,
        "build_pushstreams": False,
        "build_experimental": False,
        "build_celix_dfi": False,
        "build_dependency_manager": False,
        "build_dependency_manager_cxx": False,
        "build_framework": False,
        "build_rcm": False,
        "build_utils": False,
        "celix_cxx14": True,
        "celix_cxx17": True,
        "celix_install_deprecated_api": False,
        "celix_use_compression_for_bundle_zips": True,
        "enable_cmake_warning_tests": False,
        "enable_testing_on_ci": False,
        "framework_curlinit": True,
        "enable_ccache": False,
    }
    options = {
        "celix_err_buffer_size": ["ANY"],
    }
    default_options = {
        "celix_err_buffer_size": "512",
    }

    for comp in _celix_defaults.keys():
        options[comp] = [True, False]
    del comp

    _cmake = None

    def validate(self):
        if self.settings.os != "Linux" and self.settings.os != "Macos":
            raise ConanInvalidConfiguration("Celix is only supported for Linux/Macos")

        if self.options.build_rsa_remote_service_admin_shm_v2 and self.settings.os != "Linux":
            raise ConanInvalidConfiguration("Celix build_rsa_remote_service_admin_shm_v2 is only supported for Linux")

        if self.options.build_rsa_discovery_zeroconf and self.settings.os != "Linux":
            raise ConanInvalidConfiguration("Celix build_rsa_discovery_zeroconf is only supported for Linux")

        if self.options.build_shell_bonjour and self.settings.os != "Linux":
            raise ConanInvalidConfiguration("Celix build_shell_bonjour is only supported for Linux")

        try:
            val = int(self.options.celix_err_buffer_size)
            if val <= 0:
                raise ValueError
        except ValueError:
            raise ConanInvalidConfiguration("celix_err_buffer_size must be a positive number")

    def package_id(self):
        del self.info.options.build_all
        # the followings are not installed
        del self.info.options.build_pubsub_integration
        del self.info.options.build_pubsub_examples
        del self.info.options.build_cxx_rsa_integration
        del self.info.options.build_examples
        del self.info.options.build_shell_bonjour
        del self.info.options.enable_testing_dependency_manager_for_cxx11
        del self.info.options.enable_testing_for_cxx14
        del self.info.options.enable_cmake_warning_tests
        del self.info.options.enable_testing_on_ci
        del self.info.options.enable_ccache

    def build_requirements(self):
        if self.options.enable_testing:
            self.test_requires("gtest/1.10.0")
            self.test_requires("cpputest/4.0")
        if self.options.enable_ccache:
            self.build_requires("ccache/4.6")

    def configure(self):
        # copy options to options, fill in defaults if not set
        options = {}
        for opt in self._celix_defaults.keys():
            options[opt] = self.options.get_safe(opt).value
            if options[opt] is None:
                options[opt] = self._celix_defaults[opt]

        if options["build_all"]:
            for opt in options.keys():
                if opt.startswith('build_'):
                    options[opt] = True

        if self.settings.os != "Linux":
            options["build_rsa_remote_service_admin_shm_v2"] = False
            options["build_rsa_discovery_zeroconf"] = False
            options["build_shell_bonjour"] = False

        if options["enable_code_coverage"]:
            options["enable_testing"] = True

        if options["build_examples"]:
            options["build_shell_tui"] = True
            options["build_shell_wui"] = True
            options["build_log_service"] = True
            options["build_syslog_writer"] = True

        if options["build_shell_bonjour"]:
            options["build_shell"] = True

        if options["build_deployment_admin"]:
            options["build_framework"] = True

        if options["build_cxx_rsa_integration"]:
            options["build_cxx_remote_service_admin"] = True
            options["build_pushstreams"] = True
            options["build_promises"] = True
            options["build_log_helper"] = True
            options["build_shell"] = True
            options["build_shell_tui"] = True
            options["build_shell_api"] = True
            options["build_pubsub"] = True
            options["build_pubsub_wire_protocol_v2"] = True
            options["build_pubsub_json_serializer"] = True
            options["build_pubsub_psa_zmq"] = True
            options["build_pubsub_discovery_etcd"] = True

        if options["build_pubsub_integration"]:
            options["build_pubsub"] = True
            options["build_shell_tui"] = True
            options["build_pubsub_json_serializer"] = True
            options["build_pubsub_wire_protocol_v2"] = True
            options["build_pubsub_wire_protocol_v1"] = True
            options["enable_testing"] = True

        if options["build_pubsub_examples"]:
            options["build_log_service"] = True
            options["build_shell_tui"] = True
            options["build_pubsub_json_serializer"] = True
            options["build_pubsub_discovery_etcd"] = True
            options["build_pubsub_wire_protocol_v2"] = True
            options["build_pubsub_wire_protocol_v1"] = True

        if options["build_pubsub_discovery_etcd"]:
            options["build_pubsub"] = True
            options["build_celix_etcdlib"] = True

        if options["build_pubsub_psa_ws"]:
            options["build_http_admin"] = True
            options["build_pubsub"] = True

        if options["build_pubsub_psa_zmq"] or options["build_pubsub_psa_tcp"] \
                or options["build_pubsub_psa_udp_mc"]:
            options["build_pubsub"] = True

        if options["build_pubsub_wire_protocol_v1"]:
            options["build_pubsub"] = True

        if options["build_pubsub_wire_protocol_v2"]:
            options["build_pubsub"] = True

        if options["build_pubsub_json_serializer"] or options["build_pubsub_avrobin_serializer"]:
            options["build_pubsub"] = True

        if options["build_pubsub"]:
            options["build_framework"] = True
            options["build_celix_dfi"] = True
            options["build_shell_api"] = True
            options["build_log_helper"] = True
            options["celix_install_deprecated_api"] = True

        if options["build_cxx_remote_service_admin"]:
            options["build_framework"] = True
            options["build_log_helper"] = True
            options["celix_cxx17"] = True

        if options["build_rsa_discovery_etcd"]:
            options["build_celix_etcdlib"] = True
            options["build_rsa_discovery_common"] = True

        if options["build_rsa_discovery_configured"]:
            options["build_rsa_discovery_common"] = True

        if options["build_rsa_discovery_common"] or options["build_rsa_discovery_zeroconf"] \
                or options["build_rsa_remote_service_admin_dfi"] or options["build_rsa_json_rpc"] \
                or options["build_rsa_remote_service_admin_shm_v2"]:
            options["build_remote_service_admin"] = True

        if options["build_remote_service_admin"]:
            options["build_framework"] = True
            options["build_log_helper"] = True
            options["build_celix_dfi"] = True
            options["celix_install_deprecated_api"] = True

        if options["build_remote_shell"]:
            options["build_shell"] = True

        if options["build_shell_wui"]:
            options["build_shell"] = True
            options["build_http_admin"] = True

        if options["build_shell_tui"]:
            options["build_shell"] = True

        if options["build_shell"]:
            options["build_shell_api"] = True
            options["build_log_helper"] = True
            options["build_framework"] = True

        if options["build_http_admin"]:
            options["build_framework"] = True

        if options["build_syslog_writer"]:
            options["build_log_service"] = True

        if options["build_log_service"]:
            options["build_log_service_api"] = True
            options["build_shell_api"] = True
            options["build_framework"] = True
            options["build_log_helper"] = True

        if options["build_shell_api"]:
            options["build_utils"] = True

        if options["build_log_helper"]:
            options["build_log_service_api"] = True
            options["build_framework"] = True

        if options["build_log_service_api"]:
            options["build_utils"] = True
            if options["celix_install_deprecated_api"]:
                options["build_framework"] = True

        if options["build_components_ready_check"]:
            options["build_framework"] = True

        if options["build_rcm"]:
            options["build_utils"] = True

        if options["build_launcher"] or options["build_dependency_manager"]:
            options["build_framework"] = True

        if options["build_dependency_manager_cxx"]:
            options["build_framework"] = True
            options["celix_cxx14"] = True

        if options["build_celix_dfi"]:
            options["build_utils"] = True

        if options["build_framework"]:
            options["build_utils"] = True

        if options["build_pushstreams"]:
            options["build_promises"] = True

        if options["build_promises"]:
            options["celix_cxx17"] = True

        if options["celix_cxx17"]:
            options["celix_cxx14"] = True

        for opt in self._celix_defaults.keys():
            setattr(self.options, opt, options[opt])
        del options

        # Conan 2 does not support set dependency option in requirements()
        # https://github.com/conan-io/conan/issues/14528#issuecomment-1685344080
        if self.options.build_utils:
            self.options['libzip'].shared = True
        if self.options.build_framework or self.options.build_pubsub:
            self.options['util-linux-libuuid'].shared = True
        if ((self.options.build_framework and self.options.framework_curlinit)
                or self.options.build_celix_etcdlib or self.options.build_deployment_admin
                or self.options.build_rsa_discovery_common or self.options.build_rsa_remote_service_admin_dfi
                or self.options.build_launcher):
            self.options['libcurl'].shared = True
            self.options['openssl'].shared = True
        if self.options.build_deployment_admin:
            self.options['zlib'].shared = True
        if self.options.enable_testing:
            self.options['gtest'].shared = True
            if self.options.enable_address_sanitizer:
                self.options["cpputest"].with_leak_detection = False
        if (self.options.build_rsa_discovery_common or self.options.build_shell_bonjour or
                (self.options.build_rsa_remote_service_admin_dfi and self.options.enable_testing)):
            self.options['libxml2'].shared = True
        if self.options.build_pubsub_psa_zmq:
            self.options['zeromq'].shared = True
            self.options['czmq'].shared = True
        if self.options.build_http_admin or self.options.build_rsa_discovery_common \
                or self.options.build_rsa_remote_service_admin_dfi:
            self.options['civetweb'].shared = True
            self.options['openssl'].shared = True
        if self.options.build_celix_dfi:
            self.options['libffi'].shared = True
        if self.options.build_celix_dfi or self.options.build_celix_etcdlib:
            self.options['jansson'].shared = True

    def requirements(self):
        if self.options.build_utils:
            self.requires("libzip/[>=1.7.3 <2.0.0]")
        if self.options.build_framework or self.options.build_pubsub:
            self.requires("util-linux-libuuid/2.39")
        if ((self.options.build_framework and self.options.framework_curlinit)
                or self.options.build_celix_etcdlib or self.options.build_deployment_admin
                or self.options.build_rsa_discovery_common or self.options.build_rsa_remote_service_admin_dfi
                or self.options.build_launcher):
            self.requires("libcurl/[>=7.64.1 <8.0.0]")
        if self.options.build_deployment_admin:
            self.requires("zlib/[>=1.2.8 <2.0.0]")
        if (self.options.build_rsa_discovery_common or self.options.build_shell_bonjour or
                (self.options.build_rsa_remote_service_admin_dfi and self.options.enable_testing)):
            self.requires("libxml2/[>=2.9.9 <3.0.0]")
        if self.options.build_cxx_remote_service_admin:
            self.requires("rapidjson/[>=1.1.0 <2.0.0]")
        if self.options.build_pubsub_psa_zmq:
            self.requires("zeromq/4.3.4")
            self.requires("czmq/4.2.0")
        if self.options.build_http_admin or self.options.build_rsa_discovery_common \
                or self.options.build_rsa_remote_service_admin_dfi:
            self.requires("civetweb/1.16")
        if self.options.build_celix_dfi:
            self.requires("libffi/[>=3.2.1 <4.0.0]")
        if self.options.build_celix_dfi or self.options.build_celix_etcdlib:
            self.requires("jansson/[>=2.12 <3.0.0]")
        if self.options.build_rsa_discovery_zeroconf or self.options.build_shell_bonjour:
            # TODO: To be replaced with mdnsresponder/1790.80.10, resolve some problems of mdnsresponder
            # https://github.com/conan-io/conan-center-index/pull/16254
            self.requires("mdnsresponder/1310.140.1")
        # the latest civetweb (1.16) is not ready for openssl3
        self.requires("openssl/1.1.1t", override=True)
        self.validate()

    def generate(self):
        tc = CMakeToolchain(self)
        for opt in self._celix_defaults.keys():
            tc.cache_variables[opt.upper()] = self.options.get_safe(opt)
        if self.options.enable_testing:
            lst = [x.ref.name for x in self.requires.values()]
            if "mdnsresponder" in lst:
                tc.cache_variables["BUILD_ERROR_INJECTOR_MDNSRESPONDER"] = "ON"
        tc.cache_variables["CELIX_ERR_BUFFER_SIZE"] = str(self.options.celix_err_buffer_size)
        # tc.cache_variables["CMAKE_PROJECT_Celix_INCLUDE"] = os.path.join(self.build_folder, "conan_paths.cmake")
        # the following is workaround for https://github.com/conan-io/conan/issues/7192
        if self.settings.os == "Linux":
            tc.cache_variables["CMAKE_EXE_LINKER_FLAGS"] = "-Wl,--unresolved-symbols=ignore-in-shared-libs"
        elif self.settings.os == "Macos":
            tc.cache_variables["CMAKE_EXE_LINKER_FLAGS"] = "-Wl,-undefined -Wl,dynamic_lookup"
        v = Version(self.version)
        tc.cache_variables["CELIX_MAJOR"] = str(v.major.value)
        tc.cache_variables["CELIX_MINOR"] = str(v.minor.value)
        tc.cache_variables["CELIX_MICRO"] = str(v.patch.value)
        tc.generate()

    def _configure_cmake(self):
        if self._cmake:
            return self._cmake
        self._cmake = CMake(self)
        self._cmake.configure()
        return self._cmake

    def build(self):
        # self._patch_sources()
        cmake = self._configure_cmake()
        cmake.build()

    def package(self):
        copy(self, "LICENSE", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))
        cmake = self._configure_cmake()
        cmake.install()

    def package_info(self):
        # enable imports() of conanfile.py to collect bundles from the local cache using @bindirs
        # check https://docs.conan.io/en/latest/reference/conanfile/methods.html#imports
        self.cpp_info.bindirs = ["bin", os.path.join("share", self.name, "bundles")]
        self.cpp_info.build_modules["cmake"].append(os.path.join("lib", "cmake", "Celix", "CelixConfig.cmake"))
        self.cpp_info.build_modules["cmake_find_package"].append(os.path.join("lib", "cmake",
                                                                              "Celix", "CelixConfig.cmake"))
        self.cpp_info.set_property("cmake_build_modules", [os.path.join("lib", "cmake", "Celix", "CelixConfig.cmake")])
