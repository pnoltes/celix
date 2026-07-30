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

#ifndef CELIX_ARRAY_LIST_PRIVATE_H
#define CELIX_ARRAY_LIST_PRIVATE_H

#include "celix_array_list.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Appends an owning variant entry to a variant array list.
 *
 * The array list takes ownership of value and its referenced value, also when insertion fails. The value must use the
 * same owning representation as array-list variant entries: strings and structured pointers are owned.
 */
celix_status_t celix_arrayList_assignVariant(celix_array_list_t* list, celix_array_list_variant_t* value);

#ifdef __cplusplus
}
#endif

#endif /* CELIX_ARRAY_LIST_PRIVATE_H */
