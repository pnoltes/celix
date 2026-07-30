---
title: JSON-compatible Apache Celix properties implementation plan
---

<!--
Licensed to the Apache Software Foundation (ASF) under one or more
contributor license agreements.  See the NOTICE file distributed with
this work for additional information regarding copyright ownership.
The ASF licenses this file to You under the Apache License, Version 2.0
(the "License"); you may not use this file except in compliance with
the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

# JSON-compatible Apache Celix properties implementation plan

## Purpose

This document is an implementation plan for making `celix_properties_t` capable of representing a JSON object without
flattening or dropping valid JSON values. The work must start in `celix_array_list_t`, because properties cannot preserve
JSON arrays until array lists support objects, nested arrays, empty arrays, `null`, and heterogeneous values.

Implement the work as the independently testable stages below. Do not start the JSONPath stage until arbitrary supported
JSON object trees round-trip correctly.

## Target behavior

After this work:

- A `celix_properties_t` represents a JSON object. Its keys are literal object member names.
- A nested `celix_properties_t` value encodes as a nested JSON object.
- A `celix_array_list_t` can efficiently hold the existing homogeneous scalar types, homogeneous properties objects,
  homogeneous nested array lists, or a heterogeneous sequence.
- Every value accepted from a JSON object is retained: string, integer, real, boolean, `null`, object, empty array,
  homogeneous array, heterogeneous array, and nested array.
- Encoding a successfully decoded JSON object does not silently omit or flatten a value.
- JSONPath queries can select values from both objects and arrays.

The properties root remains an object. Supporting a scalar or array as the root of `celix_properties_load*` is out of
scope; root arrays remain supported by `celix_arrayList_load*`.

## Current implementation constraints

The relevant code is concentrated in:

- `libs/utils/include/celix_array_list.h` and `libs/utils/src/array_list.c`;
- `libs/utils/include/celix_array_list_encoding.h` and
  `libs/utils/src/celix_array_list_encoding.c`;
- `libs/utils/include/celix_properties.h` and `libs/utils/src/properties.c`;
- `libs/utils/src/properties_encoding.c`;
- the header-only C++ wrapper in `libs/utils/include/celix/Properties.h`;
- the array-list, properties, encoding, C++, and error-injection test suites in `libs/utils/gtest/src`.

Important current behavior that must be deliberately replaced or preserved:

- Array lists have one element type for the entire list. Their backing storage is an array of
  `celix_array_list_entry_t` unions, which is a useful compact representation and must stay compact for existing types.
- Empty JSON arrays decode to `NULL`; heterogeneous, object, `null`, and nested arrays are ignored or rejected according
  to flags.
- JSON objects are flattened into dotted keys during decoding. The nested encoding style reconstructs objects by
  splitting keys. Literal member names and structural nesting therefore cannot be distinguished.
- JSON `null` is ignored or rejected.
- Strings matching `version<...>` are automatically decoded as Celix versions. This prevents an arbitrary JSON string
  with that content from retaining its JSON string type.
- NaN and infinity, which JSON cannot represent, may currently cause the containing value to be omitted.
- Several consumers switch over the public array-list and properties type enums. Every such switch must be audited when
  new values are appended.

## Design rules

1. Append new public enum values; do not renumber existing values.
2. Keep `celix_array_list_entry_t` pointer-sized on supported 64-bit platforms. Add pointer members to its union rather
   than embedding a large tagged union in every list slot.
3. Pay the per-entry allocation cost only for a heterogeneous array. This is expected to be an uncommon case.
4. Use recursive ownership, deep copy, deep equality, and recursive destruction for structured values.
5. Do not expose mutable borrowed children. Getters for nested properties and array lists return `const` pointers owned
   by their parent.
6. Copying setters make a deep copy. `assign` setters take ownership and destroy the supplied value even when insertion
   fails, matching the existing Celix assign conventions.
7. Reject direct self-assignment of a properties object or array list as a child of itself. Because structured getters
   are const and assignment transfers exclusive ownership, the public API must not permit cycles.
8. Preserve a non-`NULL` `celix_properties_entry_t::value` for all stored types so existing iteration and filter code
   cannot dereference `NULL`. Use `"null"` for null and a compact JSON representation for structured values.
9. Never silently discard a valid JSON value. Invalid API input, allocation failure, integer overflow, invalid UTF-8
   where it is checked, and non-finite doubles must return an error and add context to `celix_err`.
10. Keep the C implementation and public C API authoritative. Update the header-only C++ API after each C data-model
    stage rather than inventing a separate C++ representation.

## Stage 1: Extend `celix_array_list_t`

### 1.1 Add structured and heterogeneous element types

Append these element types to `celix_array_list_element_type_t`:

- `CELIX_ARRAY_LIST_ELEMENT_TYPE_PROPERTIES`;
- `CELIX_ARRAY_LIST_ELEMENT_TYPE_ARRAY_LIST`;
- `CELIX_ARRAY_LIST_ELEMENT_TYPE_VARIANT`.

Add corresponding pointer members to `celix_array_list_entry_t`. Forward-declare properties through
`celix_properties_type.h` to avoid including the full properties API in the array-list type declarations.

Add an exposed read-only variant description for values in a variant list:

```c
typedef enum celix_array_list_variant_type {
    CELIX_ARRAY_LIST_VARIANT_TYPE_NULL,
    CELIX_ARRAY_LIST_VARIANT_TYPE_STRING,
    CELIX_ARRAY_LIST_VARIANT_TYPE_LONG,
    CELIX_ARRAY_LIST_VARIANT_TYPE_DOUBLE,
    CELIX_ARRAY_LIST_VARIANT_TYPE_BOOL,
    CELIX_ARRAY_LIST_VARIANT_TYPE_VERSION,
    CELIX_ARRAY_LIST_VARIANT_TYPE_PROPERTIES,
    CELIX_ARRAY_LIST_VARIANT_TYPE_ARRAY_LIST,
} celix_array_list_variant_type_e;

typedef struct celix_array_list_variant {
    celix_array_list_variant_type_e type;
    union {
        const char* stringValue;
        long longValue;
        double doubleValue;
        bool boolValue;
        const celix_version_t* versionValue;
        const celix_properties_t* propertiesValue;
        const celix_array_list_t* arrayListValue;
    } value;
} celix_array_list_variant_t;
```

The public variant is a borrowed value description. `celix_arrayList_addVariant` deep-copies it. Internally, a variant
array stores a separately allocated owning variant object per list slot. Add a private ownership-transfer helper for the
JSON decoder so it does not copy a value it just created; a public assign API can be added only if its ownership contract
can be made as clear as the existing assign functions.

### 1.2 Add typed array-list APIs

Add and document:

```c
celix_array_list_t* celix_arrayList_createPropertiesArray(void);
celix_array_list_t* celix_arrayList_createArrayListArray(void);
celix_array_list_t* celix_arrayList_createVariantArray(void);

const celix_properties_t* celix_arrayList_getProperties(const celix_array_list_t* list, int index);
const celix_array_list_t* celix_arrayList_getArrayList(const celix_array_list_t* list, int index);
const celix_array_list_variant_t* celix_arrayList_getVariant(const celix_array_list_t* list, int index);

celix_status_t celix_arrayList_addProperties(celix_array_list_t* list, const celix_properties_t* value);
celix_status_t celix_arrayList_assignProperties(celix_array_list_t* list, celix_properties_t* value);
celix_status_t celix_arrayList_addArrayList(celix_array_list_t* list, const celix_array_list_t* value);
celix_status_t celix_arrayList_assignArrayList(celix_array_list_t* list, celix_array_list_t* value);
celix_status_t celix_arrayList_addVariant(celix_array_list_t* list,
                                         const celix_array_list_variant_t* value);
```

Also extend `celix_arrayList_elementTypeToString`.

Configure default callbacks for the new types:

- properties: recursive destroy, `celix_properties_equals`, and `celix_properties_copy`;
- nested array list: recursive destroy, `celix_arrayList_equals`, and `celix_arrayList_copy`;
- variant: destroy/copy/compare equality by variant type and recursively by value.

Do not install a default sort callback for properties, nested array lists, or variants. `celix_arrayList_sort` should
remain a no-op for those types unless the caller explicitly supplies a comparator.

### 1.3 Define JSON array type inference

Use this deterministic decoding rule:

| JSON array content | Celix element type |
| --- | --- |
| empty | `VARIANT` |
| all strings | `STRING` |
| all integers fitting in `long` | `LONG` |
| all reals | `DOUBLE` |
| integers and reals mixed together | `VARIANT` |
| all booleans | `BOOL` |
| all objects | `PROPERTIES` |
| all arrays | `ARRAY_LIST` |
| all `null`, or any other combination | `VARIANT` |

Do not infer `VERSION` from JSON strings in the JSON-compatible default mode. Programmatically created version arrays
remain supported and encode as arrays of tagged strings.

JSON integers and reals are distinct value types. Do not promote a mixed integer/real JSON array to `DOUBLE`: doing so
can round a large integer and makes an exact JSON tree round trip impossible. Represent a mixed numeric array as
`VARIANT`, retaining a `LONG` or `DOUBLE` tag for every element. Programmatically created homogeneous double arrays
remain supported.

A variant entry must be able to contain another array list or properties object, so arbitrarily nested JSON arrays can
be represented. Empty arrays use `VARIANT` because their element type is unknowable; unlike today, an empty variant list
is still a real list and encodes as `[]`.

### 1.4 Array-list tests

Extend `ArrayListTestSuite.cc`, `ArrayListErrorInjectionTestSuite.cc`,
`CelixArrayListEncodingTestSuite.cc`, and `CelixArrayListEncodingErrorInjectionTestSuite.cc` to cover:

- add, assign, get, remove, clear, copy, equality, and destruction for properties and nested array-list types;
- a variant list containing every variant type;
- deep nesting and direct-cycle rejection;
- empty variant arrays;
- mixed integer/real decoding to a variant array without changing either numeric value or tag;
- all-object, all-array, all-null, and genuinely heterogeneous arrays;
- rollback and cleanup after every new fallible allocation;
- preservation of the backing union size, with a compile-time assertion where portable;
- JSON round trips for `[]`, `[{}, {}]`, `[[1], []]`, `[null, 1, "two", {"three": 3}]`, and nested combinations.

## Stage 2: Make properties a recursive JSON object model

### 2.1 Add null and nested properties types

Append to `celix_properties_value_type_e`:

- `CELIX_PROPERTIES_VALUE_TYPE_NULL`;
- `CELIX_PROPERTIES_VALUE_TYPE_PROPERTIES`.

Add `propertiesValue` to the typed union in `celix_properties_entry_t`, without increasing the union beyond a pointer
slot.

Add:

```c
celix_status_t celix_properties_setNull(celix_properties_t* properties, const char* key);
bool celix_properties_isNull(const celix_properties_t* properties, const char* key);

celix_status_t celix_properties_setProperties(celix_properties_t* properties,
                                              const char* key,
                                              const celix_properties_t* value);
celix_status_t celix_properties_assignProperties(celix_properties_t* properties,
                                                 const char* key,
                                                 celix_properties_t* value);
const celix_properties_t* celix_properties_getProperties(const celix_properties_t* properties,
                                                         const char* key);
```

`celix_properties_setArrayList` and `celix_properties_assignArrayList` must accept all JSON-capable array-list element
types added in Stage 1. They must continue rejecting `UNDEFINED` and raw `POINTER` lists.

Update entry creation, string representation, destruction, `celix_properties_setEntry`, copy, equality, iteration, and
statistics. Deep-copy nested structures. For a null entry use the stable string `"null"`; for properties and array-list
entries store a compact JSON string snapshot in `entry.value`.

### 2.2 Preserve object structure

Remove structural meaning from characters in a properties key:

- `"a.b"` and `"a/b"` are literal top-level member names;
- a nested object exists only when the value is a `CELIX_PROPERTIES_VALUE_TYPE_PROPERTIES`;
- `{ "a.b": 1 }` and `{ "a": { "b": 1 } }` must remain different after decode and encode.

Delete the dotted/slashed key-combining path in `properties_encoding.c` after legacy compatibility, if any, has been
isolated. Do not keep two internal representations of nested objects.

### 2.3 Audit non-codec consumers

Search the complete source tree for switches and assumptions involving:

- `CELIX_PROPERTIES_VALUE_TYPE_*`;
- `CELIX_ARRAY_LIST_ELEMENT_TYPE_*`;
- `entry.value`;
- `celix_properties_getArrayList`;
- `celix_arrayList_getElementType`.

At minimum, update `libs/utils/src/filter.c`, `libs/utils/src/celix_convert_utils.c`, the framework property validation
code, DFI/RSA users, and `libs/utils/include/celix/Properties.h` where applicable.

LDAP-style filters should retain their current behavior for existing scalar and homogeneous scalar-array values. Define
structured properties, nested arrays, variants, and null to match only a presence test; comparison, substring, and
ordering operations return false rather than asserting or string-comparing an implementation detail.

### 2.4 Properties tests

Extend the regular and error-injection property tests for:

- null set/get/type/replace/copy/equality;
- nested set/assign/get/copy/equality/destruction;
- properties arrays and heterogeneous arrays as property values;
- literal empty, dotted, slashed, quoted, Unicode, and escaped keys;
- recursive cleanup at each allocation failure;
- no change to existing scalar and homogeneous array behavior;
- defined filter behavior for all new types.

## Stage 3: Replace the lossy JSON codec

### 3.1 Use shared recursive conversion helpers

Refactor the private Jansson conversion code so properties and array-list encoding call the same recursive value
conversion helpers. Keep Jansson types out of public headers.

The mapping is:

| Celix value | JSON value |
| --- | --- |
| string | string |
| long | integer |
| double | number |
| bool | boolean |
| null | `null` |
| nested properties | object |
| array list | array |
| version | tagged JSON string, for existing Celix API compatibility |

The reverse mapping follows JSON types exactly. In particular, a JSON string is a Celix string by default, even if it
looks like a tagged version.

Before converting a Jansson integer to `long`, check its range. Return `CELIX_ILLEGAL_ARGUMENT` with a useful
`celix_err` message on platforms where it does not fit. Always reject NaN and infinity during encoding; never omit the
member or array element.

Use an all-or-nothing decode transaction: construct the complete child tree off to the side and publish `*out` only
after success. Every failure must release the partially built tree.

### 3.2 Encoding and decoding flags

Retain the flag that still expresses useful JSON output behavior:

- `CELIX_PROPERTIES_ENCODE_PRETTY`.

Always configure Jansson to reject duplicate object member names. A `celix_properties_t` cannot represent both values,
so accepting duplicates would silently choose one value and violate the no-data-loss goal. As rejection is
unconditional, `CELIX_PROPERTIES_DECODE_ERROR_ON_DUPLICATES` no longer changes behavior and can be deprecated.

For standalone array-list encoding, retain `CELIX_ARRAY_LIST_ENCODE_PRETTY`. Empty and heterogeneous arrays are now
supported, and non-finite values must fail unconditionally, so the other array-list encode/decode flags can be
deprecated.

Deprecate the following without reusing their bit values:

- duplicate-key flags, because duplicate JSON object members are always rejected;
- flat/nested style and collision flags, because structure is now represented by values rather than key parsing;
- error-on-empty-array flags, because empty arrays are supported;
- error-on-null flags, because null is supported;
- error-on-unsupported-array flags, because all JSON array value combinations are supported;
- error-on-empty-key flags, because an empty object member name is valid;
- error-on-NaN/Inf flags, because non-finite values must now always fail rather than be omitted;
- the old strict aggregates, which are composed from obsolete flags.

Keep deprecated macros for a source-compatibility window and document them as no-ops where safe. If maintaining legacy
flat/nested output is required for one release, isolate it behind explicitly named `LEGACY` flags; do not let it remain
the default and do not overload the JSON-compatible representation.

If users need tagged strings to decode back to Celix versions, add an opt-in
`CELIX_PROPERTIES_DECODE_LEGACY_VERSION_STRINGS` flag and the corresponding array-list flag. The default must preserve
arbitrary JSON strings as strings.

### 3.3 Compatibility and round-trip tests

In `PropertiesEncodingTestSuite.cc`:

1. Decode a fixture containing every JSON value type and nesting combination.
2. Encode it again.
3. Parse the result with Jansson and compare the JSON trees with `json_equal`; do not compare serialized member order.
4. Assert separately that `version<1.2.3>` remains a string in default mode.
5. Assert that programmatic Celix version values still encode to the documented string representation.
6. Assert that no entry is omitted for empty arrays, null, heterogeneous arrays, objects, or nested arrays.
7. Assert that dotted and slashed keys are unchanged.
8. Test duplicate-key policy, integer boundaries, non-finite numbers, invalid JSON, deep nesting, and allocation errors.
9. Include a mixed numeric boundary case such as `[9007199254740993, 0.5]` to prove that array inference does not
   round an integer through `double`.

Update the fuzz corpus in `libs/utils/fuzzing/properties_corpus` with null, empty, mixed, object, nested-array, escaped-key,
and deeply nested examples. Keep the decoder bounded against excessive nesting.

## Stage 4: Add JSONPath queries

Base the syntax and result semantics on [RFC 9535](https://www.rfc-editor.org/rfc/rfc9535.html), not the earlier informal
JSONPath proposals. Call this a documented RFC 9535 subset; do not claim full compliance.

### 4.1 Supported subset

Implement:

- root identifier: `$`;
- child name selectors in dot and bracket notation: `$.name`, `$['name']`, and `$["name"]`;
- array index selectors, including negative indexes: `$[0]` and `$[-1]`;
- wildcard child selectors: `.*` and `[*]`;
- multiple selectors in one child segment, preserving duplicates;
- array slices with positive or negative step;
- descendant segments: `..name`, `..*`, and bracket equivalents;
- JSON string escaping and optional whitespace required by the supported grammar.

Explicitly reject filter selectors, `@`, and function extensions with `CELIX_ILLEGAL_ARGUMENT` and a clear
`celix_err` message. They may be added later without changing the result APIs.

Follow these RFC semantics:

- a structural mismatch or out-of-range index produces no match, not an error;
- an invalid or unsupported query is an error;
- array results preserve array order;
- object wildcard order is unspecified;
- repeated selection retains duplicate nodes;
- descendant traversal visits a node before its descendants and visits array children in array order.

Use checked arithmetic for indexes and slices. Implement traversal iteratively or enforce a documented nesting limit so
a query cannot overflow the C stack. Check result growth for overflow and allocation failure.

### 4.2 Internal query representation

Add a private parser/evaluator pair, for example:

- `libs/utils/src/celix_properties_jsonpath.c`;
- `libs/utils/src/celix_properties_jsonpath_private.h`.

Parse a query once into a list of segments/selectors, then evaluate each segment against an internal nodelist. A node is
a borrowed typed reference to a value in the properties/array tree. It must distinguish a present null node from no
node.

Do not encode to JSON and query Jansson values. Query the Celix tree directly to avoid reparsing, type loss, and temporary
JSON allocations.

### 4.3 Public query API

Use descriptive getter names consistent with the Celix coding conventions. The following is the recommended API,
corresponding to shorter sketches such as `celix_properties_pathStr` and `celix_properties_pathAllStr`:

```c
const char* celix_properties_getStringByPath(const celix_properties_t* properties,
                                             const char* path,
                                             const char* defaultValue);
long celix_properties_getLongByPath(const celix_properties_t* properties,
                                    const char* path,
                                    long defaultValue);
double celix_properties_getDoubleByPath(const celix_properties_t* properties,
                                        const char* path,
                                        double defaultValue);
bool celix_properties_getBoolByPath(const celix_properties_t* properties,
                                    const char* path,
                                    bool defaultValue);
const celix_version_t* celix_properties_getVersionByPath(const celix_properties_t* properties,
                                                         const char* path,
                                                         const celix_version_t* defaultValue);
const celix_properties_t* celix_properties_getPropertiesByPath(
    const celix_properties_t* properties,
    const char* path,
    const celix_properties_t* defaultValue);
const celix_array_list_t* celix_properties_getArrayListByPath(
    const celix_properties_t* properties,
    const char* path,
    const celix_array_list_t* defaultValue);

bool celix_properties_hasPath(const celix_properties_t* properties, const char* path);
bool celix_properties_hasStringPath(const celix_properties_t* properties, const char* path);
bool celix_properties_hasLongPath(const celix_properties_t* properties, const char* path);
bool celix_properties_hasDoublePath(const celix_properties_t* properties, const char* path);
bool celix_properties_hasBoolPath(const celix_properties_t* properties, const char* path);
bool celix_properties_hasVersionPath(const celix_properties_t* properties, const char* path);
bool celix_properties_hasPropertiesPath(const celix_properties_t* properties, const char* path);
bool celix_properties_hasArrayListPath(const celix_properties_t* properties, const char* path);
bool celix_properties_hasNullPath(const celix_properties_t* properties, const char* path);

bool celix_properties_checkPath(const char* path);
```

Single-value getters scan the RFC nodelist in order and return the first result of the requested exact type. If there is
no such result, they return the supplied default. A returned selected pointer is borrowed from the root properties
object; a returned default pointer is exactly the caller-supplied pointer.

`celix_properties_checkPath` parses the path without evaluating it and returns `true` if it is valid and supported by the
implemented RFC 9535 subset. It returns `false` for malformed or unsupported paths and prints a detailed error to
`celix_err`. Design this primarily for tests, so applications can verify their fixed JSONPath expressions before
production. Production code should normally use known-valid paths without checking on every retrieval. If a path is
provided by a user or another untrusted external source, validate it once with `celix_properties_checkPath` before using
it so that an invalid path can be distinguished from a valid path that selects no values.

All convenience query functions use the same error model:

- malformed or unsupported paths print an error to `celix_err`;
- single-value getters return the supplied default;
- `has*Path` functions return `false`;
- all-result getters return an empty array list of the requested type;
- allocation failure is printed to `celix_err` and is the only case in which an all-result getter may return `NULL`.

Do not return `celix_status_t` from one retrieval family while returning values directly from the other. Callers wanting
up-front path validation use `celix_properties_checkPath`.

Do not perform implicit string-to-number conversions in these APIs. A future `getAs<Type>ByPath` family can provide that
behavior without weakening typed JSONPath access.

The all-result APIs directly return an owned array list:

```c
celix_array_list_t* celix_properties_getAllStringsByPath(const celix_properties_t* properties,
                                                         const char* path);
celix_array_list_t* celix_properties_getAllLongsByPath(const celix_properties_t* properties,
                                                       const char* path);
celix_array_list_t* celix_properties_getAllDoublesByPath(const celix_properties_t* properties,
                                                         const char* path);
celix_array_list_t* celix_properties_getAllBoolsByPath(const celix_properties_t* properties,
                                                       const char* path);
celix_array_list_t* celix_properties_getAllVersionsByPath(const celix_properties_t* properties,
                                                          const char* path);
celix_array_list_t* celix_properties_getAllPropertiesByPath(const celix_properties_t* properties,
                                                            const char* path);
celix_array_list_t* celix_properties_getAllArrayListsByPath(const celix_properties_t* properties,
                                                            const char* path);
celix_array_list_t* celix_properties_getAllValuesByPath(const celix_properties_t* properties,
                                                        const char* path);
```

The returned list is owned by the caller and has the requested element type even when it is empty.
`getAllValuesByPath` returns a variant list. Typed all-result calls retain only results of the requested exact type.
Result lists own deep copies, including strings and structured values, so they do not depend on the lifetime of the
queried properties object.

If repeated parsing becomes measurable, add an opaque compiled-query API later. Do not complicate the first public
surface with caching.

### 4.4 JSONPath tests

Add `PropertiesJsonPathTestSuite.cc` and an error-injection counterpart. Use the RFC 9535 bookstore example for every
supported selector and add focused cases for:

- root, literal unusual names, quotes, escapes, Unicode, and empty names;
- positive, negative, and out-of-range indexes;
- wildcard object/array behavior;
- selector unions and retained duplicates;
- forward, reverse, omitted-bound, and zero-step slices;
- descendant traversal through objects, arrays, empty values, and null;
- structural mismatch producing an empty result;
- invalid and explicitly unsupported queries;
- `checkPath` returning true for supported syntax and false with an error for invalid or unsupported syntax;
- invalid-path convenience behavior: single getters return defaults, `has*Path` returns false, and all-result getters
  return a correctly typed empty list;
- first typed result and default behavior;
- `hasPath` being true for null while typed `has*Path` results remain exact;
- all-result calls returning a non-`NULL` correctly typed empty list for no matches and invalid paths;
- deep-copy ownership of all-result lists;
- overflow, nesting/result limits, and allocation failures.

Add a JSONPath parser/evaluator fuzz target or extend the existing properties fuzz target with a separate path input.

## Stage 5: C++ parity and documentation

Extend `celix::Properties::ValueType` by appending `Null` and `Properties`. Add C++ APIs for null and nested properties,
and add save/load tests showing that nested objects are preserved.

The utils C++ API must remain compatible with the project's configured C++ standard. Do not use `std::variant` unless
the minimum supported standard permits it. For heterogeneous arrays, either add a small Celix tagged-value wrapper over
`celix_array_list_variant_t` or initially expose explicit typed accessors; do not represent heterogeneous values as
strings.

### 5.1 Expected C++ JSONPath API

Mirror the C convenience API and naming. Path syntax errors must not force normal callers to handle a status or
exception. The expected public surface is:

```cpp
class Properties;

class PropertyValue {
public:
    enum class Type { Null, String, Long, Double, Bool, Version, Properties, Array };

    Type getType() const noexcept;
    bool isNull() const noexcept;
    std::string getString(const std::string& defaultValue = {}) const;
    long getLong(long defaultValue = 0L) const;
    double getDouble(double defaultValue = 0.0) const;
    bool getBool(bool defaultValue = false) const;
    celix::Version getVersion(const celix::Version& defaultValue = {}) const;
    celix::Properties getProperties() const;
    celix::Properties getProperties(const celix::Properties& defaultValue) const;
    std::vector<PropertyValue> getArray(const std::vector<PropertyValue>& defaultValue = {}) const;
};

class Properties {
public:
    celix::Properties getProperties(
        const std::string& key, const celix::Properties& defaultValue = {}) const;

    static bool checkPath(const std::string& path);

    std::string getStringByPath(
        const std::string& path, const std::string& defaultValue = {}) const;
    long getLongByPath(const std::string& path, long defaultValue = 0L) const;
    double getDoubleByPath(const std::string& path, double defaultValue = 0.0) const;
    bool getBoolByPath(const std::string& path, bool defaultValue = false) const;
    celix::Version getVersionByPath(
        const std::string& path, const celix::Version& defaultValue = {}) const;
    celix::Properties getPropertiesByPath(
        const std::string& path, const celix::Properties& defaultValue = {}) const;
    std::vector<PropertyValue> getArrayByPath(
        const std::string& path,
        const std::vector<PropertyValue>& defaultValue = {}) const;

    std::vector<std::string> getStringVectorByPath(
        const std::string& path, const std::vector<std::string>& defaultValue = {}) const;
    std::vector<long> getLongVectorByPath(
        const std::string& path, const std::vector<long>& defaultValue = {}) const;
    std::vector<double> getDoubleVectorByPath(
        const std::string& path, const std::vector<double>& defaultValue = {}) const;
    std::vector<bool> getBoolVectorByPath(
        const std::string& path, const std::vector<bool>& defaultValue = {}) const;
    std::vector<celix::Version> getVersionVectorByPath(
        const std::string& path, const std::vector<celix::Version>& defaultValue = {}) const;
    std::vector<celix::Properties> getPropertiesVectorByPath(
        const std::string& path, const std::vector<celix::Properties>& defaultValue = {}) const;

    std::vector<std::string> getAllStringsByPath(const std::string& path) const;
    std::vector<long> getAllLongsByPath(const std::string& path) const;
    std::vector<double> getAllDoublesByPath(const std::string& path) const;
    std::vector<bool> getAllBoolsByPath(const std::string& path) const;
    std::vector<celix::Version> getAllVersionsByPath(const std::string& path) const;
    std::vector<celix::Properties> getAllPropertiesByPath(const std::string& path) const;
    std::vector<std::vector<PropertyValue>> getAllArraysByPath(const std::string& path) const;
    std::vector<PropertyValue> getAllValuesByPath(const std::string& path) const;

    bool hasPath(const std::string& path) const;
    bool hasStringPath(const std::string& path) const;
    bool hasLongPath(const std::string& path) const;
    bool hasDoublePath(const std::string& path) const;
    bool hasBoolPath(const std::string& path) const;
    bool hasVersionPath(const std::string& path) const;
    bool hasPropertiesPath(const std::string& path) const;
    bool hasArrayPath(const std::string& path) const;
    bool hasNullPath(const std::string& path) const;
};
```

Use `const&` for nontrivial input and default-value parameters (`std::string`, `celix::Version`, `celix::Properties`,
and vectors); keep scalar defaults by value. Owning results such as strings, versions, properties copies, and vectors
remain return-by-value APIs so that C++14 move construction can be used.

Nested-object getters return an owning `celix::Properties` copy. A `const celix::Properties&` cannot be returned safely
because the C tree stores a `celix_properties_t`, not a stable C++ wrapper object whose address can be referenced. Do not
add a separate view type or a mutable wrapper cache solely to manufacture reference semantics.

`PropertyValue` is a small owning C++14-compatible tagged-value wrapper. Do not use `std::variant`. Its implementation
must own a deep copy of strings and structured C values through RAII, and its copy operations must remain deep and safe.
It is the lossless C++ representation used only where a JSON result can be heterogeneous or recursively nested.

Array-list conversion follows these rules:

- `getAll<Type>ByPath` calls the matching C all-result function, copies each entry into a `std::vector<T>`, and destroys
  the temporary `celix_array_list_t` after conversion.
- `get<Type>VectorByPath` selects one array node. It converts only an array with the exact homogeneous element type;
  a missing, invalid, or differently typed result returns the supplied default vector.
- `getArrayByPath` converts any selected array recursively to `std::vector<PropertyValue>`. A homogeneous list becomes
  one `PropertyValue` per element; a variant list retains every variant tag; nested array lists become nested vectors;
  and properties elements become owning `celix::Properties` values.
- `getAllArraysByPath` applies that recursive conversion to every selected array node.
- Empty C array lists become empty vectors. The C++ vector does not retain an otherwise unknowable element type.
- Strings, versions, properties, and nested arrays in returned vectors are owning values. Returned vectors and
  `PropertyValue` instances do not borrow storage from the queried `Properties` object.

The C++ error model mirrors C: malformed or unsupported paths are printed to `celix_err` and getters return their
default or an empty vector. `Properties::checkPath` is primarily for validating fixed paths in tests; production code
should normally use known-valid paths directly. Validate a user-provided or otherwise untrusted path once with
`Properties::checkPath` before retrieval. Only allocation failure is translated to `std::bad_alloc`; an invalid path
does not throw `celix::IllegalArgumentException`.

Add C++ tests for every method family, exact-type vector conversion, defaults, empty arrays, mixed arrays, nested arrays,
deep ownership after destroying the source properties, independent nested-properties copies, invalid-path logging,
`checkPath`, allocation failure, and default temporaries passed through `const&`.

Update:

- `documents/properties_and_filter.md`;
- `documents/properties_encoding.md`;
- the Doxygen comments in all changed public headers;
- C++ API examples;
- release notes and deprecation notes for changed codec defaults and flags.

Document that a Celix version is an extension type encoded as a JSON string and is not recoverable as a version in
standards-compatible decoding unless the explicit legacy-version flag is used.

## Implementation-review correction gate

The initial implementation must not be considered complete merely because the existing utils tests pass. Before
continuing with feature work, reconcile it with the contracts above and add tests that fail on the current incomplete
behavior.

### Correct the recursive storage and codec foundation

- Add the private ownership-transfer helper for variant entries described in Stage 1. The JSON decoder currently creates
  a structured child and then deep-copies it through the public borrowed-value API. Publish the already-owned child
  atomically instead.
- Preserve the real error status while creating `celix_properties_entry_t::value` snapshots. Invalid UTF-8,
  non-finite numbers, invalid variant tags, and other illegal values must not be reported as `CELIX_ENOMEM`.
- Make structured setters atomic if snapshot generation fails, and add error context for invalid direct self-assignment
  and every new public invalid-input path.
- Document the exceptional ownership rule for rejected direct self-assignment: an assign function cannot both destroy
  the supplied object and leave the parent alive when both pointers are the same. Test this case separately from normal
  insertion failure, where assign APIs must destroy the supplied value.
- Reject negative indexes in the new array-list getters without reading before the backing array. Keep their behavior
  consistent with the documented invalid-index result.
- Configure standalone array-list JSON loading with `JSON_REJECT_DUPLICATES`, because an array can contain objects. Test
  duplicate members at the root properties level and inside standalone arrays, nested arrays, and variant entries.
- Apply decode policy consistently at every recursion boundary. Do not pass `0` for nested objects while forwarding
  flags to nested arrays. Once obsolete flags are no-ops, recursion should use only the remaining meaningful flags.
- Perform the documented `json_int_t`-to-`long` range check in homogeneous arrays, variant entries, and properties.
  Never cast first and check later.

### Remove remaining lossy legacy behavior

- Non-finite property doubles are still omitted when the error flag is absent. Make rejection unconditional for scalar,
  homogeneous-array, nested, and variant values, with the failing key/index in `celix_err`.
- The old flat/nested key-splitting implementation and its collision behavior are still active. Either remove them or
  move them behind explicitly named legacy flags. The JSON-compatible path must always treat dots, slashes, empty
  strings, and escape characters as literal member-name content.
- Mark obsolete C and C++ flags as deprecated and make the documented no-op flags actually be no-ops. Remove tests that
  preserve strict-mode rejection of valid empty arrays or null values, and replace them with compatibility/no-op tests.
- Keep unconditional duplicate rejection and unconditional non-finite rejection independent of legacy or strict flags.
- Update the public header comments, `documents/properties_encoding.md`, examples, and release notes at the same time as
  the flag behavior. The current comments still describe empty arrays as unrepresentable and nested key splitting as
  the default object model.

### Complete the downstream consumer audit

- Update LDAP filter matching before structured values are exposed broadly. Null, nested properties, structured arrays,
  and variants match presence only; equality, approximation, substring, and ordering must not compare the cached JSON
  string in `entry.value`.
- Interpret a filter attribute as JSONPath only when it starts with `$`. Attributes without `$` remain literal root keys,
  including names containing dots or brackets.
- Add focused filter tests for null, nested objects, object arrays, nested arrays, variants, and empty arrays. Retain the
  existing scalar and homogeneous scalar-array behavior.
- Review every source-tree use of the public properties and array-list type enums, `entry.value`,
  `celix_properties_getArrayList`, and `celix_arrayList_getElementType`. For each switch or type assumption, either add
  the new cases or document why the input is restricted before the switch.
- Permit structured values in framework service properties so local services can use the complete properties model and
  JSONPath filter matching. Validate or adapt structured values at downstream serialization boundaries such as RSA/DFI;
  a compact JSON snapshot is an iteration compatibility value, not permission for an existing string consumer to accept
  a structured value silently.

### Replace the partial JSONPath implementation

- Keep the JSONPath API private or experimental until the parser/evaluator implements the complete documented subset.
  The initial resolver supports only a single chain of child names and array indexes; it does not implement nodelists,
  wildcards, selector unions, slices, descendants, RFC string unescaping, or optional grammar whitespace.
- Parse once into selectors and evaluate against an ordered nodelist. Single-result getters scan for the first matching
  exact type; all-result getters retain every typed match and duplicate node in RFC order.
- Give the internal parser/evaluator a status-bearing result rather than a single boolean. It must distinguish malformed
  input, allocation failure, no match, and a structural mismatch so that allocation failure is the only reason an
  all-result API returns `NULL`.
- Decode quoted member-name selectors, including JSON escapes, Unicode escapes, empty names, and escaped quotes, before
  property lookup. Validate dot-name shorthand according to the supported RFC grammar instead of accepting arbitrary
  bytes until the next delimiter.
- Add checked traversal/result limits and either iterative descendant traversal or an enforced nesting limit. Add the
  parser/evaluator fuzz target before exporting the API.
- Add the dedicated regular and error-injection suites from Stage 4. Existing convenience tests for `$.name` and
  `$[index]` do not establish subset compliance.

### Finish C++ parity and error handling

- Add the missing null and nested-properties setters, version and array single-result getters, every typed all-result
  family, homogeneous vector getters, generic arrays, generic values, and the owning recursive `PropertyValue`.
- Ensure every selected nested-properties copy is checked before constructing `celix::Properties`. A failed
  `celix_properties_copy` must throw `std::bad_alloc`, not create a wrapper containing `nullptr`.
- Keep invalid paths non-throwing as documented, while translating only actual allocation failures to
  `std::bad_alloc`. This depends on the C evaluator preserving the error distinction.
- Add C++ error-injection and lifetime tests for selected properties, default properties, strings, versions, homogeneous
  arrays, variants, and nested arrays.

### Restore review and repository hygiene

- Replace the abbreviated header in new C/C++ sources with the repository's standard ASF license header.
- Minimize unrelated whole-file formatting churn before review. Format new and materially changed C/C++ code with the
  repository `.clang-format`, but keep the functional diff separable from tool-only reformatting and Graphify setup.
- Add or update fuzz corpus files, Doxygen, user documentation, deprecation notes, and release notes in the same staged
  commits as the behavior they describe.
- Run the scoped utils suite after every corrective step and the full suite before submission. Passing the existing
  scoped suite is a baseline, not evidence that the new error, ownership, filter, JSONPath, and compatibility paths are
  covered.

## Suggested change sequence

Keep each step buildable and reviewable:

1. Add properties, nested-array, and variant array-list storage plus unit/error-injection tests.
2. Extend array-list JSON encoding/decoding and its fuzz corpus.
3. Add null and nested properties storage plus unit/error-injection/filter tests.
4. Replace properties JSON flattening with recursive JSON conversion and update flag behavior.
5. Add full JSON tree round-trip tests and update existing serialization expectations.
6. Add the JSONPath parser/evaluator and generic internal nodelist tests.
7. Add typed C JSONPath APIs and their ownership/error tests.
8. Add C++ parity, documentation, deprecation notes, and downstream switch audits.

After every step, format changed C/C++ files with the repository `.clang-format`, build, and run the scoped utils tests:

```bash
cmake --build build --parallel
ctest --output-on-failure --test-dir build/libs/utils
```

Before submitting the final change, run the full suite:

```bash
ctest --output-on-failure --test-dir build
```

New allocation and conversion paths must be covered with the Celix error-injector libraries, aiming for more than 95%
line coverage as required by the project.

## Completion criteria

The implementation is complete when:

- any valid JSON object within documented depth/size limits decodes without ignored members;
- decoding and re-encoding preserves its JSON tree, modulo object member order and non-lossy numeric formatting;
- mixed integer/real arrays preserve each element's numeric value and JSON numeric type without `double` promotion;
- empty, object, nested, null, and heterogeneous arrays are first-class array-list values;
- nested properties, not key separators or encoding flags, determine JSON object nesting;
- existing scalar and homogeneous-array properties APIs retain their behavior;
- invalid/non-representable input fails atomically with useful `celix_err` context;
- all supported RFC 9535 examples produce the specified nodelists;
- unsupported JSONPath features are reported to `celix_err` and rejected by `checkPath`;
- single JSONPath getters return their default on no typed match or an invalid path;
- all-result JSONPath calls return an owned, correctly typed empty array list for no matches or an invalid path, with
  `NULL` reserved for allocation failure;
- C++ typed queries return owning `std::vector<T>` values and generic array queries preserve mixed and nested values
  through `PropertyValue`;
- C, C++, codec, JSONPath, fuzz, error-injection, scoped, and full-suite tests pass.
