#include "qs_reflect.h"
#include "qs_log.h"
#include "cJSON.h"

#include <string.h>

/* ================================================================
   REGISTRY
   ================================================================ */

#define QS_MAX_TYPES 128

static struct {
    Qs_TypeInfo entries[QS_MAX_TYPES];
    uint32_t    count;
} s_registry;

const Qs_TypeInfo *qs_type_register(const Qs_TypeInfo *info)
{
    if (!info || !info->name) return NULL;

    for (uint32_t i = 0; i < s_registry.count; i++) {
        if (strcmp(s_registry.entries[i].name, info->name) == 0)
            return &s_registry.entries[i];
    }

    if (s_registry.count >= QS_MAX_TYPES) {
        QS_LOG_ERROR("Type registry full (%d)", QS_MAX_TYPES);
        return NULL;
    }

    Qs_TypeInfo *slot = &s_registry.entries[s_registry.count++];
    *slot = *info;
    QS_LOG_INFO("Reflect type '%s' registered (%u fields)",
                info->name, info->field_count);
    return slot;
}

const Qs_TypeInfo *qs_type_find(const char *name)
{
    if (!name) return NULL;
    for (uint32_t i = 0; i < s_registry.count; i++) {
        if (strcmp(s_registry.entries[i].name, name) == 0)
            return &s_registry.entries[i];
    }
    return NULL;
}

/* ================================================================
   SERIALIZATION — field → cJSON value
   ================================================================ */

static cJSON *serialize_field(const void *base, const Qs_FieldInfo *f)
{
    const uint8_t *ptr = (const uint8_t *)base + f->offset;

    switch (f->type) {
    case QS_FIELD_FLOAT:
        return cJSON_CreateNumber(*(const float *)ptr);

    case QS_FIELD_FLOAT2: {
        const float *v = (const float *)ptr;
        cJSON *arr = cJSON_CreateArray();
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(v[0]));
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(v[1]));
        return arr;
    }

    case QS_FIELD_FLOAT3: {
        const float *v = (const float *)ptr;
        cJSON *arr = cJSON_CreateArray();
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(v[0]));
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(v[1]));
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(v[2]));
        return arr;
    }

    case QS_FIELD_FLOAT4: {
        const float *v = (const float *)ptr;
        cJSON *arr = cJSON_CreateArray();
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(v[0]));
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(v[1]));
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(v[2]));
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(v[3]));
        return arr;
    }

    case QS_FIELD_INT32:
        return cJSON_CreateNumber(*(const int32_t *)ptr);

    case QS_FIELD_UINT32:
    case QS_FIELD_ENTITY:
        return cJSON_CreateNumber(*(const uint32_t *)ptr);

    case QS_FIELD_BOOL:
        return cJSON_CreateBool(*(const bool *)ptr);

    case QS_FIELD_STRING:
        return cJSON_CreateString((const char *)ptr);
    }

    return cJSON_CreateNull();
}

cJSON *qs_reflect_to_json(const void *data, const Qs_TypeInfo *type)
{
    if (!data || !type) return NULL;

    cJSON *obj = cJSON_CreateObject();
    for (uint32_t i = 0; i < type->field_count; i++) {
        cJSON *val = serialize_field(data, &type->fields[i]);
        if (val)
            cJSON_AddItemToObject(obj, type->fields[i].name, val);
    }
    return obj;
}

/* ================================================================
   DESERIALIZATION — cJSON value → field
   ================================================================ */

static bool deserialize_field(void *base, const Qs_FieldInfo *f,
                              const cJSON *val)
{
    uint8_t *ptr = (uint8_t *)base + f->offset;

    switch (f->type) {
    case QS_FIELD_FLOAT:
        if (!cJSON_IsNumber(val)) return false;
        *(float *)ptr = (float)val->valuedouble;
        return true;

    case QS_FIELD_FLOAT2:
    case QS_FIELD_FLOAT3:
    case QS_FIELD_FLOAT4: {
        if (!cJSON_IsArray(val)) return false;
        int count = (f->type == QS_FIELD_FLOAT2) ? 2 :
                    (f->type == QS_FIELD_FLOAT3) ? 3 : 4;
        float *v = (float *)ptr;
        int idx = 0;
        const cJSON *elem;
        cJSON_ArrayForEach(elem, val) {
            if (idx >= count) break;
            if (cJSON_IsNumber(elem))
                v[idx] = (float)elem->valuedouble;
            idx++;
        }
        return true;
    }

    case QS_FIELD_INT32:
        if (!cJSON_IsNumber(val)) return false;
        *(int32_t *)ptr = (int32_t)val->valuedouble;
        return true;

    case QS_FIELD_UINT32:
    case QS_FIELD_ENTITY:
        if (!cJSON_IsNumber(val)) return false;
        *(uint32_t *)ptr = (uint32_t)val->valuedouble;
        return true;

    case QS_FIELD_BOOL:
        if (!cJSON_IsBool(val)) return false;
        *(bool *)ptr = cJSON_IsTrue(val);
        return true;

    case QS_FIELD_STRING:
        if (!cJSON_IsString(val)) return false;
        snprintf((char *)ptr, f->size, "%s", val->valuestring);
        return true;
    }

    return false;
}

bool qs_reflect_from_json(void *data, const Qs_TypeInfo *type,
                          const cJSON *json)
{
    if (!data || !type || !json) return false;

    for (uint32_t i = 0; i < type->field_count; i++) {
        const cJSON *val = cJSON_GetObjectItemCaseSensitive(
            json, type->fields[i].name);
        if (val)
            deserialize_field(data, &type->fields[i], val);
    }
    return true;
}
