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

#pragma once

#include "celix/Filter.h"

namespace celix {
    class FilterCriteriaBuilder; //forward declaration
    class FilterContainerBuilder; //forward declaration

    class FilterCriteriaBuilder {
    public:
        explicit FilterCriteriaBuilder(std::string attribute);
        FilterCriteriaBuilder(celix::FilterCriteria parent, std::string attribute);

//        FilterContainerBuilder gte(const std::string& value);
//        FilterContainerBuilder gt(const std::string& value);
        FilterContainerBuilder is(const std::string& value);
//        FilterContainerBuilder isPresent(const std::string& value);
//        FilterContainerBuilder like(const std::string& value);
//        FilterContainerBuilder lte(const std::string& value);
//        FilterContainerBuilder lt(const std::string& value);
//        FilterContainerBuilder nott(const std::string& value);
    private:
        bool useContainer;
        celix::FilterCriteria container{};
        std::string attribute;

        //TODO make copy ctor/assign private
    };

    class FilterContainerBuilder {
    public:
        explicit FilterContainerBuilder(celix::FilterCriteria parent);

        //TODO rename andd, note and is not possible (keyword)
        FilterCriteriaBuilder andd(const std::string& attribute);
//        FilterCriteriaBuilder orr(const std::string& attribute);
//        FilterCriteriaBuilder orr(FilterContainerBuilder nested);

        celix::Filter build();
    protected:
        celix::FilterCriteria filterCriteria{};
    private:
        //TODO make copy ctor/assign private
    };

    class FilterBuilder {
    public:
        static FilterCriteriaBuilder where(const std::string &attribute);
    };
}