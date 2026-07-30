---
title: Apache Celix Properties Encoding
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

# Apache Celix Properties JSON Encoding

Celix properties form a recursive, JSON-compatible object model. Property keys are JSON member names and are always
used literally: dots, slashes, empty strings, and escape characters have no special encoding meaning. A nested JSON
object is represented by a nested `celix_properties_t` value, not by splitting a key.

## Value mapping

| Celix value | JSON value |
|---|---|
| null | null |
| string | string |
| long | integer number |
| double | real number |
| bool | boolean |
| nested properties | object |
| array list | array |
| version extension | tagged string such as `version<1.2.3>` |

Every valid JSON array shape is representable. Homogeneous arrays use their corresponding typed array list. Empty,
heterogeneous, or null-containing arrays use a variant array list; arrays of objects and arrays of arrays use their
dedicated recursive element types. Mixed integer/real arrays remain variants so that both the integer value and the JSON
numeric type are preserved. In particular, `[9007199254740993, 0.5]` is not promoted through `double`.

JSON strings are decoded as strings. A string that happens to look like `version<...>` is not inferred as a Celix
version. Programmatically created version values still encode using the tagged string extension, but this type is not
recoverable through standards-compatible decoding.

## Encoding

Use `celix_properties_save`, `celix_properties_saveToStream`, or `celix_properties_saveToString`.
`CELIX_PROPERTIES_ENCODE_PRETTY` controls whitespace. The historical flat/nested, collision, empty-array, and non-finite
flags are deprecated compatibility no-ops. Nested values always determine object structure, and empty arrays are always
written as `[]`.

NaN, positive infinity, and negative infinity are not JSON values and are rejected unconditionally, including when they
occur in nested properties, homogeneous arrays, or variants. Invalid UTF-8 member names and strings are also rejected.
No property is silently omitted.

```c
celix_autoptr(celix_properties_t) root = celix_properties_create();
celix_autoptr(celix_properties_t) child = celix_properties_create();
celix_properties_setString(child, "name", "example");
celix_properties_setProperties(root, "parent", child);
celix_properties_setString(root, "literal.key/with/slash", "unchanged");

celix_autofree char* json = NULL;
celix_properties_saveToString(root, CELIX_PROPERTIES_ENCODE_PRETTY, &json);
```

This produces an object equivalent to:

```json
{
  "parent": {"name": "example"},
  "literal.key/with/slash": "unchanged"
}
```

## Decoding

Use `celix_properties_load`, `celix_properties_loadFromStream`, or `celix_properties_loadFromString`. Duplicate member
names are rejected unconditionally at the root and in every nested object, including objects inside arrays. Integers
must fit in a C `long`; out-of-range input is rejected rather than truncated.

All historical error-policy decode flags are deprecated no-ops. Nulls, empty member names, empty arrays, objects, nested
arrays, and heterogeneous arrays are valid and are never ignored. The opt-in
`CELIX_PROPERTIES_DECODE_LEGACY_VERSION_STRINGS` flag decodes tagged strings as versions for Celix-specific formats such
as bundle manifests. Allocation failure returns `CELIX_ENOMEM`; malformed or unrepresentable input returns
`CELIX_ILLEGAL_ARGUMENT`, with context available through `celix_err`.

## JSONPath queries

The properties API provides typed single-result, presence, and all-result JSONPath functions. The supported subset
includes root (`$`), child and bracket names, escaped quoted names, array indexes (including negative indexes),
wildcards, selector unions, slices, and descendant segments. All-result functions preserve result order and duplicates
and return owned deep copies for strings, versions, properties, arrays, and variants.

For example, `$.parent.child`, `$.parent.children[2]`, and `$..name` select nested values. Invalid or unsupported paths
return the documented fallback or an empty typed result; allocation failure is the only reason an all-result function
returns `NULL`.
