#ifndef QS_REFLECT_H
#define QS_REFLECT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Forward-declare cJSON so consumers don't need the full header. */
struct cJSON;

/* ================================================================
   FIELD TYPES
   ================================================================ */

typedef enum Qs_FieldType {
    QS_FIELD_FLOAT,       /* float                   */
    QS_FIELD_FLOAT2,      /* float[2]                */
    QS_FIELD_FLOAT3,      /* float[3]                */
    QS_FIELD_FLOAT4,      /* float[4]                */
    QS_FIELD_INT32,       /* int32_t                 */
    QS_FIELD_UINT32,      /* uint32_t                */
    QS_FIELD_BOOL,        /* bool                    */
    QS_FIELD_STRING,      /* char[] fixed-size buf   */
    QS_FIELD_ENTITY,      /* Qs_Entity (uint32_t)    */
} Qs_FieldType;

/* ================================================================
   FIELD / TYPE DESCRIPTORS
   ================================================================ */

typedef struct Qs_FieldInfo {
    const char   *name;    /* JSON key / display name          */
    Qs_FieldType  type;
    size_t        offset;  /* offsetof(Struct, field)           */
    size_t        size;    /* sizeof(field) — STRING: buf size  */
} Qs_FieldInfo;

typedef struct Qs_TypeInfo {
    const char         *name;
    size_t              data_size;
    const Qs_FieldInfo *fields;
    uint32_t            field_count;
} Qs_TypeInfo;

/* ================================================================
   CONVENIENCE MACROS
   ================================================================ */

/// Declare a field descriptor: QS_FIELD(Qs_Transform, position, QS_FIELD_FLOAT3)
#define QS_FIELD(struct_type, field, ftype) \
    { #field, (ftype), offsetof(struct_type, field), \
      sizeof(((struct_type *)0)->field) }

/// Array length helper.
#define QS_COUNTOF(arr) ((uint32_t)(sizeof(arr) / sizeof((arr)[0])))

/* ================================================================
   TYPE REGISTRY
   ================================================================ */

/// Registers a type for reflection.  The Qs_TypeInfo is shallow-copied
/// internally so the fields array must have static lifetime.
/// Returns the stored copy, or NULL on failure.
const Qs_TypeInfo *qs_type_register(const Qs_TypeInfo *info);

/// Finds a registered type by name.  Returns NULL if not found.
const Qs_TypeInfo *qs_type_find(const char *name);

/* ================================================================
   GENERIC SERIALIZATION
   ================================================================ */

/// Serializes a struct to a new cJSON object.  Caller owns the result.
struct cJSON *qs_reflect_to_json(const void *data, const Qs_TypeInfo *type);

/// Deserializes a cJSON object into a struct.  Fields absent from the JSON
/// are left untouched.  Returns true on success.
bool qs_reflect_from_json(void *data, const Qs_TypeInfo *type,
                          const struct cJSON *json);

#endif
