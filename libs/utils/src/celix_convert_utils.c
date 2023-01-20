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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "utils.h"
#include "celix_utils.h"
#include "celix_version.h"

bool celix_utils_convertStringToBool(const char* val, bool defaultValue, bool* converted) {
    bool result = defaultValue;
    if (converted != NULL) {
        *converted = false;
    }
    if (val != NULL) {
        char buf[32];
        snprintf(buf, 32, "%s", val);
        char *trimmed = utils_stringTrim(buf);
        if (strncasecmp("true", trimmed, 5) == 0) {
            result = true;
            if (converted) {
                *converted = true;
            }
        } else if (strncasecmp("false", trimmed, 6) == 0) {
            result = false;
            if (converted) {
                *converted = true;
            }
        }
    }
    return result;
}

double celix_utils_convertStringToDouble(const char* val, double defaultValue, bool* converted) {
    double result = defaultValue;
    if (converted != NULL) {
        *converted = false;
    }
    if (val != NULL) {
        char *endptr;
        double d = strtod(val, &endptr);
        if (endptr != val) {
            result = d;
            if (converted) {
                *converted = true;
            }
        }
    }
    return result;
}

long celix_utils_convertStringToLong(const char* val, long defaultValue, bool* converted) {
    long result = defaultValue;
    if (converted != NULL) {
        *converted = false;
    }
    if (val != NULL) {
        char *endptr;
        long l = strtol(val, &endptr, 10);
        if (endptr != val) {
            result = l;
            if (converted) {
                *converted = true;
            }
        }
    }
    return result;
}

celix_version_t* celix_utils_convertStringToVersion(const char* val) {
    celix_version_t* result = NULL;
    if (val != NULL) {
        //check if string has two dots ('.'), and only try to create string if it has two dots
        char* firstDot = strchr(val, '.');
        char* lastDot = strrchr(val, '.');
        if (firstDot != NULL && lastDot != NULL && firstDot != lastDot) {
            result = celix_version_createVersionFromString(val);
        }
    }
    return result;
}
