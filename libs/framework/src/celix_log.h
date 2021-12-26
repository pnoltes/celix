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
 *  KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef CELIX_LOG_H_
#define CELIX_LOG_H_

#include <stdio.h>

#include "celix_log_level.h"
#include "celix_errno.h"
#include "framework_exports.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct celix_framework_logger celix_framework_logger_t; //opaque

#define CELIX_FRAMEWORKLOGGER_LOG(logger, level, fmsg, args...) celix_frameworkLogger_log(logger, level, __func__, __FILE__, __LINE__, fmsg, ## args)

#define CELIX_FRAMEWORKLOGGER_LOG_CODE(logger, level, code, fmsg, args...) celix_frameworkLogger_logCode(logger, level, __func__, __FILE__, __LINE__, code, fmsg, ## args)

#define CELIX_FRAMEWORKLOGGER_LOG_IF_ERROR(logger, status, error, fmsg, args...) \
    if (status != CELIX_SUCCESS) { \
        if (error != NULL) { \
            CELIX_FRAMEWORKLOGGER_LOG_CODE(logger, CELIX_LOG_LEVEL_ERROR, status, #fmsg";\n Cause: %s", ## args, (char*) error); \
        } else { \
            CELIX_FRAMEWORKLOGGER_LOG_CODE(logger, CELIX_LOG_LEVEL_ERROR, status, #fmsg, ## args); \
        } \
    }


celix_framework_logger_t* celix_frameworkLogger_create(celix_log_level_e activeLogLevel);
void celix_frameworkLogger_destroy(celix_framework_logger_t* logger);
void celix_frameworkLogger_setLogCallback(celix_framework_logger_t* logger, void* logHandle, void (*logFunction)(void* handle, celix_log_level_e level, const char* file, const char *function, int line, const char *format, va_list formatArgs));
celix_framework_logger_t* celix_frameworkLogger_globalLogger();

void celix_frameworkLogger_log(celix_framework_logger_t* logger, celix_log_level_e level, const char *func, const char *file, int line,
              const char *format, ...);

void celix_frameworkLogger_logCode(celix_framework_logger_t* logger, celix_log_level_e level, const char *func, const char *file, int line,
                  celix_status_t code, const char *format, ...);

void celix_frameworkLogger_vlog(celix_framework_logger_t* logger, celix_log_level_e level, const char* file, const char* function, int line, const char* format, va_list args);

#ifdef __cplusplus
}
#endif

#endif /* CELIX_LOG_H_ */
