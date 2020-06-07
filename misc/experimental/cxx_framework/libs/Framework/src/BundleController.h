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

#ifndef CXX_CELIX_BUNDLECONTROLLER_H
#define CXX_CELIX_BUNDLECONTROLLER_H

#include <fstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


#include <zip.h>
#include <dirent.h>

#include "celix/IBundle.h"
#include "celix/BundleContext.h"
#include "Bundle.h"

namespace celix {
    class BundleController {
    public:
        BundleController(
                std::function<celix::IBundleActivator*(const std::shared_ptr<celix::BundleContext>&)> _actFactory,
                std::shared_ptr<celix::Bundle> _bnd,
                std::shared_ptr<celix::BundleContext> _ctx,
                const uint8_t *rZip,
                size_t rZipLen) :
                actFactory{std::move(_actFactory)}, bnd{std::move(_bnd)}, ctx{std::move(_ctx)}, resourcesZip{rZip}, resourcesZipLen{rZipLen} {}

        BundleController(const BundleController&) = delete;
        BundleController& operator=(const BundleController&) = delete;

        //specific part
        bool transitionTo(BundleState desired) {
            bool success = false;
            std::lock_guard<std::mutex> lck{mutex};
            const BundleState state = bnd->state();
            if (state == desired) {
                //nop
                success = true;
            } else if (state == BundleState::INSTALLED && desired == BundleState::ACTIVE) {
                success = createBundleCache();
                if (success) {
                    act = std::unique_ptr<celix::IBundleActivator>{actFactory(ctx)};
                    bnd->setState(BundleState::ACTIVE);
                }
            } else if (state == BundleState::ACTIVE && desired == BundleState::INSTALLED ) {
                act = nullptr;
                success = deleteBundleCache();
                bnd->setState(BundleState::INSTALLED); //note still going to installed
            } else {
                //TODO log to cc file and add logger
                //LOG(ERROR) << "Unexpected desired state " << desired << " from state " << bndState << std::endl;
                //LOG(ERROR) << "Unexpected desired/form state combination " << std::endl;
            }
            return success;
        }

        std::shared_ptr<celix::Bundle> bundle() const { return bnd; }
        std::shared_ptr<celix::BundleContext> context() const { return ctx; }
    private:
        bool createBundleCache() {
            auto bundleCache =  bundle()->cacheRoot();
            bool success = createDir(bundleCache);
            if (success) {
                //auto manifestPath = bundle()->absPathForCacheEntry("META-INF/manifest.mf");
                //TODO success = celix::storeProperties(bundle()->manifest(), manifestPath);
            }
            if (success) {
                success = extractResources(bundleCache);
            }
            return success;
        }

        bool deleteBundleCache() {
            return deleteDir(bundle()->cacheRoot());
        }

        bool deleteDir(const std::string &path) {
            bool success = false;
            DIR *dir;
            dir = opendir(path.c_str());
            if (dir == NULL) {
                //TODO move to cc file and add logger
                //LOG(WARNING) << "Cannot delete dir " << path << ". " << strerror(errno) << std::endl;
            } else {
                struct dirent* dent = NULL;
                errno = 0;
                dent = readdir(dir);
                while (errno == 0 && dent != NULL) {
                    if ((strcmp((dent->d_name), ".") != 0) && (strcmp((dent->d_name), "..") != 0)) {
                        char subdir[512];
                        snprintf(subdir, 512, "%s/%s", path.c_str(), dent->d_name);

                        struct stat st;
                        if (stat(subdir, &st) == 0) {
                            if (S_ISDIR (st.st_mode)) {
                                std::string sd{subdir};
                                success = deleteDir(sd);
                            } else {
                                if (remove(subdir) != 0) {
                                    //TODO move to cc file and add logger
                                    //LOG(WARNING) << "Cannot delete dir " << path << ". " << strerror(errno) << std::endl;
                                }
                            }
                        }
                    }
                    errno = 0;
                    dent = readdir(dir);
                }

                if (errno != 0) {
                    //TODO move to cc file and add logger
                    //LOG(WARNING) << "Cannot delete dir " << path << ". " << strerror(errno) << std::endl;
                } else if (closedir(dir) != 0) {
                    //TODO move to cc file and add logger
                    //LOG(WARNING) << "Cannot close dir " << path << ". " << strerror(errno) << std::endl;
                } else {
                    if (rmdir(path.c_str()) != 0) {
                        //TODO move to cc file and add logger
                        //LOG(WARNING) << "Cannot delete dir " << path << ". " << strerror(errno) << std::endl;
                    } else {
                        success = true;
                    }
                }
            }

            return success;
        }

        bool createDir(const std::string &path) {
            bool success = false;

            struct stat st;
            bool exists = stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);

            if (!exists) {
                const char *slashAt = strrchr(path.c_str(), '/');
                if (slashAt == nullptr) {
                    //no sub dir
                    if (mkdir(path.c_str(), S_IRWXU) == 0) {
                        success = true;
                    } else {
                        //TODO move to cc file and add logger
                        //LOG(WARNING) << "Cannot create dir " << path << ". " << strerror(errno) << std::endl;
                    }
                } else {
                    std::string subdir = path.substr(0, slashAt - path.c_str());
                    bool subdirCreated = createDir(subdir);
                    if (subdirCreated) {
                        if (mkdir(path.c_str(), S_IRWXU) == 0) {
                            success = true;
                        } else {
                            //TODO move to cc file and add logger
                            //LOG(WARNING) << "Cannot create dir " << path << ". " << strerror(errno) << std::endl;
                        }
                    } else {
                        //nop, error should be reported in the recursion
                    }
                }
            } else {
                //exists, so true
                success = true;
            }


            return success;
        }

        bool extractResources(const std::string &bundleCache) {
            bool success = false;
            if (resourcesZip != nullptr) {
                zip_error_t error;
                zip_source_t *source = zip_source_buffer_create(resourcesZip, resourcesZipLen, 0, &error);
                if (source != nullptr) {
                    zip_t *zip = zip_open_from_source(source, ZIP_RDONLY, &error);
                    if (zip != nullptr) {
                        extractZipSource(zip, bundleCache);
                        zip_close(zip);
                        zip_source_close(source);
                        success = true;
                    } else {
                        logger->error("Cannot open zip from source: {}", zip_error_strerror(&error));
                        zip_source_free(source);
                    }
                } else {
                    logger->error("Cannot create zip from source: {}", zip_error_strerror(&error));
                }
            } else {
                //no resources
                success = true;
            }
            return success;
        }

        bool extractZipSource(zip_t *zip, const std::string &bundleCache) {
            bool succes = true;
            char buf[128];
            zip_int64_t nrOfEntries = zip_get_num_entries(zip, 0);
            for (int i = 0; i < nrOfEntries; ++i) {
                zip_stat_t st;
                zip_stat_index(zip, i, 0, &st);
                if (st.name[strlen(st.name) - 1] == '/') {
                    //dir
                    logger->error("TODO extract dirs");
                    succes = false;
                } else {
                    //file
                    zip_file_t *f = zip_fopen_index(zip, i, 0);
                    if (f != nullptr) {
                        std::ofstream outf;
                        std::string p = bundleCache + "/" + st.name;
                        outf.open(p);
                        if (!outf.fail()) {
                            zip_int64_t read = zip_fread(f, buf, 128);
                            while (read != 0) {
                                outf.write(buf, read);
                                read = zip_fread(f, buf, 128);
                            }
                            outf.close();
                            logger->trace("extracted file {}", p);
                        } else {
                            logger->error("Cannot open file '{}'", p);
                        }
                        zip_fclose(f);
                    } else {
                        logger->error("Cannot read file from zip: {}", zip_strerror(zip));
                        succes = false;
                        break;
                    }
                }
            }
            return succes;
        }

    private:
        const std::shared_ptr<spdlog::logger> logger{celix::getLogger("celix::bundle::BundleController")};


        const std::function<celix::IBundleActivator*(std::shared_ptr<celix::BundleContext>)> actFactory;
        const std::shared_ptr<celix::Bundle> bnd;
        const std::shared_ptr<celix::BundleContext> ctx;
        const uint8_t *resourcesZip;
        const size_t resourcesZipLen;

        mutable std::mutex mutex{}; //protects act and lock access to the bundle for state transitions
        std::unique_ptr<celix::IBundleActivator> act{nullptr};
    };
}

#endif //CXX_CELIX_BUNDLECONTROLLER_H
