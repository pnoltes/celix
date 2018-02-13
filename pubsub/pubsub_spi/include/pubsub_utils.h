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
/*
 * pubsub_utils.h
 *
 *  \date       Sep 24, 2015
 *  \author    	<a href="mailto:dev@celix.apache.org">Apache Celix Project Team</a>
 *  \copyright	Apache License, Version 2.0
 */

#ifndef PUBSUB_UTILS_H_
#define PUBSUB_UTILS_H_

#include "bundle_context.h"
#include "array_list.h"

char* pubsub_getScopeFromFilter(const char* bundle_filter);
char* pubsub_getTopicFromFilter(const char* bundle_filter);
char* pubsub_getKeysBundleDir(bundle_context_pt ctx);
array_list_pt pubsub_getTopicsFromString(const char* string);


#endif /* PUBSUB_UTILS_H_ */