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

#pragma once

#include <map>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "celix/Exceptions.h"
#include "celix/Version.h"
#include "celix_properties.h"
#include "celix_utils.h"

namespace celix {

class Properties;

/**
 * Owning, recursive C++14 representation of a JSON-compatible property value.
 */
class PropertyValue {
  public:
    enum class Type { Null, String, Long, Double, Bool, Version, Properties, Array };

    PropertyValue() = default;
    PropertyValue(const PropertyValue& rhs);
    PropertyValue(PropertyValue&&) noexcept = default;
    PropertyValue& operator=(const PropertyValue& rhs);
    PropertyValue& operator=(PropertyValue&&) noexcept = default;

    Type getType() const noexcept { return type; }
    bool isNull() const noexcept { return type == Type::Null; }
    std::string getString(const std::string& fallback = {}) const {
        return type == Type::String ? stringValue : fallback;
    }
    long getLong(long fallback = 0L) const noexcept { return type == Type::Long ? longValue : fallback; }
    double getDouble(double fallback = 0.0) const noexcept { return type == Type::Double ? doubleValue : fallback; }
    bool getBool(bool fallback = false) const noexcept { return type == Type::Bool ? boolValue : fallback; }
    celix::Version getVersion(const celix::Version& fallback = {}) const {
        return type == Type::Version ? versionValue : fallback;
    }
    celix::Properties getProperties() const;
    celix::Properties getProperties(const celix::Properties& fallback) const;
    std::vector<PropertyValue> getArray(const std::vector<PropertyValue>& fallback = {}) const {
        return type == Type::Array ? arrayValue : fallback;
    }

  private:
    friend class Properties;
    static PropertyValue fromVariant(const celix_array_list_variant_t* value);
    static std::vector<PropertyValue> fromArray(const celix_array_list_t* value);
    static PropertyValue fromArrayElement(const celix_array_list_t* value, int index);
    static PropertyValue fromProperties(const celix_properties_t* value);

    Type type{Type::Null};
    std::string stringValue{};
    long longValue{0};
    double doubleValue{0.0};
    bool boolValue{false};
    celix::Version versionValue{};
    std::shared_ptr<celix_properties_t> propertiesValue{};
    std::vector<PropertyValue> arrayValue{};
};

/**
 * @brief A iterator for celix::Properties.
 */
class ConstPropertiesIterator {
  public:
    explicit ConstPropertiesIterator(const celix_properties_t* props) {
        iter = celix_properties_begin(props);
        setFields();
    }

    explicit ConstPropertiesIterator(celix_properties_iterator_t _iter) {
        iter = _iter;
        setFields();
    }

    ConstPropertiesIterator& operator++() {
        next();
        return *this;
    }

    const ConstPropertiesIterator& operator*() const { return *this; }

    bool operator==(const celix::ConstPropertiesIterator& rhs) const {
        return celix_propertiesIterator_equals(&iter, &rhs.iter);
    }

    bool operator!=(const celix::ConstPropertiesIterator& rhs) const { return !operator==(rhs); }

    void next() {
        celix_propertiesIterator_next(&iter);
        setFields();
    }

    std::string first{};
    std::string second{};

  private:
    void setFields() {
        if (celix_propertiesIterator_isEnd(&iter)) {
            first = {};
            second = {};
        } else {
            first = iter.key;
            second = iter.entry.value;
        }
    }

    celix_properties_iterator_t iter{.key = nullptr, .entry = {}, ._data = {}};
};

/**
 * @brief A collection of strings key values mainly used as meta data for registered services.
 *
 * @note Provided `const char*` values must be null terminated strings.
 * @note Not thread safe.
 */
class Properties {
  private:
    template <typename T>
    using IsString = std::is_same<std::decay_t<T>, std::string>; // Util to check if T is a std::string.

    template <typename T>
    using IsVersion = std::is_same<std::decay_t<T>, ::celix::Version>; // Util to check if T is a celix::Version.

    template <typename T>
    using IsIntegral = std::integral_constant<bool,
                                              std::is_integral<std::decay_t<T>>::value &&
                                                  !std::is_same<std::decay_t<T>, bool>::value>; // Util to check if T is
                                                                                                // an integral type.

    template <typename T>
    using IsFloatingPoint = std::is_floating_point<std::decay_t<T>>; // Util to check if T is a floating point type.

    template <typename T>
    using IsBoolean = std::is_same<std::decay_t<T>, bool>; // Util to check if T is a boolean type.

    template <typename T>
    using IsCharPointer = std::is_same<std::decay_t<T>, const char*>; // Util to check if T is a const char* type.

    template <typename T>
    using IsNotStringVersionIntegralFloatingPointOrBoolean =
        std::integral_constant<bool,
                               !IsString<T>::value && !IsVersion<T>::value && !IsCharPointer<T>::value &&
                                   !IsIntegral<T>::value && !IsFloatingPoint<T>::value && !IsBoolean<T>::value>;

  public:
    using const_iterator = ConstPropertiesIterator; // note currently only a const iterator is supported.

    /**
     * @brief Enum representing the possible types of a property value.
     */
    enum class ValueType {
        Unset,      /**< Property value is not set. */
        String,     /**< Property value is a string. */
        Long,       /**< Property value is a long integer. */
        Double,     /**< Property value is a double. */
        Bool,       /**< Property value is a boolean. */
        Version,    /**< Property value is a Celix version. */
        Vector,     /**< Property value is a vector of long integers, doubles, booleans, celix::Version or
                          string. */
        Null,       /**< Property value is JSON null. */
        Properties, /**< Property value is a nested properties object. */
    };

    class ValueRef {
      public:
        ValueRef(std::shared_ptr<celix_properties_t> _props, std::string _key)
            : props{std::move(_props)}, stringKey{std::move(_key)}, charKey{nullptr} {}
        ValueRef(std::shared_ptr<celix_properties_t> _props, const char* _key)
            : props{std::move(_props)}, stringKey{}, charKey{_key} {}

        ValueRef(const ValueRef&) = default;
        ValueRef(ValueRef&&) = default;
        ValueRef& operator=(const ValueRef&) = default;
        ValueRef& operator=(ValueRef&&) = default;

        ValueRef& operator=(const std::string& value) {
            if (charKey == nullptr) {
                celix_properties_set(props.get(), stringKey.c_str(), value.c_str());
            } else {
                celix_properties_set(props.get(), charKey, value.c_str());
            }
            return *this;
        }

        const char* getValue() const {
            if (charKey == nullptr) {
                return celix_properties_get(props.get(), stringKey.c_str(), nullptr);
            } else {
                return celix_properties_get(props.get(), charKey, nullptr);
            }
        }

        operator std::string() const {
            auto* cstr = getValue();
            return cstr == nullptr ? std::string{} : std::string{cstr};
        }

      private:
        std::shared_ptr<celix_properties_t> props;
        std::string stringKey;
        const char* charKey;
    };

    Properties() : cProps{createCProps(celix_properties_create())} {}

    Properties(Properties&&) = default;
    Properties& operator=(Properties&&) = default;

    Properties& operator=(const Properties& rhs) {
        if (this != &rhs) {
            cProps = createCProps(celix_properties_copy(rhs.cProps.get()));
        }
        return *this;
    }

    Properties(const Properties& rhs) : cProps{createCProps(celix_properties_copy(rhs.cProps.get()))} {}

    Properties(std::initializer_list<std::pair<std::string, std::string>> list)
        : cProps{celix_properties_create(), [](celix_properties_t* p) { celix_properties_destroy(p); }} {
        for (auto& entry : list) {
            set(entry.first, entry.second);
        }
    }

    /**
     * @brief Wrap C properties and returns it as const in a shared_ptr,
     * but does not take ownership -> dtor will not destroy C properties.
     */
    static Properties wrap(const celix_properties_t* wrapProps) {
        auto* cp = const_cast<celix_properties_t*>(wrapProps);
        return Properties{cp, false};
    }

    /**
     * @brief Wrap C properties and returns it as const in a shared_ptr,
     * but does not take ownership -> dtor will not destroy C properties.
     */
    static Properties wrap(celix_properties_t* wrapProps) { return Properties{wrapProps, false}; }

    /**
     * @brief Wrap C properties and take ownership -> dtor will destroy C properties.
     */
    static Properties own(celix_properties_t* wrapProps) { return Properties{wrapProps, true}; }

    /**
     * @brief Copy C properties and take ownership -> dtor will destroy C properties.
     */
    static Properties copy(const celix_properties_t* copyProps) {
        auto* result = celix_properties_copy(copyProps);
        throwIfNull(result);
        return Properties{result, true};
    }

    /**
     * Get the C properties object.
     *
     * @warning Try not the depend on the C API from a C++ bundle. If features are missing these should be added to
     * the C++ API.
     */
    celix_properties_t* getCProperties() const { return cProps.get(); }

    /**
     * @brief Get the value for a property key
     */
    ValueRef operator[](std::string key) { return ValueRef{cProps, std::move(key)}; }

    /**
     * @brief Get the value for a property key
     */
    ValueRef operator[](std::string key) const { return ValueRef{cProps, std::move(key)}; }

    /**
     * @brief Compare two properties objects for equality.
     * @param rhs
     * @return true if the properties are equal, false otherwise.
     */
    bool operator==(const Properties& rhs) const { return celix_properties_equals(cProps.get(), rhs.cProps.get()); }

    /**
     * @brief begin iterator
     */
    const_iterator begin() const noexcept { return ConstPropertiesIterator{cProps.get()}; }

    /**
     * @brief end iterator
     */
    const_iterator end() const noexcept { return ConstPropertiesIterator{celix_properties_end(cProps.get())}; }

    /**
     * @brief constant begin iterator
     */
    const_iterator cbegin() const noexcept { return ConstPropertiesIterator{cProps.get()}; }

    /**
     * @brief constant end iterator
     */
    const_iterator cend() const noexcept { return ConstPropertiesIterator{celix_properties_end(cProps.get())}; }

    /**
     * @brief Get the string value or string representation of a property.
     *
     * @note identical to celix::Properties::getAsString
     *
     * @param[in] key The key of the property to get.
     * @param[in] defaultValue The value to return if the property is not set.
     * @return The value of the property, or the default value if the property is not set.
     */
    std::string get(const std::string& key, const std::string& defaultValue = {}) const {
        return getAsString(key, defaultValue);
    }

    /**
     * @brief Get the string value or string representation of a property.
     *
     * @note identical to celix::Properties::get
     *
     * @param[in] key The key of the property to get.
     * @param[in] defaultValue The value to return if the property is not set.
     * @return The value of the property, or the default value if the property is not set.
     */
    std::string getAsString(const std::string& key, const std::string& defaultValue = {}) const {
        const char* found = celix_properties_getAsString(cProps.get(), key.c_str(), nullptr);
        return found == nullptr ? std::string{defaultValue} : std::string{found};
    }

    /**
     * @brief Get the value of a property, if the property is set and the underlying type is a string.
     * @param[in] key The key of the property to get.
     * @param[in] defaultValue The value to return if the property is not set or the value is not a string.
     * @return The value of the property, or the default value if the property is not set or the value is not of the
     * requested type.
     */
    std::string getString(const std::string& key, const std::string& defaultValue = {}) const {
        const char* found = celix_properties_getString(cProps.get(), key.c_str());
        return found == nullptr ? std::string{defaultValue} : std::string{found};
    }

    /**
     * @brief Get the value of the property with key as a long.
     *
     * @param[in] key The key of the property to get.
     * @param[in] defaultValue The value to return if the property is not set or if the value cannot be converted
     *                         to a long.
     * @return The long value of the property if it exists and can be converted, or the default value otherwise.
     */
    long getAsLong(const std::string& key, long defaultValue) const {
        return celix_properties_getAsLong(cProps.get(), key.c_str(), defaultValue);
    }

    /**
     * @brief Get the value of a property, if the property is set and the underlying type is a long.
     * @param[in] key The key of the property to get.
     * @param[in] defaultValue The value to return if the property is not set or the value is not a long.
     * @return The value of the property, or the default value if the property is not set or the value is not of the
     * requested type.
     */
    long getLong(const std::string& key, long defaultValue) const {
        return celix_properties_getLong(cProps.get(), key.c_str(), defaultValue);
    }

    /**
     * @brief Get the value of the property with key as a double.
     *
     * @param[in] key The key of the property to get.
     * @param[in] defaultValue The value to return if the property is not set or if the value cannot be converted
     *                         to a double.
     * @return The double value of the property if it exists and can be converted, or the default value otherwise.
     */
    double getAsDouble(const std::string& key, double defaultValue) const {
        return celix_properties_getAsDouble(cProps.get(), key.c_str(), defaultValue);
    }

    /**
     * @brief Get the value of a property, if the property is set and the underlying type is a double.
     * @param[in] properties The property set to search.
     * @param[in] key The key of the property to get.
     * @param[in] defaultValue The value to return if the property is not set or the value is not a double.
     * @return The value of the property, or the default value if the property is not set or the value is not of the
     * requested type.
     */
    double getDouble(const std::string& key, double defaultValue) const {
        return celix_properties_getDouble(cProps.get(), key.c_str(), defaultValue);
    }

    /**
     * @brief Get the value of the property with key as a boolean.
     *
     * @param[in] key The key of the property to get.
     * @param[in] defaultValue The value to return if the property is not set or if the value cannot be converted
     *                         to a boolean.
     * @return The boolean value of the property if it exists and can be converted, or the default value otherwise.
     */
    bool getAsBool(const std::string& key, bool defaultValue) const {
        return celix_properties_getAsBool(cProps.get(), key.c_str(), defaultValue);
    }

    /**
     * @brief Get the value of a property, if the property is set and the underlying type is a boolean.
     * @param[in] key The key of the property to get.
     * @param[in] defaultValue The value to return if the property is not set or the value is not a boolean.
     * @return The value of the property, or the default value if the property is not set or the value is not of the
     * requested type.
     */
    bool getBool(const std::string& key, bool defaultValue) const {
        return celix_properties_getBool(cProps.get(), key.c_str(), defaultValue);
    }

    /**
     * @brief Get the value of the property with key as a Celix version.
     *
     * Note that this function does not automatically convert a string property value to a Celix version.
     *
     * @param[in] key The key of the property to get.
     * @param[in] defaultValue The value to return if the property is not set or if the value is not a Celix
     *                         version.
     * @return The value of the property if it is a Celix version, or the default value if the property is not set
     *         or the value is not a Celix version.
     */
    celix::Version getAsVersion(const std::string& key, celix::Version defaultValue = {}) {
        celix_autoptr(celix_version_t) cVersion;
        celix_properties_getAsVersion(cProps.get(), key.data(), nullptr, &cVersion);
        if (cVersion) {
            celix::Version version{celix_version_getMajor(cVersion),
                                   celix_version_getMinor(cVersion),
                                   celix_version_getMicro(cVersion),
                                   celix_version_getQualifier(cVersion)};
            return version;
        }
        return defaultValue;
    }

    /**
     * @brief Get the Celix version value of a property without copying.
     *
     * This function provides a non-owning, read-only access to a Celix version contained in the properties.
     * It returns a const pointer to the Celix version value associated with the specified key.
     * This function does not perform any conversion from a string property value to a Celix version.
     *
     * @param[in] key The key of the property to get.
     * @param[in] defaultValue The value to return if the property is not set or if the value is not a Celix
     * version.
     * @return A const pointer to the Celix version if it is present and valid, or the provided default value if the
     * property is not set or the value is not a valid Celix version. The returned pointer should not be modified or
     * freed.
     */
    celix::Version getVersion(const std::string& key, celix::Version defaultValue = {}) const {
        auto* v = celix_properties_getVersion(cProps.get(), key.c_str());
        if (v) {
            return celix::Version{celix_version_getMajor(v),
                                  celix_version_getMinor(v),
                                  celix_version_getMicro(v),
                                  celix_version_getQualifier(v)};
        }
        return defaultValue;
    }

    /**
     * @brief Get a property value as a vector of longs.
     *
     * This function retrieves the value of a property, interpreting it as a vector of longs. If the underlying type
     * of the property value is a long array, a new long vector with the longs of the found array  is returned. If
     * the underlying type is a string, the string is converted to a vector of longs if possible. If the property is
     * not set, its value is not a vector of longs or its value cannot be converted to a long vector, the default
     * value is returned as a copy.
     *
     * @param[in] key The key of the property to get.
     * @param[in] defaultValue The default value to return if the property is not set or its value is not a vector
     * of longs.
     * @return A new vector with long values or a copy of the default value if
     * the property is not set, its value is not a array of longs or its value cannot be converted to a vector of
     * longs.
     */
    std::vector<long> getAsLongVector(const std::string& key, const std::vector<long>& defaultValue = {}) const {
        celix_autoptr(celix_array_list_t) list;
        celix_status_t status = celix_properties_getAsLongArrayList(cProps.get(), key.c_str(), nullptr, &list);
        throwIfEnomem(status);
        return convertToVector<long>(list, defaultValue, celix_arrayList_getLong);
    }

    /**
     * @brief Get vector of longs if the underlying property type is a array of longs.
     *
     * @param[in] key The key of the property to get.
     * @param[in] defaultValue A new vector with long values or a copy of the default value if the property is not
     * set or its value is not a array of longs.
     */
    std::vector<long> getLongVector(const std::string& key, const std::vector<long>& defaultValue = {}) const {
        const auto* list = celix_properties_getLongArrayList(cProps.get(), key.c_str());
        return convertToVector<long>(list, defaultValue, celix_arrayList_getLong);
    }

    /**
     * @brief Get a property value as a vector of booleans.
     *
     * This function retrieves the value of a property, interpreting it as a vector of booleans. If the underlying
     * type of the property value is a booleans array, a new boolean vector with the booleans of the found array  is
     * returned. If the underlying type is a string, the string is converted to a vector of booleans if possible. If
     * the property is not set, its value is not a vector of booleans or its value cannot be converted to a boolean
     * vector, the default value is returned as a copy.
     *
     * @param[in] key The key of the property to get.
     * @param[in] defaultValue The default value to return if the property is not set or its value is not a vector
     * of booleans.
     * @return A new vector with boolean values or a copy of the default value if
     * the property is not set, its value is not a array of booleans or its value cannot be converted to a vector of
     * booleans.
     */
    std::vector<bool> getAsBoolVector(const std::string& key, const std::vector<bool>& defaultValue = {}) const {
        celix_autoptr(celix_array_list_t) list;
        celix_status_t status = celix_properties_getAsBoolArrayList(cProps.get(), key.c_str(), nullptr, &list);
        throwIfEnomem(status);
        return convertToVector<bool>(list, defaultValue, celix_arrayList_getBool);
    }

    /**
     * @brief Get vector of booleans if the underlying property type is a array of booleans.
     *
     * @param[in] key The key of the property to get.
     * @param[in] defaultValue A new vector with boolean values or a copy of the default value if the property is not
     * set or its value is not a array of booleans.
     */
    std::vector<bool> getBoolVector(const std::string& key, const std::vector<bool>& defaultValue = {}) const {
        const auto* list = celix_properties_getBoolArrayList(cProps.get(), key.c_str());
        return convertToVector<bool>(list, defaultValue, celix_arrayList_getBool);
    }

    /**
     * @brief Get a property value as a vector of doubles.
     *
     * This function retrieves the value of a property, interpreting it as a vector of doubles. If the underlying
     * type of the property value is a double array, a new double vector with the doubles of the found array  is
     * returned. If the underlying type is a string, the string is converted to a vector of doubles if possible. If
     * the property is not set, its value is not a vector of doubles or its value cannot be converted to a double
     * vector, the default value is returned as a copy.
     *
     * @param[in] key The key of the property to get.
     * @param[in] defaultValue The default value to return if the property is not set or its value is not a vector
     * of doubles.
     * @return A new vector with double values or a copy of the default value if
     * the property is not set, its value is not a array of doubles or its value cannot be converted to a vector of
     * doubles.
     */
    std::vector<double> getAsDoubleVector(const std::string& key, const std::vector<double>& defaultValue = {}) const {
        celix_autoptr(celix_array_list_t) list;
        celix_status_t status = celix_properties_getAsDoubleArrayList(cProps.get(), key.c_str(), nullptr, &list);
        throwIfEnomem(status);
        return convertToVector<double>(list, defaultValue, celix_arrayList_getDouble);
    }

    /**
     * @brief Get vector of doubles if the underlying property type is a array of doubles.
     *
     * @param[in] key The key of the property to get.
     * @param[in] defaultValue A new vector with double values or a copy of the default value if the property is not
     * set or its value is not a array of doubles.
     */
    std::vector<double> getDoubleVector(const std::string& key, const std::vector<double>& defaultValue = {}) const {
        const auto* list = celix_properties_getDoubleArrayList(cProps.get(), key.c_str());
        return convertToVector<double>(list, defaultValue, celix_arrayList_getDouble);
    }

    /**
     * @brief Get a property value as a vector of celix::Version.
     *
     * This function retrieves the value of a property, interpreting it as a vector of celix::Version. If the
     * underlying type of the property value is a celix_version_t* array, a new celix::Version vector created using
     * the celix_version_t of the found array is returned. If the underlying type is a string, the string is
     * converted to a vector of celix::Version if possible. If the property is not set, its value is not a vector of
     * celix_version_t* or its value cannot be converted to a celix::Version vector, the default value is returned
     * as a copy.
     *
     * @param[in] key The key of the property to get.
     * @param[in] defaultValue The default value to return if the property is not set or its value is not a vector
     * of celix::Version.
     * @return A new vector with celix::Version values or a copy of the default value if
     * the property is not set, its value is not a array of celix_version_t* or its value cannot be converted to a
     * vector of celix::Version.
     */
    std::vector<celix::Version> getAsVersionVector(const std::string& key,
                                                   const std::vector<celix::Version>& defaultValue = {}) const {
        celix_autoptr(celix_array_list_t) list;
        celix_status_t status = celix_properties_getAsVersionArrayList(cProps.get(), key.c_str(), nullptr, &list);
        throwIfEnomem(status);
        if (list) {
            std::vector<celix::Version> result{};
            for (int i = 0; i < celix_arrayList_size(list); ++i) {
                const auto* v = celix_arrayList_getVersion(list, i);
                result.emplace_back(celix_version_getMajor(v),
                                    celix_version_getMinor(v),
                                    celix_version_getMicro(v),
                                    celix_version_getQualifier(v));
            }
            return result;
        }
        return defaultValue;
    }

    /**
     * @brief Get vector of celix::Version if the underlying property type is a array of celix_version_t*.
     *
     * @param[in] key The key of the property to get.
     * @param[in] defaultValue A new vector with celix::Version values or a copy of the default value if the
     * property is not set or its value is not a array of celix_version_t*.
     */
    std::vector<celix::Version> getVersionVector(const std::string& key,
                                                 const std::vector<celix::Version>& defaultValue = {}) const {
        const auto* list = celix_properties_getVersionArrayList(cProps.get(), key.c_str());
        if (list) {
            std::vector<celix::Version> result{};
            for (int i = 0; i < celix_arrayList_size(list); ++i) {
                const auto* v = celix_arrayList_getVersion(list, i);
                result.emplace_back(celix_version_getMajor(v),
                                    celix_version_getMinor(v),
                                    celix_version_getMicro(v),
                                    celix_version_getQualifier(v));
            }
            return result;
        }
        return defaultValue;
    }

    /**
     * @brief Get a property value as a vector of strings.
     *
     * This function retrieves the value of a property, interpreting it as a vector of strings. If the underlying
     * type of the property value is a string array, a new string vector with the strings of the found array  is
     * returned. If the underlying type is a string, the string is converted to a vector of strings if possible. If
     * the property is not set, its value is not a vector of strings or its value cannot be converted to a string
     * vector, the default value is returned as a copy.
     *
     * @param[in] key The key of the property to get.
     * @param[in] defaultValue The default value to return if the property is not set or its value is not a vector
     * of strings.
     * @return A new vector with string values or a copy of the default value if
     * the property is not set, its value is not a array of strings or its value cannot be converted to a vector of
     * strings.
     */
    std::vector<std::string> getAsStringVector(const std::string& key,
                                               const std::vector<std::string>& defaultValue = {}) const {
        celix_autoptr(celix_array_list_t) list;
        celix_status_t status = celix_properties_getAsStringArrayList(cProps.get(), key.c_str(), nullptr, &list);
        throwIfEnomem(status);
        if (list) {
            std::vector<std::string> result{};
            for (int i = 0; i < celix_arrayList_size(list); ++i) {
                auto* s = celix_arrayList_getString(list, i);
                result.emplace_back(s);
            }
            return result;
        }
        return defaultValue;
    }

    /**
     * @brief Get vector of strings if the underlying property type is a array of strings.
     *
     * @param[in] key The key of the property to get.
     * @param[in] defaultValue A new vector with string values or a copy of the default value if the property is not
     * set or its value is not a array of strings.
     */
    std::vector<std::string> getStringVector(const std::string& key,
                                             const std::vector<std::string>& defaultValue = {}) const {
        const auto* list = celix_properties_getStringArrayList(cProps.get(), key.c_str());
        if (list) {
            std::vector<std::string> result{};
            for (int i = 0; i < celix_arrayList_size(list); ++i) {
                auto* s = celix_arrayList_getString(list, i);
                result.emplace_back(s);
            }
            return result;
        }
        return defaultValue;
    }

    /**
     * @brief Set the value of a property.
     *
     * The property value will be set as a string.
     *
     * @param[in] key The key of the property to set.
     * @param[in] value The value to set the property to.
     * @throws std::bad_alloc If a ENOMEM error occurs while setting the property.
     */
    template <typename T>
    typename std::enable_if<IsString<T>::value>::type set(const std::string& key, T&& value) {
        auto status = celix_properties_set(cProps.get(), key.data(), value.c_str());
        throwIfEnomem(status);
    }

    /**
     * @brief Set string property value for a given key.
     *
     * The set property type will be ValueType::String.
     *
     * @param[in] key The key of the property to set.
     * @param[in] value The value to set the property to.
     * @throws std::bad_alloc If a ENOMEM error occurs while setting the property.
     */
    template <typename T>
    typename std::enable_if<IsCharPointer<T>::value>::type set(const std::string& key, T&& value) {
        auto status = celix_properties_set(cProps.get(), key.data(), value);
        throwIfEnomem(status);
    }

    /**
     * @brief Set a property with a to_string value of type T.
     *
     * The set property type will be ValueType::String.
     *
     * This function will use the std::to_string function to convert the value of type T to a string,
     * which will be used as the value for the property.
     *
     * @tparam T The type of the value to set.
     * @param[in] key The key of the property to set.
     * @param[in] value The value to set for the property.
     * @throws std::bad_alloc If a ENOMEM error occurs while setting the property.
     */
    template <typename T>
    typename std::enable_if<::celix::Properties::IsNotStringVersionIntegralFloatingPointOrBoolean<T>::value>::type
    set(const std::string& key, T&& value) {
        using namespace std;
        auto status = celix_properties_set(cProps.get(), key.c_str(), to_string(value).c_str());
        throwIfEnomem(status);
    }

    /**
     * @brief Sets a celix::Version property value for a given key.
     *
     * The set property type will be ValueType::Version.
     *
     * @param[in] key The key of the property to set.
     * @param[in] value The value to set for the property.
     * @throws std::bad_alloc If a ENOMEM error occurs while setting the property.
     */
    template <typename T>
    typename std::enable_if<::celix::Properties::IsVersion<T>::value>::type set(const std::string& key, T&& value) {
        auto status = celix_properties_setVersion(cProps.get(), key.data(), value.getCVersion());
        throwIfEnomem(status);
    }

    /**
     * @brief Sets a bool property value for a given key.
     *
     * The set property type will be ValueType::Bool.
     *
     * @param[in] key The key of the property to set.
     * @param[in] value The value to set for the property.
     * @throws std::bad_alloc If a ENOMEM error occurs while setting the property.
     */
    template <typename T>
    typename std::enable_if<::celix::Properties::IsBoolean<T>::value>::type set(const std::string& key, T&& value) {
        auto status = celix_properties_setBool(cProps.get(), key.data(), value);
        throwIfEnomem(status);
    }

    /**
     * @brief Sets a long property value for a given key.
     *
     * The set property type will be ValueType::Long.
     *
     * @param[in] key The key of the property to set.
     * @param[in] value The value to set for the property.
     * @throws std::bad_alloc If a ENOMEM error occurs while setting the property.
     */
    template <typename T>
    typename std::enable_if<::celix::Properties::IsIntegral<T>::value>::type set(const std::string& key, T&& value) {
        auto status = celix_properties_setLong(cProps.get(), key.data(), value);
        throwIfEnomem(status);
    }

    /**
     * @brief Sets a double property value for a given key.
     *
     * The set property type will be ValueType::Double.
     *
     * @param[in] key The key of the property to set.
     * @param[in] value The value to set for the property.
     * @throws std::bad_alloc If a ENOMEM error occurs while setting the property.
     */
    template <typename T>
    typename std::enable_if<::celix::Properties::IsFloatingPoint<T>::value>::type set(const std::string& key,
                                                                                      T&& value) {
        auto status = celix_properties_setDouble(cProps.get(), key.data(), value);
        throwIfEnomem(status);
    }

    /**
     * @brief Sets a celix_version_t* property value for a given key.
     *
     * The set property type will be ValueType::Version.
     *
     * @param[in] key The key of the property to set.
     * @param[in] value The value to set for the property.
     * @throws std::bad_alloc If a ENOMEM error occurs while setting the property.
     */
    void set(const std::string& key, const celix_version_t* value) {
        auto status = celix_properties_setVersion(cProps.get(), key.data(), value);
        throwIfEnomem(status);
    }

    /** Set an explicit JSON null property. */
    void setNull(const std::string& key) {
        auto status = celix_properties_setNull(cProps.get(), key.c_str());
        throwIfEnomem(status);
    }

    /** Set an owning copy of a nested properties value. */
    void setProperties(const std::string& key, const Properties& value) {
        auto status = celix_properties_setProperties(cProps.get(), key.c_str(), value.cProps.get());
        throwIfEnomem(status);
        if (status != CELIX_SUCCESS)
            celix::impl::throwException(status, "Cannot set nested celix::Properties");
    }

    /**
     * @brief Set a long array value for a property.
     *
     * @note The serVector method is only available for long, double, boolean, string and celix::Version types.
     *
     * The set property type will be ValueType::LongArray.
     *
     * @param[in] key The key of the property to set.
     * @param[in] values An vector of long values to set for the property.
     */
    template <typename T>
    typename std::enable_if<IsIntegral<T>::value, void>::type setVector(const std::string& key,
                                                                        const std::vector<T>& values) {
        setVectorInternal(key, values, celix_arrayList_createLongArray(), celix_arrayList_addLong);
    }

    /**
     * @brief Set a double array value for a property.
     *
     * @note The serVector method is only available for long, double, boolean, string and celix::Version types.
     *
     * The set property type will be ValueType::DoubleArray.
     *
     * @param[in] key The key of the property to set.
     * @param[in] values An vector of double values to set for the property.
     */
    template <typename T>
    typename std::enable_if<IsFloatingPoint<T>::value, void>::type setVector(const std::string& key,
                                                                             const std::vector<T>& values) {
        setVectorInternal(key, values, celix_arrayList_createDoubleArray(), celix_arrayList_addDouble);
    }

    /**
     * @brief Set a boolean array value for a property.
     *
     * @note The serVector method is only available for long, double, boolean, string and celix::Version types.
     *
     * The set property type will be ValueType::BooleanArray.
     *
     * @param[in] key The key of the property to set.
     * @param[in] values An vector of boolean values to set for the property.
     */
    template <typename T>
    typename std::enable_if<IsBoolean<T>::value, void>::type setVector(const std::string& key,
                                                                       const std::vector<T>& values) {
        setVectorInternal(key, values, celix_arrayList_createBoolArray(), celix_arrayList_addBool);
    }

    /**
     * @brief Set a string array value for a property.
     *
     * @note The serVector method is only available for long, double, boolean, string and celix::Version types.
     *
     * The set property type will be ValueType::StringArray.
     *
     * @param[in] key The key of the property to set.
     * @param[in] values An vector of string values to set for the property.
     */
    template <typename T>
    typename std::enable_if<IsString<T>::value, void>::type setVector(const std::string& key,
                                                                      const std::vector<T>& values) {
        celix_autoptr(celix_array_list_t) list = celix_arrayList_createStringArray();
        throwIfNull(list);
        for (const auto& v : values) {
            celix_status_t status = celix_arrayList_addString(list, v.c_str());
            throwIfEnomem(status);
        }
        auto status = celix_properties_assignArrayList(cProps.get(), key.data(), celix_steal_ptr(list));
        throwIfEnomem(status);
    }

    /**
     * @brief Set a celix::Version array value for a property.
     *
     * @note The serVector method is only available for long, double, boolean, string and celix::Version types.
     *
     * The set property type will be ValueType::VersionArray.
     *
     * @param[in] key The key of the property to set.
     * @param[in] values An vector of celix::Version values to set for the property.
     */
    template <typename T>
    typename std::enable_if<IsVersion<T>::value, void>::type setVector(const std::string& key,
                                                                       const std::vector<T>& values) {
        celix_autoptr(celix_array_list_t) list = celix_arrayList_createVersionArray();
        throwIfNull(list);
        for (const auto& v : values) {
            auto* cVer = celix_version_create(v.getMajor(), v.getMinor(), v.getMicro(), v.getQualifier().c_str());
            throwIfNull(cVer);
            celix_status_t status = celix_arrayList_assignVersion(list, cVer);
            throwIfEnomem(status);
        }
        auto status = celix_properties_assignArrayList(cProps.get(), key.data(), celix_steal_ptr(list));
        throwIfEnomem(status);
    }

    /**
     * @brief Returns the number of properties in the Properties object.
     */
    std::size_t size() const { return celix_properties_size(cProps.get()); }

    /**
     * @brief Get the type of the property with key.
     *
     * @param[in] key The key of the property to get the type for.
     * @return The type of the property with the given key, or ValueType::Unset if the property
     *         does not exist.
     */
    ValueType getType(const std::string& key) { return getAndConvertType(cProps, key.data()); }

    Properties getProperties(const std::string& key, const Properties& defaultValue = {}) const {
        const auto* value = celix_properties_getProperties(cProps.get(), key.c_str());
        return value ? Properties::copy(value) : defaultValue;
    }

    static bool checkPath(const std::string& path) { return celix_properties_checkPath(path.c_str()); }

    std::string getStringByPath(const std::string& path, const std::string& defaultValue = {}) const {
        return celix_properties_getStringByPath(cProps.get(), path.c_str(), defaultValue.c_str());
    }

    long getLongByPath(const std::string& path, long defaultValue = 0L) const {
        return celix_properties_getLongByPath(cProps.get(), path.c_str(), defaultValue);
    }

    double getDoubleByPath(const std::string& path, double defaultValue = 0.0) const {
        return celix_properties_getDoubleByPath(cProps.get(), path.c_str(), defaultValue);
    }

    bool getBoolByPath(const std::string& path, bool defaultValue = false) const {
        return celix_properties_getBoolByPath(cProps.get(), path.c_str(), defaultValue);
    }

    celix::Version getVersionByPath(const std::string& path, const celix::Version& defaultValue = {}) const {
        const auto* value = celix_properties_getVersionByPath(cProps.get(), path.c_str(), nullptr);
        return value ? celix::Version{celix_version_getMajor(value),
                                      celix_version_getMinor(value),
                                      celix_version_getMicro(value),
                                      celix_version_getQualifier(value)}
                     : defaultValue;
    }

    Properties getPropertiesByPath(const std::string& path, const Properties& defaultValue = {}) const {
        const auto* value = celix_properties_getPropertiesByPath(cProps.get(), path.c_str(), nullptr);
        return value ? Properties::copy(value) : defaultValue;
    }

    std::vector<PropertyValue> getArrayByPath(const std::string& path,
                                              const std::vector<PropertyValue>& defaultValue = {}) const {
        const auto* value = celix_properties_getArrayListByPath(cProps.get(), path.c_str(), nullptr);
        return value ? PropertyValue::fromArray(value) : defaultValue;
    }

    std::vector<std::string> getStringVectorByPath(const std::string& path,
                                                   const std::vector<std::string>& defaultValue = {}) const {
        const auto* value = celix_properties_getArrayListByPath(cProps.get(), path.c_str(), nullptr);
        if (!value || celix_arrayList_getElementType(value) != CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING)
            return defaultValue;
        std::vector<std::string> result;
        result.reserve(celix_arrayList_size(value));
        for (int i = 0; i < celix_arrayList_size(value); ++i)
            result.emplace_back(celix_arrayList_getString(value, i));
        return result;
    }

    std::vector<long> getLongVectorByPath(const std::string& path, const std::vector<long>& defaultValue = {}) const {
        const auto* value = celix_properties_getArrayListByPath(cProps.get(), path.c_str(), nullptr);
        return value && celix_arrayList_getElementType(value) == CELIX_ARRAY_LIST_ELEMENT_TYPE_LONG
                   ? convertToVector<long>(value, {}, celix_arrayList_getLong)
                   : defaultValue;
    }

    std::vector<double> getDoubleVectorByPath(const std::string& path,
                                              const std::vector<double>& defaultValue = {}) const {
        const auto* value = celix_properties_getArrayListByPath(cProps.get(), path.c_str(), nullptr);
        return value && celix_arrayList_getElementType(value) == CELIX_ARRAY_LIST_ELEMENT_TYPE_DOUBLE
                   ? convertToVector<double>(value, {}, celix_arrayList_getDouble)
                   : defaultValue;
    }

    std::vector<bool> getBoolVectorByPath(const std::string& path, const std::vector<bool>& defaultValue = {}) const {
        const auto* value = celix_properties_getArrayListByPath(cProps.get(), path.c_str(), nullptr);
        return value && celix_arrayList_getElementType(value) == CELIX_ARRAY_LIST_ELEMENT_TYPE_BOOL
                   ? convertToVector<bool>(value, {}, celix_arrayList_getBool)
                   : defaultValue;
    }

    std::vector<celix::Version> getVersionVectorByPath(const std::string& path,
                                                       const std::vector<celix::Version>& defaultValue = {}) const {
        const auto* value = celix_properties_getArrayListByPath(cProps.get(), path.c_str(), nullptr);
        if (!value || celix_arrayList_getElementType(value) != CELIX_ARRAY_LIST_ELEMENT_TYPE_VERSION)
            return defaultValue;
        std::vector<celix::Version> result;
        result.reserve(celix_arrayList_size(value));
        for (int i = 0; i < celix_arrayList_size(value); ++i) {
            const auto* version = celix_arrayList_getVersion(value, i);
            result.emplace_back(celix_version_getMajor(version),
                                celix_version_getMinor(version),
                                celix_version_getMicro(version),
                                celix_version_getQualifier(version));
        }
        return result;
    }

    std::vector<Properties> getPropertiesVectorByPath(const std::string& path,
                                                      const std::vector<Properties>& defaultValue = {}) const {
        const auto* value = celix_properties_getArrayListByPath(cProps.get(), path.c_str(), nullptr);
        if (!value || celix_arrayList_getElementType(value) != CELIX_ARRAY_LIST_ELEMENT_TYPE_PROPERTIES)
            return defaultValue;
        std::vector<Properties> result;
        result.reserve(celix_arrayList_size(value));
        for (int i = 0; i < celix_arrayList_size(value); ++i)
            result.emplace_back(Properties::copy(celix_arrayList_getProperties(value, i)));
        return result;
    }

    bool hasPath(const std::string& path) const { return celix_properties_hasPath(cProps.get(), path.c_str()); }
    bool hasStringPath(const std::string& path) const {
        return celix_properties_hasStringPath(cProps.get(), path.c_str());
    }
    bool hasLongPath(const std::string& path) const { return celix_properties_hasLongPath(cProps.get(), path.c_str()); }
    bool hasDoublePath(const std::string& path) const {
        return celix_properties_hasDoublePath(cProps.get(), path.c_str());
    }
    bool hasBoolPath(const std::string& path) const { return celix_properties_hasBoolPath(cProps.get(), path.c_str()); }
    bool hasVersionPath(const std::string& path) const {
        return celix_properties_hasVersionPath(cProps.get(), path.c_str());
    }
    bool hasPropertiesPath(const std::string& path) const {
        return celix_properties_hasPropertiesPath(cProps.get(), path.c_str());
    }
    bool hasArrayPath(const std::string& path) const {
        return celix_properties_hasArrayListPath(cProps.get(), path.c_str());
    }
    bool hasNullPath(const std::string& path) const { return celix_properties_hasNullPath(cProps.get(), path.c_str()); }

    std::vector<std::string> getAllStringsByPath(const std::string& path) const {
        celix_autoptr(celix_array_list_t) list = celix_properties_getAllStringsByPath(cProps.get(), path.c_str());
        throwIfNull(list);
        std::vector<std::string> result{};
        for (int i = 0; i < celix_arrayList_size(list); ++i)
            result.emplace_back(celix_arrayList_getString(list, i));
        return result;
    }

    std::vector<long> getAllLongsByPath(const std::string& path) const {
        celix_autoptr(celix_array_list_t) list = celix_properties_getAllLongsByPath(cProps.get(), path.c_str());
        throwIfNull(list);
        std::vector<long> result{};
        for (int i = 0; i < celix_arrayList_size(list); ++i)
            result.emplace_back(celix_arrayList_getLong(list, i));
        return result;
    }

    std::vector<double> getAllDoublesByPath(const std::string& path) const {
        celix_autoptr(celix_array_list_t) list = celix_properties_getAllDoublesByPath(cProps.get(), path.c_str());
        throwIfNull(list);
        return convertToVector<double>(list, {}, celix_arrayList_getDouble);
    }

    std::vector<bool> getAllBoolsByPath(const std::string& path) const {
        celix_autoptr(celix_array_list_t) list = celix_properties_getAllBoolsByPath(cProps.get(), path.c_str());
        throwIfNull(list);
        return convertToVector<bool>(list, {}, celix_arrayList_getBool);
    }

    std::vector<celix::Version> getAllVersionsByPath(const std::string& path) const {
        celix_autoptr(celix_array_list_t) list = celix_properties_getAllVersionsByPath(cProps.get(), path.c_str());
        throwIfNull(list);
        std::vector<celix::Version> result;
        result.reserve(celix_arrayList_size(list));
        for (int i = 0; i < celix_arrayList_size(list); ++i) {
            const auto* version = celix_arrayList_getVersion(list, i);
            result.emplace_back(celix_version_getMajor(version),
                                celix_version_getMinor(version),
                                celix_version_getMicro(version),
                                celix_version_getQualifier(version));
        }
        return result;
    }

    std::vector<Properties> getAllPropertiesByPath(const std::string& path) const {
        celix_autoptr(celix_array_list_t) list = celix_properties_getAllPropertiesByPath(cProps.get(), path.c_str());
        throwIfNull(list);
        std::vector<Properties> result{};
        for (int i = 0; i < celix_arrayList_size(list); ++i) {
            result.emplace_back(Properties::copy(celix_arrayList_getProperties(list, i)));
        }
        return result;
    }

    std::vector<std::vector<PropertyValue>> getAllArraysByPath(const std::string& path) const {
        celix_autoptr(celix_array_list_t) list = celix_properties_getAllArrayListsByPath(cProps.get(), path.c_str());
        throwIfNull(list);
        std::vector<std::vector<PropertyValue>> result;
        result.reserve(celix_arrayList_size(list));
        for (int i = 0; i < celix_arrayList_size(list); ++i)
            result.emplace_back(PropertyValue::fromArray(celix_arrayList_getArrayList(list, i)));
        return result;
    }

    std::vector<PropertyValue> getAllValuesByPath(const std::string& path) const {
        celix_autoptr(celix_array_list_t) list = celix_properties_getAllValuesByPath(cProps.get(), path.c_str());
        throwIfNull(list);
        return PropertyValue::fromArray(list);
    }

    /**
     * @brief Convert the properties a (new) std::string, std::string map.
     */
    std::map<std::string, std::string> convertToMap() const {
        std::map<std::string, std::string> result{};
        for (const auto& pair : *this) {
            result[std::string{pair.first}] = pair.second;
        }
        return result;
    }

    /**
     * @brief Convert the properties a (new) std::string, std::string unordered map.
     */
    std::unordered_map<std::string, std::string> convertToUnorderedMap() const {
        std::unordered_map<std::string, std::string> result{};
        for (const auto& pair : *this) {
            result[std::string{pair.first}] = pair.second;
        }
        return result;
    }

    /**
     * @brief Enum class for encoding flags used in Celix properties JSON encoding.
     *
     * The flags are used to control the encoding process and to specify the output format.
     *
     * @enum EncodingFlags
     */
    enum class EncodingFlags : int {
        None = 0,                                /**< No special encoding flags. */
        Pretty = CELIX_PROPERTIES_ENCODE_PRETTY, /**< Encode in a pretty format, with indentation and line breaks. */
        FlatStyle = CELIX_PROPERTIES_ENCODE_FLAT_STYLE,                     /**< Deprecated no-op compatibility flag. */
        NestedStyle = CELIX_PROPERTIES_ENCODE_NESTED_STYLE,                 /**< Deprecated no-op compatibility flag. */
        ErrorOnCollisions = CELIX_PROPERTIES_ENCODE_ERROR_ON_COLLISIONS,    /**< Deprecated no-op compatibility flag. */
        ErrorOnEmptyArrays = CELIX_PROPERTIES_ENCODE_ERROR_ON_EMPTY_ARRAYS, /**< Deprecated no-op compatibility flag. */
        ErrorOnNanInf =
            CELIX_PROPERTIES_ENCODE_ERROR_ON_NAN_INF, /**< Deprecated no-op; non-finite values always fail. */
        Strict = CELIX_PROPERTIES_ENCODE_STRICT,      /**< Deprecated combination of compatibility flags. */
    };

    /**
     * @brief Save (encode) this properties object as a JSON representation to a file.
     *
     * For more information how a properties object is encoded to JSON, see the celix_properties_loadFromStream
     *
     * For a overview of the possible encode flags, see the EncodingFlags flags documentation.
     * The default encoding is compact and property member names are literal.
     *
     * @param[in] filename The file to write the JSON representation of the properties object to.
     * @param[in] encodingFlags The flags to use when encoding the input string.
     * @throws celix::IOException If an error occurs while writing to the file.
     * @throws celix::IllegalArgumentException If the provided properties cannot be encoded to JSON.
     * @throws std::bad_alloc If there was not enough memory to save the properties.
     */
    void save(const std::string& filename, EncodingFlags encodingFlags = EncodingFlags::None) const {
        auto status = celix_properties_save(cProps.get(), filename.c_str(), static_cast<int>(encodingFlags));
        if (status == ENOMEM) {
            throw std::bad_alloc();
        } else if (status != CELIX_SUCCESS) {
            celix::impl::throwException(status, std::string{"Cannot save celix::Properties to "} + filename);
        }
    }

    /**
     * @brief Save (encode) this properties object as a JSON representation to a string.
     *
     * For more information how a properties object is encoded to JSON, see the celix_properties_loadFromStream
     *
     * For a overview of the possible encode flags, see the EncodingFlags flags documentation.
     * The default encoding is compact and property member names are literal.
     *
     * @param[in] encodeFlags The flags to use when encoding the input string.
     * @throws celix::IllegalArgumentException If the provided properties cannot be encoded to JSON.
     * @throws std::bad_alloc If there was not enough memory to save the properties.
     */
    std::string saveToString(EncodingFlags encodeFlags = EncodingFlags::None) const {
        char* str = nullptr;
        auto status = celix_properties_saveToString(cProps.get(), static_cast<int>(encodeFlags), &str);
        if (status == ENOMEM) {
            throw std::bad_alloc();
        } else if (status != CELIX_SUCCESS) {
            celix::impl::throwException(status, "Cannot save celix::Properties to string");
        }
        std::string result{str};
        free(str);
        return result;
    }

    /**
     * @brief Enum class for decoding flags used in Celix properties JSON decoding.
     *
     * The flags are used to control the decoding process and to specify the output format.
     *
     * @enum DecodeFlags
     */
    enum class DecodeFlags : int {
        None = 0, /**< No special decoding flags. */
        ErrorOnDuplicates =
            CELIX_PROPERTIES_DECODE_ERROR_ON_DUPLICATES, /**< Deprecated no-op; duplicates always fail. */
        ErrorOnCollisions = CELIX_PROPERTIES_DECODE_ERROR_ON_COLLISIONS,    /**< Deprecated no-op compatibility flag. */
        ErrorOnNullValues = CELIX_PROPERTIES_DECODE_ERROR_ON_NULL_VALUES,   /**< Deprecated no-op compatibility flag. */
        ErrorOnEmptyArrays = CELIX_PROPERTIES_DECODE_ERROR_ON_EMPTY_ARRAYS, /**< Deprecated no-op compatibility flag. */
        ErrorOnEmptyKeys = CELIX_PROPERTIES_DECODE_ERROR_ON_EMPTY_KEYS,     /**< Deprecated no-op compatibility flag. */
        ErrorOnUnsupportedArrays = CELIX_PROPERTIES_DECODE_ERROR_ON_UNSUPPORTED_ARRAYS, /**< Deprecated no-op. */
        LegacyVersionStrings =
            CELIX_PROPERTIES_DECODE_LEGACY_VERSION_STRINGS, /**< Decode tagged strings as versions. */
        Strict = CELIX_PROPERTIES_DECODE_STRICT             /**< Deprecated combination of no-op compatibility flags. */
    };

    /**
     * @brief Load a Properties object from a file.
     *
     * @warning The name is temporary and will be renamed to celix::Properties::load in the future (when
     * the current celix::Properties::load is removed).
     *
     * The content of the filename file is expected to be in the format of a JSON object.
     * For what can and cannot be parsed, see celix_properties_loadFromStream documentation.
     *
     * For a overview of the possible decode flags, see the DecodingFlags flags documentation.
     *
     * @param[in] filename The file to load the properties from.
     * @param[in] decodeFlags The flags to use when decoding the input string.
     * @return A new Properties object containing the properties from the file.
     * @throws celix::IOException If the file cannot be opened or read.
     * @throws celix::IllegalArgumentException if the provided input cannot be decoded to a properties object.
     * @throws std::bad_alloc If there was not enough memory to load the properties.
     */
    static Properties load2(const std::string& filename, DecodeFlags decodeFlags = DecodeFlags::None) {
        celix_properties_t* props;
        auto status = celix_properties_load(filename.c_str(), static_cast<int>(decodeFlags), &props);
        if (status == ENOMEM) {
            throw std::bad_alloc();
        } else if (status != CELIX_SUCCESS) {
            celix::impl::throwException(status, "Cannot load celix::Properties from " + filename);
        }
        return celix::Properties::own(props);
    }

    /**
     * @brief Load a Properties object from a string.
     *
     *
     * The input string is expected to be in the format of a JSON object.
     * For what can and cannot be parsed, see celix_properties_loadFromStream documentation.
     *
     * For a overview of the possible decode flags, see the DecodingFlags flags documentation.
     *
     * @param[in] input The input string to parse.
     * @param[in] decodeFlags The flags to use when decoding the input string.
     * @return A new Properties object containing the properties from the file.
     * @throws celix::IllegalArgumentException if the provided input cannot be decoded to a properties object.
     * @throws std::bad_alloc If there was not enough memory to load the properties.
     */
    static Properties loadFromString(const std::string& input, DecodeFlags decodeFlags = DecodeFlags::None) {
        celix_properties_t* props;
        auto status = celix_properties_loadFromString(input.c_str(), static_cast<int>(decodeFlags), &props);
        if (status == ENOMEM) {
            throw std::bad_alloc();
        } else if (status != CELIX_SUCCESS) {
            celix::impl::throwException(status, "Cannot load celix::Properties from string");
        }
        return celix::Properties::own(props);
    }

  private:
    Properties(celix_properties_t* props, bool takeOwnership)
        : cProps{props, [takeOwnership](celix_properties_t* p) {
                     if (takeOwnership) {
                         celix_properties_destroy(p);
                     }
                 }} {}

    static std::shared_ptr<celix_properties_t> createCProps(celix_properties_t* p) {
        throwIfNull(p);
        return std::shared_ptr<celix_properties_t>{p, [](celix_properties_t* p) { celix_properties_destroy(p); }};
    }

    static void throwIfEnomem(int status) {
        if (status == CELIX_ENOMEM) {
            throw std::bad_alloc();
        }
    }

    template <typename T>
    static void throwIfNull(T* ptr) {
        if (ptr == nullptr) {
            throw std::bad_alloc();
        }
    }

    static celix::Properties::ValueType getAndConvertType(const std::shared_ptr<celix_properties_t>& cProperties,
                                                          const char* key) {
        auto cType = celix_properties_getType(cProperties.get(), key);
        switch (cType) {
        case CELIX_PROPERTIES_VALUE_TYPE_STRING:
            return ValueType::String;
        case CELIX_PROPERTIES_VALUE_TYPE_LONG:
            return ValueType::Long;
        case CELIX_PROPERTIES_VALUE_TYPE_DOUBLE:
            return ValueType::Double;
        case CELIX_PROPERTIES_VALUE_TYPE_BOOL:
            return ValueType::Bool;
        case CELIX_PROPERTIES_VALUE_TYPE_VERSION:
            return ValueType::Version;
        case CELIX_PROPERTIES_VALUE_TYPE_ARRAY_LIST:
            return ValueType::Vector;
        case CELIX_PROPERTIES_VALUE_TYPE_NULL:
            return ValueType::Null;
        case CELIX_PROPERTIES_VALUE_TYPE_PROPERTIES:
            return ValueType::Properties;
        default: /*unset*/
            return ValueType::Unset;
        }
    }

    template <typename T>
    std::vector<T> convertToVector(const celix_array_list_t* list,
                                   const std::vector<T>& defaultValue,
                                   T (*get)(const celix_array_list_t*, int index)) const {
        if (list) {
            std::vector<T> result{};
            result.reserve(celix_arrayList_size(list));
            for (int i = 0; i < celix_arrayList_size(list); ++i) {
                T value = get(list, i);
                result.emplace_back(value);
            }
            return result;
        }
        return defaultValue;
    }

    template <typename T>
    void setVectorInternal(const std::string& key,
                           const std::vector<T>& values,
                           celix_array_list_t* listIn,
                           celix_status_t (*add)(celix_array_list_t*, T value)) {
        celix_autoptr(celix_array_list_t) list = listIn;
        throwIfNull(list);
        for (const auto& v : values) {
            celix_status_t status = add(list, v);
            throwIfEnomem(status);
        }
        auto status = celix_properties_assignArrayList(cProps.get(), key.data(), celix_steal_ptr(list));
        throwIfEnomem(status);
    }

    std::shared_ptr<celix_properties_t> cProps;
};

inline PropertyValue PropertyValue::fromProperties(const celix_properties_t* value) {
    PropertyValue result;
    result.type = Type::Properties;
    auto* copy = celix_properties_copy(value);
    if (!copy)
        throw std::bad_alloc{};
    result.propertiesValue =
        std::shared_ptr<celix_properties_t>{copy, [](celix_properties_t* props) { celix_properties_destroy(props); }};
    return result;
}

inline PropertyValue PropertyValue::fromVariant(const celix_array_list_variant_t* value) {
    PropertyValue result;
    switch (value->type) {
    case CELIX_ARRAY_LIST_VARIANT_TYPE_NULL:
        result.type = Type::Null;
        break;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_STRING:
        result.type = Type::String;
        result.stringValue = value->value.stringValue;
        break;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_LONG:
        result.type = Type::Long;
        result.longValue = value->value.longValue;
        break;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_DOUBLE:
        result.type = Type::Double;
        result.doubleValue = value->value.doubleValue;
        break;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_BOOL:
        result.type = Type::Bool;
        result.boolValue = value->value.boolValue;
        break;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_VERSION:
        result.type = Type::Version;
        result.versionValue = celix::Version{celix_version_getMajor(value->value.versionValue),
                                             celix_version_getMinor(value->value.versionValue),
                                             celix_version_getMicro(value->value.versionValue),
                                             celix_version_getQualifier(value->value.versionValue)};
        break;
    case CELIX_ARRAY_LIST_VARIANT_TYPE_PROPERTIES:
        return fromProperties(value->value.propertiesValue);
    case CELIX_ARRAY_LIST_VARIANT_TYPE_ARRAY_LIST:
        result.type = Type::Array;
        result.arrayValue = fromArray(value->value.arrayListValue);
        break;
    }
    return result;
}

inline PropertyValue PropertyValue::fromArrayElement(const celix_array_list_t* value, int index) {
    PropertyValue result;
    switch (celix_arrayList_getElementType(value)) {
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_STRING:
        result.type = Type::String;
        result.stringValue = celix_arrayList_getString(value, index);
        break;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_LONG:
        result.type = Type::Long;
        result.longValue = celix_arrayList_getLong(value, index);
        break;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_DOUBLE:
        result.type = Type::Double;
        result.doubleValue = celix_arrayList_getDouble(value, index);
        break;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_BOOL:
        result.type = Type::Bool;
        result.boolValue = celix_arrayList_getBool(value, index);
        break;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_VERSION: {
        const auto* version = celix_arrayList_getVersion(value, index);
        result.type = Type::Version;
        result.versionValue = celix::Version{celix_version_getMajor(version),
                                             celix_version_getMinor(version),
                                             celix_version_getMicro(version),
                                             celix_version_getQualifier(version)};
        break;
    }
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_PROPERTIES:
        return fromProperties(celix_arrayList_getProperties(value, index));
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_ARRAY_LIST:
        result.type = Type::Array;
        result.arrayValue = fromArray(celix_arrayList_getArrayList(value, index));
        break;
    case CELIX_ARRAY_LIST_ELEMENT_TYPE_VARIANT:
        return fromVariant(celix_arrayList_getVariant(value, index));
    default:
        break;
    }
    return result;
}

inline std::vector<PropertyValue> PropertyValue::fromArray(const celix_array_list_t* value) {
    std::vector<PropertyValue> result;
    result.reserve(celix_arrayList_size(value));
    for (int i = 0; i < celix_arrayList_size(value); ++i)
        result.emplace_back(fromArrayElement(value, i));
    return result;
}

inline PropertyValue::PropertyValue(const PropertyValue& rhs)
    : type{rhs.type}, stringValue{rhs.stringValue}, longValue{rhs.longValue}, doubleValue{rhs.doubleValue},
      boolValue{rhs.boolValue}, versionValue{rhs.versionValue}, arrayValue{rhs.arrayValue} {
    if (rhs.propertiesValue) {
        auto* copy = celix_properties_copy(rhs.propertiesValue.get());
        if (!copy)
            throw std::bad_alloc{};
        propertiesValue = std::shared_ptr<celix_properties_t>{
            copy, [](celix_properties_t* props) { celix_properties_destroy(props); }};
    }
}

inline PropertyValue& PropertyValue::operator=(const PropertyValue& rhs) {
    if (this != &rhs) {
        PropertyValue copy{rhs};
        *this = std::move(copy);
    }
    return *this;
}

inline celix::Properties PropertyValue::getProperties() const {
    return type == Type::Properties ? celix::Properties::copy(propertiesValue.get()) : celix::Properties{};
}

inline celix::Properties PropertyValue::getProperties(const celix::Properties& fallback) const {
    return type == Type::Properties ? celix::Properties::copy(propertiesValue.get()) : fallback;
}
} // namespace celix

/**
 * @brief Stream operator to print the properties value reference to a stream.
 * @param[in] os The stream to print the properties to.
 * @param[in] ref The properties value reference to print.
 * @return The os stream.
 */
inline std::ostream& operator<<(std::ostream& os, const ::celix::Properties::ValueRef& ref) {
    os << std::string{ref.getValue()};
    return os;
}

/**
 * @brief Bitwise OR operator for EncodingFlags.
 * @param[in] a encoding flags
 * @param[in] b encoding flags
 * @return The bitwise OR of the two encoding flags.
 */
inline ::celix::Properties::EncodingFlags operator|(::celix::Properties::EncodingFlags a,
                                                    ::celix::Properties::EncodingFlags b) {
    return static_cast<::celix::Properties::EncodingFlags>(static_cast<int>(a) | static_cast<int>(b));
}

/**
 * @brief Bitwise OR operator for DecodeFlags.
 * @param[in] a decoding flags
 * @param[in] b decoding flags
 * @return The bitwise OR of the two decoding flags.
 */
inline ::celix::Properties::DecodeFlags operator|(::celix::Properties::DecodeFlags a,
                                                  ::celix::Properties::DecodeFlags b) {
    return static_cast<::celix::Properties::DecodeFlags>(static_cast<int>(a) | static_cast<int>(b));
}
