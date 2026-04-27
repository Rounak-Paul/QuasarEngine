#include "qs_scene.h"
#include "qs_reflect.h"
#include "qs_log.h"
#include "qs_system.h"
#include "qs_math.h"
#include "qs_asset_pack.h"
#include "qs_renderer.h"
#include "qs_mesh.h"
#include "qs_material.h"
#include "qs_project.h"
#include "quasar.h"
#include "cJSON.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _MSC_VER
#include <intrin.h>
#endif

/* ================================================================
   LIMITS
   ================================================================ */

#define QS_MAX_SCENES           32
#define QS_MAX_ENTITIES         4096
#define QS_MAX_COMPONENT_TYPES  64
#define QS_ENTITY_MASK_WORDS    ((QS_MAX_ENTITIES + 63) / 64)

/* ================================================================
   INTERNAL TYPES
   ================================================================ */

struct Qs_ComponentType {
    char     name[64];
    bool     in_use;
    uint32_t index;
    size_t   data_size;
    const Qs_TypeInfo *type_info;
    void (*init)(void *comp, Qs_Scene *scene, Qs_Entity entity);
    void (*destroy)(void *comp, Qs_Scene *scene, Qs_Entity entity);
    void (*update)(void *comp, Qs_Scene *scene, Qs_Entity entity, float dt);
};

typedef struct ComponentStore {
    uint8_t  *data;           /* dense-packed component data                   */
    uint32_t *sparse;         /* entity → dense index (UINT32_MAX = absent)    */
    uint32_t *dense;          /* dense index → entity ID                       */
    uint32_t  count;          /* number of live components in dense array      */
} ComponentStore;

static inline bool store_has(const ComponentStore *store, uint32_t entity)
{
    if (!store->sparse) return false;
    uint32_t idx = store->sparse[entity];
    return idx < store->count && store->dense[idx] == entity;
}

static inline void *store_get(const ComponentStore *store,
                              uint32_t entity, size_t data_size)
{
    uint32_t idx = store->sparse[entity];
    return store->data + (size_t)idx * data_size;
}

struct Qs_Scene {
    char              name[64];
    char              source_path[512];   /* Absolute path to the .qscene/.qproto this was loaded from. Empty if never loaded. */
    bool              in_use;

    /* Entity slots */
    char              entity_names[QS_MAX_ENTITIES][32];
    uint64_t          alive[QS_ENTITY_MASK_WORDS];
    uint64_t          enabled[QS_ENTITY_MASK_WORDS];
    uint32_t          parent_entity[QS_MAX_ENTITIES];  /* QS_ENTITY_INVALID = root */
    uint32_t          entity_count;
    uint32_t          next_entity_id;  /* Auto-increment for Qs_IdComp */

    /* Component storage — one per registered type */
    ComponentStore    stores[QS_MAX_COMPONENT_TYPES];

    /* Callbacks */
    Qs_SceneCallback  on_activate;
    Qs_SceneCallback  on_deactivate;
    void             *user_data;
};

typedef struct Qs_SceneSystemData {
    Qs_Scene         *scenes[QS_MAX_SCENES];
    Qs_ComponentType  types[QS_MAX_COMPONENT_TYPES];
    uint32_t          type_count;
    Qs_Scene         *active_scene;
    Qs_Engine        *engine;
} Qs_SceneSystemData;

static Qs_SceneSystemData *g_scene_system;

/* ================================================================
   BITSET HELPERS
   ================================================================ */

static inline void bit_set(uint64_t *bits, uint32_t i)
{
    bits[i / 64] |= (1ULL << (i % 64));
}

static inline void bit_clear(uint64_t *bits, uint32_t i)
{
    bits[i / 64] &= ~(1ULL << (i % 64));
}

static inline bool bit_test(const uint64_t *bits, uint32_t i)
{
    return (bits[i / 64] & (1ULL << (i % 64))) != 0;
}

static uint32_t bit_next_set(const uint64_t *bits, uint32_t start)
{
    uint32_t word = start / 64;
    uint32_t bit  = start % 64;

    if (word >= QS_ENTITY_MASK_WORDS) return QS_MAX_ENTITIES;

    /* Check remaining bits in the starting word */
    uint64_t masked = bits[word] & (~0ULL << bit);
    if (masked) {
        unsigned long idx;
#ifdef _MSC_VER
        _BitScanForward64(&idx, masked);
#else
        idx = (unsigned long)__builtin_ctzll(masked);
#endif
        return word * 64 + idx;
    }

    for (word++; word < QS_ENTITY_MASK_WORDS; word++) {
        if (bits[word]) {
            unsigned long idx;
#ifdef _MSC_VER
            _BitScanForward64(&idx, bits[word]);
#else
            idx = (unsigned long)__builtin_ctzll(bits[word]);
#endif
            return word * 64 + idx;
        }
    }
    return QS_MAX_ENTITIES;
}

/* ================================================================
   COMPONENT TYPE REGISTRATION
   ================================================================ */

Qs_ComponentType *qs_component_register(Qs_Engine *engine,
                                         const Qs_ComponentTypeDesc *desc)
{
    (void)engine;
    if (!g_scene_system || !desc || !desc->name || desc->data_size == 0)
        return NULL;

    if (g_scene_system->type_count >= QS_MAX_COMPONENT_TYPES) {
        QS_LOG_ERROR("Component type limit reached (%d)", QS_MAX_COMPONENT_TYPES);
        return NULL;
    }

    /* Check for duplicate */
    for (uint32_t i = 0; i < QS_MAX_COMPONENT_TYPES; i++) {
        if (g_scene_system->types[i].in_use &&
            strcmp(g_scene_system->types[i].name, desc->name) == 0)
        {
            QS_LOG_WARN("Component type '%s' already registered", desc->name);
            return &g_scene_system->types[i];
        }
    }

    /* Find free slot */
    Qs_ComponentType *ct = NULL;
    for (uint32_t i = 0; i < QS_MAX_COMPONENT_TYPES; i++) {
        if (!g_scene_system->types[i].in_use) {
            ct = &g_scene_system->types[i];
            ct->index = i;
            break;
        }
    }
    if (!ct) return NULL;

    ct->in_use    = true;
    ct->data_size = desc->data_size;
    ct->type_info = desc->type_info;
    ct->init      = desc->init;
    ct->destroy   = desc->destroy;
    ct->update    = desc->update;
    snprintf(ct->name, sizeof(ct->name), "%s", desc->name);

    g_scene_system->type_count++;
    QS_LOG_INFO("Component type '%s' registered (size=%zu)", ct->name, ct->data_size);
    return ct;
}

Qs_ComponentType *qs_component_find(const char *name)
{
    if (!g_scene_system || !name) return NULL;
    for (uint32_t i = 0; i < QS_MAX_COMPONENT_TYPES; i++) {
        if (g_scene_system->types[i].in_use &&
            strcmp(g_scene_system->types[i].name, name) == 0)
            return &g_scene_system->types[i];
    }
    return NULL;
}

const Qs_TypeInfo *qs_component_type_info(const Qs_ComponentType *type)
{
    return type ? type->type_info : NULL;
}

const char *qs_component_type_name(const Qs_ComponentType *type)
{
    return type ? type->name : NULL;
}

uint32_t qs_component_type_count(void)
{
    return g_scene_system ? g_scene_system->type_count : 0;
}

Qs_ComponentType *qs_component_type_at(uint32_t index)
{
    if (!g_scene_system) return NULL;
    uint32_t found = 0;
    for (uint32_t i = 0; i < QS_MAX_COMPONENT_TYPES; i++) {
        if (g_scene_system->types[i].in_use) {
            if (found == index) return &g_scene_system->types[i];
            found++;
        }
    }
    return NULL;
}

/* ================================================================
   BUILT-IN COMPONENT INIT CALLBACKS
   ================================================================ */

static void transform_init(void *comp, Qs_Scene *scene, Qs_Entity entity)
{
    (void)scene; (void)entity;
    Qs_Transform *t = (Qs_Transform *)comp;
    t->rotation[3] = 1.0f;   /* w = 1 → identity quaternion */
    t->scale[0] = 1.0f;
    t->scale[1] = 1.0f;
    t->scale[2] = 1.0f;
}

static void mesh_comp_init(void *comp, Qs_Scene *scene, Qs_Entity entity)
{
    (void)scene; (void)entity;
    Qs_MeshComp *mc = (Qs_MeshComp *)comp;
    mc->visible = true;
}

static void light_comp_init(void *comp, Qs_Scene *scene, Qs_Entity entity)
{
    (void)scene; (void)entity;
    Qs_LightComp *lc = (Qs_LightComp *)comp;
    lc->type            = QS_LIGHT_DIRECTIONAL;
    lc->direction[0]    = -0.577f;
    lc->direction[1]    = -0.577f;
    lc->direction[2]    = -0.577f;
    lc->color[0]        = 1.0f;
    lc->color[1]        = 1.0f;
    lc->color[2]        = 1.0f;
    lc->intensity       = 1.0f;
    lc->range           = 0.0f;
    lc->inner_cone_deg  = 0.0f;
    lc->outer_cone_deg  = 30.0f;
    lc->cast_shadows    = true;
    lc->enabled         = true;
}

static void id_comp_init(void *comp, Qs_Scene *scene, Qs_Entity entity)
{
    (void)entity;
    Qs_IdComp *id = (Qs_IdComp *)comp;
    id->id = scene->next_entity_id++;
}

static void tag_comp_init(void *comp, Qs_Scene *scene, Qs_Entity entity)
{
    (void)scene; (void)entity;
    Qs_TagComp *tag = (Qs_TagComp *)comp;
    snprintf(tag->tag, sizeof(tag->tag), "Untagged");
}

static void prototype_comp_init(void *comp, Qs_Scene *scene, Qs_Entity entity)
{
    (void)scene; (void)entity;
    Qs_PrototypeComp *pc = (Qs_PrototypeComp *)comp;
    pc->inner       = NULL;
    pc->load_failed = false;
}

static void prototype_comp_destroy(void *comp, Qs_Scene *scene, Qs_Entity entity)
{
    (void)scene; (void)entity;
    Qs_PrototypeComp *pc = (Qs_PrototypeComp *)comp;
    if (pc->inner) {
        qs_scene_destroy(pc->inner);
        pc->inner = NULL;
    }
}

/* ================================================================
   BUILT-IN REFLECTION INFO
   ================================================================ */

static const Qs_FieldInfo s_transform_fields[] = {
    QS_FIELD(Qs_Transform, position, QS_FIELD_FLOAT3),
    QS_FIELD(Qs_Transform, rotation, QS_FIELD_FLOAT4),
    QS_FIELD(Qs_Transform, scale,    QS_FIELD_FLOAT3),
};

static const Qs_TypeInfo s_transform_type_info = {
    .name        = "Transform",
    .data_size   = sizeof(Qs_Transform),
    .fields      = s_transform_fields,
    .field_count = QS_COUNTOF(s_transform_fields),
};

static const Qs_FieldInfo s_mesh_comp_fields[] = {
    QS_FIELD(Qs_MeshComp, visible,       QS_FIELD_BOOL),
    QS_FIELD(Qs_MeshComp, mesh_path,     QS_FIELD_STRING),
    QS_FIELD(Qs_MeshComp, material_path, QS_FIELD_STRING),
};

static const Qs_TypeInfo s_mesh_comp_type_info = {
    .name        = "MeshComp",
    .data_size   = sizeof(Qs_MeshComp),
    .fields      = s_mesh_comp_fields,
    .field_count = QS_COUNTOF(s_mesh_comp_fields),
};

static const Qs_FieldInfo s_light_comp_fields[] = {
    QS_FIELD(Qs_LightComp, type,           QS_FIELD_UINT32),
    QS_FIELD(Qs_LightComp, direction,      QS_FIELD_FLOAT3),
    QS_FIELD(Qs_LightComp, color,          QS_FIELD_FLOAT3),
    QS_FIELD(Qs_LightComp, intensity,      QS_FIELD_FLOAT),
    QS_FIELD(Qs_LightComp, range,          QS_FIELD_FLOAT),
    QS_FIELD(Qs_LightComp, inner_cone_deg, QS_FIELD_FLOAT),
    QS_FIELD(Qs_LightComp, outer_cone_deg, QS_FIELD_FLOAT),
    QS_FIELD(Qs_LightComp, cast_shadows,   QS_FIELD_BOOL),
    QS_FIELD(Qs_LightComp, enabled,        QS_FIELD_BOOL),
};

static const Qs_TypeInfo s_light_comp_type_info = {
    .name        = "LightComp",
    .data_size   = sizeof(Qs_LightComp),
    .fields      = s_light_comp_fields,
    .field_count = QS_COUNTOF(s_light_comp_fields),
};

static const Qs_FieldInfo s_id_comp_fields[] = {
    QS_FIELD(Qs_IdComp, id, QS_FIELD_UINT32),
};

static const Qs_TypeInfo s_id_comp_type_info = {
    .name        = "IdComp",
    .data_size   = sizeof(Qs_IdComp),
    .fields      = s_id_comp_fields,
    .field_count = QS_COUNTOF(s_id_comp_fields),
};

static const Qs_FieldInfo s_tag_comp_fields[] = {
    QS_FIELD(Qs_TagComp, tag, QS_FIELD_STRING),
};

static const Qs_TypeInfo s_tag_comp_type_info = {
    .name        = "TagComp",
    .data_size   = sizeof(Qs_TagComp),
    .fields      = s_tag_comp_fields,
    .field_count = QS_COUNTOF(s_tag_comp_fields),
};

static const Qs_FieldInfo s_prototype_comp_fields[] = {
    QS_FIELD(Qs_PrototypeComp, path, QS_FIELD_STRING),
};

static const Qs_TypeInfo s_prototype_comp_type_info = {
    .name        = "Prototype",
    .data_size   = sizeof(Qs_PrototypeComp),
    .fields      = s_prototype_comp_fields,
    .field_count = QS_COUNTOF(s_prototype_comp_fields),
};

/* ================================================================
   BUILT-IN TYPE HANDLES
   ================================================================ */

static Qs_ComponentType *s_transform_type;
static Qs_ComponentType *s_mesh_comp_type;
static Qs_ComponentType *s_light_comp_type;
static Qs_ComponentType *s_id_comp_type;
static Qs_ComponentType *s_tag_comp_type;
static Qs_ComponentType *s_prototype_comp_type;

Qs_ComponentType *qs_transform_type(void)  { return s_transform_type; }
Qs_ComponentType *qs_mesh_comp_type(void)  { return s_mesh_comp_type; }
Qs_ComponentType *qs_light_comp_type(void) { return s_light_comp_type; }
Qs_ComponentType *qs_id_comp_type(void)    { return s_id_comp_type; }
Qs_ComponentType *qs_tag_comp_type(void)   { return s_tag_comp_type; }
Qs_ComponentType *qs_prototype_comp_type(void) { return s_prototype_comp_type; }

static void register_builtin_types(Qs_Engine *engine)
{
    /* Register reflection type infos */
    qs_type_register(&s_transform_type_info);
    qs_type_register(&s_mesh_comp_type_info);
    qs_type_register(&s_light_comp_type_info);
    qs_type_register(&s_id_comp_type_info);
    qs_type_register(&s_tag_comp_type_info);
    qs_type_register(&s_prototype_comp_type_info);

    s_id_comp_type = qs_component_register(engine, &(Qs_ComponentTypeDesc){
        .name      = "IdComp",
        .data_size = sizeof(Qs_IdComp),
        .type_info = &s_id_comp_type_info,
        .init      = id_comp_init,
    });

    s_tag_comp_type = qs_component_register(engine, &(Qs_ComponentTypeDesc){
        .name      = "TagComp",
        .data_size = sizeof(Qs_TagComp),
        .type_info = &s_tag_comp_type_info,
        .init      = tag_comp_init,
    });

    s_transform_type = qs_component_register(engine, &(Qs_ComponentTypeDesc){
        .name      = "Transform",
        .data_size = sizeof(Qs_Transform),
        .type_info = &s_transform_type_info,
        .init      = transform_init,
    });

    s_mesh_comp_type = qs_component_register(engine, &(Qs_ComponentTypeDesc){
        .name      = "MeshComp",
        .data_size = sizeof(Qs_MeshComp),
        .type_info = &s_mesh_comp_type_info,
        .init      = mesh_comp_init,
    });

    s_light_comp_type = qs_component_register(engine, &(Qs_ComponentTypeDesc){
        .name      = "LightComp",
        .data_size = sizeof(Qs_LightComp),
        .type_info = &s_light_comp_type_info,
        .init      = light_comp_init,
    });

    s_prototype_comp_type = qs_component_register(engine, &(Qs_ComponentTypeDesc){
        .name      = "Prototype",
        .data_size = sizeof(Qs_PrototypeComp),
        .type_info = &s_prototype_comp_type_info,
        .init      = prototype_comp_init,
        .destroy   = prototype_comp_destroy,
    });
}

/* ================================================================
   SCENE LIFECYCLE
   ================================================================ */

Qs_Scene *qs_scene_create(Qs_Engine *engine, const Qs_SceneDesc *desc)
{
    (void)engine;
    if (!g_scene_system || !desc) return NULL;

    /* Find free slot */
    uint32_t slot = UINT32_MAX;
    for (uint32_t i = 0; i < QS_MAX_SCENES; i++) {
        if (!g_scene_system->scenes[i]) {
            slot = i;
            break;
        }
    }
    if (slot == UINT32_MAX) {
        QS_LOG_ERROR("Scene limit reached (%d)", QS_MAX_SCENES);
        return NULL;
    }

    Qs_Scene *scene = (Qs_Scene *)calloc(1, sizeof(Qs_Scene));
    if (!scene) return NULL;

    scene->in_use       = true;
    scene->on_activate   = desc->on_activate;
    scene->on_deactivate = desc->on_deactivate;
    scene->user_data     = desc->user_data;

    memset(scene->parent_entity, 0xFF, sizeof(scene->parent_entity));

    if (desc->name)
        snprintf(scene->name, sizeof(scene->name), "%s", desc->name);
    else
        snprintf(scene->name, sizeof(scene->name), "scene_%u", slot);

    g_scene_system->scenes[slot] = scene;
    QS_LOG_INFO("Scene '%s' created", scene->name);
    return scene;
}

void qs_scene_destroy(Qs_Scene *scene)
{
    if (!scene || !scene->in_use || !g_scene_system) return;

    /* Deactivate if active */
    if (g_scene_system->active_scene == scene)
        qs_scene_set_active(NULL);

    /* Destroy all alive entities (calls component destroy callbacks) */
    for (uint32_t e = bit_next_set(scene->alive, 0);
         e < QS_MAX_ENTITIES;
         e = bit_next_set(scene->alive, e + 1))
    {
        qs_entity_destroy(scene, e);
    }

    /* Free component store buffers */
    for (uint32_t t = 0; t < QS_MAX_COMPONENT_TYPES; t++) {
        free(scene->stores[t].data);
        free(scene->stores[t].sparse);
        free(scene->stores[t].dense);
    }

    /* Remove from system array */
    for (uint32_t i = 0; i < QS_MAX_SCENES; i++) {
        if (g_scene_system->scenes[i] == scene) {
            g_scene_system->scenes[i] = NULL;
            break;
        }
    }

    QS_LOG_INFO("Scene '%s' destroyed", scene->name);
    free(scene);
}

const char *qs_scene_name(const Qs_Scene *scene)
{
    return scene ? scene->name : NULL;
}

void qs_scene_set_active(Qs_Scene *scene)
{
    if (!g_scene_system) return;

    Qs_Scene *prev = g_scene_system->active_scene;
    if (prev == scene) return;

    if (prev && prev->on_deactivate)
        prev->on_deactivate(prev, prev->user_data);

    g_scene_system->active_scene = scene;

    if (scene && scene->on_activate)
        scene->on_activate(scene, scene->user_data);

    QS_LOG_INFO("Active scene: %s", scene ? scene->name : "(none)");
}

Qs_Scene *qs_scene_active(void)
{
    return g_scene_system ? g_scene_system->active_scene : NULL;
}

/* ================================================================
   ENTITY LIFECYCLE
   ================================================================ */

Qs_Entity qs_entity_create(Qs_Scene *scene, const char *name)
{
    if (!scene || !scene->in_use) return QS_ENTITY_INVALID;

    /* Find free slot via inverted alive bitset */
    uint32_t e = QS_MAX_ENTITIES;
    for (uint32_t word = 0; word < QS_ENTITY_MASK_WORDS; word++) {
        uint64_t free_bits = ~scene->alive[word];
        if (free_bits) {
            unsigned long idx;
#ifdef _MSC_VER
            _BitScanForward64(&idx, free_bits);
#else
            idx = (unsigned long)__builtin_ctzll(free_bits);
#endif
            e = word * 64 + idx;
            break;
        }
    }
    if (e >= QS_MAX_ENTITIES) {
        QS_LOG_ERROR("Entity limit reached in '%s' (%d)",
                     scene->name, QS_MAX_ENTITIES);
        return QS_ENTITY_INVALID;
    }

    bit_set(scene->alive, e);
    bit_set(scene->enabled, e);
    scene->entity_count++;

    if (name)
        snprintf(scene->entity_names[e], 32, "%s", name);
    else
        snprintf(scene->entity_names[e], 32, "entity_%u", e);

    /* Auto-add default components: Id, Tag, Transform */
    if (s_id_comp_type)
        qs_entity_add(scene, e, s_id_comp_type);
    if (s_tag_comp_type)
        qs_entity_add(scene, e, s_tag_comp_type);
    if (s_transform_type)
        qs_entity_add(scene, e, s_transform_type);

    return e;
}

void qs_entity_destroy(Qs_Scene *scene, Qs_Entity entity)
{
    if (!scene || entity >= QS_MAX_ENTITIES ||
        !bit_test(scene->alive, entity))
        return;

    /* Remove all components */
    for (uint32_t t = 0; t < QS_MAX_COMPONENT_TYPES; t++) {
        if (g_scene_system->types[t].in_use &&
            store_has(&scene->stores[t], entity))
        {
            qs_entity_remove(scene, entity, &g_scene_system->types[t]);
        }
    }

    bit_clear(scene->alive, entity);
    bit_clear(scene->enabled, entity);
    scene->entity_names[entity][0] = '\0';
    scene->parent_entity[entity]   = QS_ENTITY_INVALID;
    if (scene->entity_count > 0) scene->entity_count--;
}

bool qs_entity_valid(const Qs_Scene *scene, Qs_Entity entity)
{
    if (!scene || entity >= QS_MAX_ENTITIES) return false;
    return bit_test(scene->alive, entity);
}

const char *qs_entity_name(const Qs_Scene *scene, Qs_Entity entity)
{
    if (!scene || entity >= QS_MAX_ENTITIES ||
        !bit_test(scene->alive, entity))
        return NULL;
    return scene->entity_names[entity];
}

void qs_entity_set_name(Qs_Scene *scene, Qs_Entity entity, const char *name)
{
    if (!scene || entity >= QS_MAX_ENTITIES ||
        !bit_test(scene->alive, entity))
        return;
    snprintf(scene->entity_names[entity],
             sizeof(scene->entity_names[entity]),
             "%s", name ? name : "");
}

void qs_entity_set_enabled(Qs_Scene *scene, Qs_Entity entity, bool enabled)
{
    if (!scene || entity >= QS_MAX_ENTITIES ||
        !bit_test(scene->alive, entity))
        return;

    if (enabled)
        bit_set(scene->enabled, entity);
    else
        bit_clear(scene->enabled, entity);
}

bool qs_entity_enabled(const Qs_Scene *scene, Qs_Entity entity)
{
    if (!scene || entity >= QS_MAX_ENTITIES) return false;
    return bit_test(scene->enabled, entity);
}

void qs_entity_set_parent(Qs_Scene *scene, Qs_Entity entity, Qs_Entity parent)
{
    if (!scene || entity >= QS_MAX_ENTITIES) return;
    scene->parent_entity[entity] = parent;
}

Qs_Entity qs_entity_get_parent(const Qs_Scene *scene, Qs_Entity entity)
{
    if (!scene || entity >= QS_MAX_ENTITIES) return QS_ENTITY_INVALID;
    return scene->parent_entity[entity];
}

uint32_t qs_scene_entity_count(const Qs_Scene *scene)
{
    return scene ? scene->entity_count : 0;
}

/* ================================================================
   COMPONENT CRUD
   ================================================================ */

void *qs_entity_add(Qs_Scene *scene, Qs_Entity entity,
                     Qs_ComponentType *type)
{
    if (!scene || !type || entity >= QS_MAX_ENTITIES ||
        !bit_test(scene->alive, entity) || !type->in_use)
        return NULL;

    ComponentStore *store = &scene->stores[type->index];

    /* Already has component? */
    if (store_has(store, entity))
        return NULL;

    /* Lazy-allocate dense arrays for this component type in this scene */
    if (!store->data) {
        store->data   = (uint8_t *)calloc(QS_MAX_ENTITIES, type->data_size);
        store->sparse = (uint32_t *)malloc(QS_MAX_ENTITIES * sizeof(uint32_t));
        store->dense  = (uint32_t *)malloc(QS_MAX_ENTITIES * sizeof(uint32_t));
        if (!store->data || !store->sparse || !store->dense) {
            free(store->data); free(store->sparse); free(store->dense);
            store->data = NULL; store->sparse = NULL; store->dense = NULL;
            return NULL;
        }
        memset(store->sparse, 0xFF, QS_MAX_ENTITIES * sizeof(uint32_t));
        store->count = 0;
    }

    /* Append to dense arrays */
    uint32_t idx = store->count++;
    store->sparse[entity] = idx;
    store->dense[idx]     = entity;

    void *comp = store->data + (size_t)idx * type->data_size;
    memset(comp, 0, type->data_size);

    if (type->init)
        type->init(comp, scene, entity);

    return comp;
}

void *qs_entity_get(const Qs_Scene *scene, Qs_Entity entity,
                     const Qs_ComponentType *type)
{
    if (!scene || !type || entity >= QS_MAX_ENTITIES ||
        !type->in_use)
        return NULL;

    const ComponentStore *store = &scene->stores[type->index];
    if (!store_has(store, entity)) return NULL;

    return store_get(store, entity, type->data_size);
}

void qs_entity_remove(Qs_Scene *scene, Qs_Entity entity,
                       Qs_ComponentType *type)
{
    if (!scene || !type || entity >= QS_MAX_ENTITIES ||
        !type->in_use)
        return;

    ComponentStore *store = &scene->stores[type->index];
    if (!store_has(store, entity)) return;

    uint32_t idx = store->sparse[entity];
    void *comp = store->data + (size_t)idx * type->data_size;
    if (type->destroy)
        type->destroy(comp, scene, entity);

    /* Swap-remove: move last element into the vacated slot */
    uint32_t last = store->count - 1;
    if (idx != last) {
        uint32_t last_entity = store->dense[last];
        memcpy(store->data + (size_t)idx * type->data_size,
               store->data + (size_t)last * type->data_size,
               type->data_size);
        store->dense[idx]          = last_entity;
        store->sparse[last_entity] = idx;
    }

    store->sparse[entity] = UINT32_MAX;
    store->count--;
}

bool qs_entity_has(const Qs_Scene *scene, Qs_Entity entity,
                    const Qs_ComponentType *type)
{
    if (!scene || !type || entity >= QS_MAX_ENTITIES || !type->in_use)
        return false;
    return store_has(&scene->stores[type->index], entity);
}

/* ================================================================
   ITERATION
   ================================================================ */

Qs_Entity qs_scene_first(const Qs_Scene *scene,
                          const Qs_ComponentType *type)
{
    if (!scene || !type || !type->in_use) return QS_ENTITY_INVALID;

    const ComponentStore *store = &scene->stores[type->index];
    return store->count > 0 ? store->dense[0] : QS_ENTITY_INVALID;
}

Qs_Entity qs_scene_next(const Qs_Scene *scene,
                         const Qs_ComponentType *type,
                         Qs_Entity after)
{
    if (!scene || !type || !type->in_use || after >= QS_MAX_ENTITIES)
        return QS_ENTITY_INVALID;

    const ComponentStore *store = &scene->stores[type->index];
    if (!store->sparse) return QS_ENTITY_INVALID;
    uint32_t idx = store->sparse[after];
    if (idx >= store->count || store->dense[idx] != after)
        return QS_ENTITY_INVALID;
    uint32_t next = idx + 1;
    return next < store->count ? store->dense[next] : QS_ENTITY_INVALID;
}

/* ================================================================
   SCENE SERIALIZATION
   ================================================================ */

cJSON *qs_scene_to_json(const Qs_Scene *scene)
{
    if (!scene || !g_scene_system) return NULL;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", scene->name);
    cJSON_AddNumberToObject(root, "next_entity_id",
                            (double)scene->next_entity_id);

    cJSON *entities = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "entities", entities);

    /* Build entity → array-index map for parent indices */
    int entity_to_index[QS_MAX_ENTITIES];
    for (int i = 0; i < QS_MAX_ENTITIES; i++) entity_to_index[i] = -1;
    int idx = 0;
    for (uint32_t e = bit_next_set(scene->alive, 0);
         e < QS_MAX_ENTITIES;
         e = bit_next_set(scene->alive, e + 1))
    {
        entity_to_index[e] = idx++;
    }

    for (uint32_t e = bit_next_set(scene->alive, 0);
         e < QS_MAX_ENTITIES;
         e = bit_next_set(scene->alive, e + 1))
    {
        cJSON *ent = cJSON_CreateObject();
        cJSON_AddStringToObject(ent, "name", scene->entity_names[e]);
        cJSON_AddBoolToObject(ent, "enabled", bit_test(scene->enabled, e));

        Qs_Entity p = scene->parent_entity[e];
        int parent_idx = (p < QS_MAX_ENTITIES) ? entity_to_index[p] : -1;
        cJSON_AddNumberToObject(ent, "parent", (double)parent_idx);

        cJSON *comps = cJSON_CreateObject();
        cJSON_AddItemToObject(ent, "components", comps);

        for (uint32_t t = 0; t < QS_MAX_COMPONENT_TYPES; t++) {
            Qs_ComponentType *type = &g_scene_system->types[t];
            if (!type->in_use) continue;
            if (!store_has(&scene->stores[t], e)) continue;

            if (type->type_info) {
                void *comp = store_get(&scene->stores[t], e,
                                       type->data_size);
                cJSON *comp_json = qs_reflect_to_json(comp, type->type_info);
                if (comp_json)
                    cJSON_AddItemToObject(comps, type->name, comp_json);
            } else {
                cJSON_AddItemToObject(comps, type->name,
                                      cJSON_CreateObject());
            }
        }

        cJSON_AddItemToArray(entities, ent);
    }

    return root;
}

bool qs_scene_from_json(Qs_Scene *scene, Qs_Engine *engine,
                        const cJSON *json)
{
    (void)engine;
    if (!scene || !json || !g_scene_system) return false;

    const cJSON *entities = cJSON_GetObjectItemCaseSensitive(json, "entities");
    if (!cJSON_IsArray(entities)) return false;

    /* Restore next_entity_id counter */
    const cJSON *next_id_json =
        cJSON_GetObjectItemCaseSensitive(json, "next_entity_id");
    if (cJSON_IsNumber(next_id_json))
        scene->next_entity_id = (uint32_t)next_id_json->valueint;

    /* Pass 1: create all entities and remember array-index → entity map */
    int total = cJSON_GetArraySize(entities);
    Qs_Entity *idx_to_entity = NULL;
    if (total > 0) {
        idx_to_entity = (Qs_Entity *)malloc(sizeof(Qs_Entity) * (size_t)total);
        for (int i = 0; i < total; i++) idx_to_entity[i] = QS_ENTITY_INVALID;
    }

    int i = 0;
    const cJSON *ent_json;
    cJSON_ArrayForEach(ent_json, entities) {
        const cJSON *name_val =
            cJSON_GetObjectItemCaseSensitive(ent_json, "name");
        const char *name =
            cJSON_IsString(name_val) ? name_val->valuestring : NULL;

        Qs_Entity entity = qs_entity_create(scene, name);
        if (idx_to_entity) idx_to_entity[i] = entity;
        i++;
        if (entity == QS_ENTITY_INVALID) continue;

        const cJSON *enabled_val =
            cJSON_GetObjectItemCaseSensitive(ent_json, "enabled");
        if (cJSON_IsBool(enabled_val))
            qs_entity_set_enabled(scene, entity, cJSON_IsTrue(enabled_val));

        const cJSON *comps =
            cJSON_GetObjectItemCaseSensitive(ent_json, "components");
        if (!cJSON_IsObject(comps)) continue;

        const cJSON *comp_json;
        cJSON_ArrayForEach(comp_json, comps) {
            const char *type_name = comp_json->string;
            Qs_ComponentType *type = qs_component_find(type_name);
            if (!type) {
                QS_LOG_WARN("Unknown component type '%s' during deserialization",
                            type_name);
                continue;
            }

            /* Transform is auto-added by qs_entity_create */
            void *comp = qs_entity_get(scene, entity, type);
            if (!comp)
                comp = qs_entity_add(scene, entity, type);
            if (!comp) continue;

            if (type->type_info)
                qs_reflect_from_json(comp, type->type_info, comp_json);
        }
    }

    /* Pass 2: assign parents from "parent" indices */
    if (idx_to_entity) {
        i = 0;
        cJSON_ArrayForEach(ent_json, entities) {
            Qs_Entity child = idx_to_entity[i++];
            if (child == QS_ENTITY_INVALID) continue;
            const cJSON *parent_val =
                cJSON_GetObjectItemCaseSensitive(ent_json, "parent");
            if (cJSON_IsNumber(parent_val)) {
                int pi = parent_val->valueint;
                if (pi >= 0 && pi < total &&
                    idx_to_entity[pi] != QS_ENTITY_INVALID)
                {
                    qs_entity_set_parent(scene, child, idx_to_entity[pi]);
                }
            }
        }
        free(idx_to_entity);
    }

    /* Sync next_entity_id past the highest loaded ID */
    if (s_id_comp_type) {
        for (Qs_Entity e = qs_scene_first(scene, s_id_comp_type);
             e != QS_ENTITY_INVALID;
             e = qs_scene_next(scene, s_id_comp_type, e))
        {
            Qs_IdComp *id = (Qs_IdComp *)qs_entity_get(
                                scene, e, s_id_comp_type);
            if (id && id->id >= scene->next_entity_id)
                scene->next_entity_id = id->id + 1;
        }
    }

    return true;
}

/* ================================================================
   SYSTEM CALLBACKS
   ================================================================ */

static bool scene_system_init(Qs_System *system, Qs_Engine *engine)
{
    Qs_SceneSystemData *data = (Qs_SceneSystemData *)qs_system_data(system);
    g_scene_system = data;
    data->engine = engine;

    register_builtin_types(engine);

    QS_LOG_INFO("Scene system initialized");
    return true;
}

static void scene_system_shutdown(Qs_System *system, Qs_Engine *engine)
{
    (void)engine;
    Qs_SceneSystemData *data = (Qs_SceneSystemData *)qs_system_data(system);

    /* Deactivate */
    if (data->active_scene)
        qs_scene_set_active(NULL);

    /* Destroy all scenes */
    for (uint32_t i = 0; i < QS_MAX_SCENES; i++) {
        if (data->scenes[i])
            qs_scene_destroy(data->scenes[i]);
    }

    g_scene_system    = NULL;
    s_transform_type  = NULL;
    s_mesh_comp_type  = NULL;
    s_light_comp_type = NULL;
    s_id_comp_type    = NULL;
    s_tag_comp_type   = NULL;
    s_prototype_comp_type = NULL;

    QS_LOG_INFO("Scene system shut down");
}

static void scene_system_update(Qs_System *system, Qs_Engine *engine, float dt)
{
    (void)engine;
    Qs_SceneSystemData *data = (Qs_SceneSystemData *)qs_system_data(system);

    Qs_Scene *scene = data->active_scene;
    if (!scene) return;

    /* Iterate all component types with update callbacks */
    for (uint32_t t = 0; t < QS_MAX_COMPONENT_TYPES; t++) {
        Qs_ComponentType *type = &data->types[t];
        if (!type->in_use || !type->update) continue;

        ComponentStore *store = &scene->stores[t];
        if (!store->data || store->count == 0) continue;

        /* Dense iteration — components are contiguous in memory */
        for (uint32_t i = 0; i < store->count; i++) {
            uint32_t e = store->dense[i];

            /* Skip disabled entities */
            if (!bit_test(scene->enabled, e)) continue;

            void *comp = store->data + (size_t)i * type->data_size;
            type->update(comp, scene, e, dt);
        }
    }
}

Qs_SystemDesc qs_scene_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Scene",
        .data_size = sizeof(Qs_SceneSystemData),
        .init      = scene_system_init,
        .shutdown  = scene_system_shutdown,
        .update    = scene_system_update,
    };
}

/* ================================================================
   SCENE FILE I/O
   ================================================================ */

bool qs_scene_save(const Qs_Scene *scene, const char *path)
{
    if (!scene || !path) return false;

    cJSON *json = qs_scene_to_json(scene);
    if (!json) return false;

    char *str = cJSON_Print(json);
    cJSON_Delete(json);
    if (!str) return false;

    FILE *f = fopen(path, "w");
    if (!f) {
        QS_LOG_ERROR("Failed to write scene file: %s", path);
        free(str);
        return false;
    }
    fputs(str, f);
    fclose(f);
    free(str);

    /* Record source path so subsequent saves & path resolution work. */
    snprintf(((Qs_Scene *)scene)->source_path,
             sizeof(((Qs_Scene *)scene)->source_path), "%s", path);

    QS_LOG_INFO("Scene saved: %s", path);
    return true;
}

bool qs_scene_load(Qs_Scene *scene, Qs_Engine *engine, const char *path)
{
    if (!scene || !engine || !path) return false;

    FILE *f = fopen(path, "r");
    if (!f) {
        QS_LOG_ERROR("Failed to open scene file: %s", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return false; }

    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return false; }
    size_t nread = fread(buf, 1, (size_t)len, f);
    buf[nread] = '\0';
    fclose(f);

    cJSON *json = cJSON_Parse(buf);
    free(buf);
    if (!json) {
        QS_LOG_ERROR("Failed to parse scene file: %s", path);
        return false;
    }

    /* Record source path BEFORE from_json so path resolution can use it. */
    snprintf(scene->source_path, sizeof(scene->source_path), "%s", path);

    bool ok = qs_scene_from_json(scene, engine, json);
    cJSON_Delete(json);

    /* Forward declaration: defined later in this file. */
    void qs_scene_resolve_assets(Qs_Scene *scene, Qs_Engine *engine);

    if (ok) {
        qs_scene_resolve_assets(scene, engine);
        QS_LOG_INFO("Scene loaded: %s", path);
    } else {
        QS_LOG_ERROR("Failed to deserialize scene: %s", path);
    }

    return ok;
}

/* ================================================================
   WORLD TRANSFORM
   ================================================================ */

void qs_scene_world_matrix(const Qs_Scene *scene, Qs_Entity entity,
                           float out[16])
{
    qs_m4_identity(out);
    if (!scene || entity >= QS_MAX_ENTITIES) return;

    /* Walk up the parent chain and collect ancestor list (leaf → root). */
    Qs_Entity chain[64];
    int depth = 0;
    for (Qs_Entity e = entity;
         e != QS_ENTITY_INVALID && depth < 64;
         e = scene->parent_entity[e])
    {
        chain[depth++] = e;
    }

    /* Multiply from root down to leaf: world = root * ... * parent * local */
    for (int i = depth - 1; i >= 0; i--) {
        const Qs_Transform *t = (const Qs_Transform *)qs_entity_get(
            scene, chain[i], s_transform_type);
        if (!t) continue;
        float local[16];
        qs_m4_from_trs(local, t->position, t->rotation, t->scale);
        float tmp[16];
        qs_m4_mul(out, local, tmp);
        memcpy(out, tmp, sizeof(float) * 16);
    }
}

/* ================================================================
   ASSET RESOLUTION + RENDERABLE SUBMISSION
   ================================================================ */

static void scene_dir(const Qs_Scene *scene, char *out, size_t out_size)
{
    const char *p = scene->source_path;
    const char *last = NULL;
    for (const char *c = p; *c; c++)
        if (*c == '/' || *c == '\\') last = c;
    if (!last) {
        snprintf(out, out_size, ".");
        return;
    }
    size_t len = (size_t)(last - p);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, p, len);
    out[len] = '\0';
}

static bool path_is_abs_(const char *p)
{
    if (!p || !*p) return false;
    if (p[0] == '/' || p[0] == '\\') return true;
#ifdef _WIN32
    if (p[1] == ':') return true;
#endif
    return false;
}

static void resolve_path(const Qs_Scene *scene, const char *rel,
                         char *abs, size_t abs_size)
{
    if (!rel || !*rel) { abs[0] = '\0'; return; }
    if (path_is_abs_(rel)) {
        snprintf(abs, abs_size, "%s", rel);
        return;
    }

    /* Prefer project-relative resolution: paths registered in the asset
       DB (e.g. "Assets/Foo/Foo.qproto") are stored relative to the
       project root.  Fall back to scene-dir for legacy scenes that
       reference siblings without an Assets/ prefix.  */
    Qs_Engine *engine = g_scene_system ? g_scene_system->engine : NULL;
    Qs_Project *project = engine ? qs_engine_project(engine) : NULL;
    if (project) {
        const char *proot = qs_project_path(project);
        if (proot && proot[0]) {
            char candidate[1024];
            snprintf(candidate, sizeof(candidate), "%s/%s", proot, rel);
            FILE *f = fopen(candidate, "rb");
            if (f) {
                fclose(f);
                snprintf(abs, abs_size, "%s", candidate);
                return;
            }
        }
    }

    char dir[512];
    scene_dir(scene, dir, sizeof(dir));
    snprintf(abs, abs_size, "%s/%s", dir, rel);
}

/// Walk every MeshComp entity in `scene` and resolve mesh_path / material_path
/// against the scene's source directory through the runtime asset cache.
/// Called automatically by qs_scene_load.  PrototypeComp entries are
/// resolved lazily by qs_scene_submit_renderables on first use.
void qs_scene_resolve_assets(Qs_Scene *scene, Qs_Engine *engine)
{
    if (!scene || !engine || !s_mesh_comp_type) return;

    for (Qs_Entity e = qs_scene_first(scene, s_mesh_comp_type);
         e != QS_ENTITY_INVALID;
         e = qs_scene_next(scene, s_mesh_comp_type, e))
    {
        Qs_MeshComp *mc = (Qs_MeshComp *)qs_entity_get(scene, e, s_mesh_comp_type);
        if (!mc) continue;

        if (!mc->mesh && mc->mesh_path[0]) {
            char abs[1024];
            resolve_path(scene, mc->mesh_path, abs, sizeof(abs));
            mc->mesh = qs_asset_cache_mesh(engine, abs);
            if (!mc->mesh)
                QS_LOG_WARN("Scene: failed to load mesh '%s'", abs);
        }
        if (!mc->material && mc->material_path[0]) {
            char abs[1024];
            resolve_path(scene, mc->material_path, abs, sizeof(abs));
            mc->material = qs_asset_cache_material(engine, abs);
            if (!mc->material)
                QS_LOG_WARN("Scene: failed to load material '%s'", abs);
        }
    }
}

void qs_scene_submit_renderables(Qs_Scene *scene,
                                 Qs_Engine *engine,
                                 Qs_Renderer *renderer,
                                 const float parent_world[16])
{
    if (!scene || !renderer) return;

    float identity[16];
    if (!parent_world) {
        qs_m4_identity(identity);
        parent_world = identity;
    }

    /* Mesh components */
    if (s_mesh_comp_type) {
        for (Qs_Entity e = qs_scene_first(scene, s_mesh_comp_type);
             e != QS_ENTITY_INVALID;
             e = qs_scene_next(scene, s_mesh_comp_type, e))
        {
            Qs_MeshComp *mc = (Qs_MeshComp *)qs_entity_get(scene, e, s_mesh_comp_type);
            if (!mc || !mc->visible || !mc->mesh || !mc->material) continue;

            float local_world[16];
            qs_scene_world_matrix(scene, e, local_world);
            float world[16];
            qs_m4_mul(parent_world, local_world, world);

            Qs_RenderableDesc r;
            r.mesh            = mc->mesh;
            r.material        = mc->material;
            r.entity          = e;
            r.cast_shadows    = true;
            r.receive_shadows = true;
            r.bounds.min[0] = r.bounds.min[1] = r.bounds.min[2] = -100.0f;
            r.bounds.max[0] = r.bounds.max[1] = r.bounds.max[2] =  100.0f;
            memcpy(r.transform, world, sizeof(world));
            qs_renderer_submit_renderable(renderer, &r);
        }
    }

    /* Light components — only submit at the top level (parent is identity)
       to avoid duplicating lights from every prototype instance. */
    if (s_light_comp_type) {
        bool top_level = true;
        for (int i = 0; i < 16 && top_level; i++) {
            float v = parent_world[i];
            float ref = (i % 5 == 0) ? 1.0f : 0.0f;
            if (v != ref) top_level = false;
        }
        if (top_level) {
            for (Qs_Entity e = qs_scene_first(scene, s_light_comp_type);
                 e != QS_ENTITY_INVALID;
                 e = qs_scene_next(scene, s_light_comp_type, e))
            {
                Qs_LightComp *lc = (Qs_LightComp *)qs_entity_get(scene, e, s_light_comp_type);
                if (lc) qs_renderer_submit_light_comp(renderer, lc);
            }
        }
    }

    /* Prototype components — recurse into nested scenes */
    if (s_prototype_comp_type && engine) {
        for (Qs_Entity e = qs_scene_first(scene, s_prototype_comp_type);
             e != QS_ENTITY_INVALID;
             e = qs_scene_next(scene, s_prototype_comp_type, e))
        {
            Qs_PrototypeComp *pc =
                (Qs_PrototypeComp *)qs_entity_get(scene, e, s_prototype_comp_type);
            if (!pc || pc->load_failed || !pc->path[0]) continue;

            /* Lazy-load the inner scene on first use. */
            if (!pc->inner) {
                char abs[1024];
                resolve_path(scene, pc->path, abs, sizeof(abs));

                /* Use the file basename (without extension) as the inner
                   scene's name so logs read "Scene 'ABeautifulGame'…"
                   rather than the full project-relative path. */
                const char *base = strrchr(pc->path, '/');
                base = base ? base + 1 : pc->path;
                char inner_name[64];
                snprintf(inner_name, sizeof(inner_name), "%s", base);
                char *dot = strrchr(inner_name, '.');
                if (dot) *dot = '\0';

                Qs_Scene *inner = qs_scene_create(engine, &(Qs_SceneDesc){
                    .name = inner_name,
                });
                if (!inner) { pc->load_failed = true; continue; }
                if (!qs_scene_load(inner, engine, abs)) {
                    qs_scene_destroy(inner);
                    pc->load_failed = true;
                    continue;
                }
                pc->inner = inner;
            }

            float local_world[16];
            qs_scene_world_matrix(scene, e, local_world);
            float world[16];
            qs_m4_mul(parent_world, local_world, world);

            qs_scene_submit_renderables(pc->inner, engine, renderer, world);
        }
    }
}
