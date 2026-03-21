#include "qs_scene.h"
#include "qs_log.h"
#include "qs_system.h"

#include <stdlib.h>
#include <string.h>

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
    void (*init)(void *comp, Qs_Scene *scene, Qs_Entity entity);
    void (*destroy)(void *comp, Qs_Scene *scene, Qs_Entity entity);
    void (*update)(void *comp, Qs_Scene *scene, Qs_Entity entity, float dt);
};

typedef struct ComponentStore {
    uint8_t  *data;                           /* lazily allocated */
    uint64_t  mask[QS_ENTITY_MASK_WORDS];
} ComponentStore;

struct Qs_Scene {
    char              name[64];
    bool              in_use;

    /* Entity slots */
    char              entity_names[QS_MAX_ENTITIES][32];
    uint64_t          alive[QS_ENTITY_MASK_WORDS];
    uint64_t          enabled[QS_ENTITY_MASK_WORDS];
    uint32_t          entity_count;

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

/* ================================================================
   BUILT-IN TYPE HANDLES
   ================================================================ */

static Qs_ComponentType *s_transform_type;
static Qs_ComponentType *s_mesh_comp_type;
static Qs_ComponentType *s_light_comp_type;

Qs_ComponentType *qs_transform_type(void)  { return s_transform_type; }
Qs_ComponentType *qs_mesh_comp_type(void)  { return s_mesh_comp_type; }
Qs_ComponentType *qs_light_comp_type(void) { return s_light_comp_type; }

static void register_builtin_types(Qs_Engine *engine)
{
    s_transform_type = qs_component_register(engine, &(Qs_ComponentTypeDesc){
        .name      = "Transform",
        .data_size = sizeof(Qs_Transform),
        .init      = transform_init,
    });

    s_mesh_comp_type = qs_component_register(engine, &(Qs_ComponentTypeDesc){
        .name      = "MeshComp",
        .data_size = sizeof(Qs_MeshComp),
        .init      = mesh_comp_init,
    });

    s_light_comp_type = qs_component_register(engine, &(Qs_ComponentTypeDesc){
        .name      = "LightComp",
        .data_size = sizeof(Qs_LightComp),
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

    /* Free component store data buffers */
    for (uint32_t t = 0; t < QS_MAX_COMPONENT_TYPES; t++) {
        free(scene->stores[t].data);
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

    /* Auto-add Transform component */
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
            bit_test(scene->stores[t].mask, entity))
        {
            qs_entity_remove(scene, entity, &g_scene_system->types[t]);
        }
    }

    bit_clear(scene->alive, entity);
    bit_clear(scene->enabled, entity);
    scene->entity_names[entity][0] = '\0';
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
    if (bit_test(store->mask, entity))
        return NULL;

    /* Lazy-allocate data array for this component type in this scene */
    if (!store->data) {
        store->data = (uint8_t *)calloc(QS_MAX_ENTITIES, type->data_size);
        if (!store->data) return NULL;
    }

    bit_set(store->mask, entity);
    void *comp = store->data + (size_t)entity * type->data_size;
    memset(comp, 0, type->data_size);

    if (type->init)
        type->init(comp, scene, entity);

    return comp;
}

void *qs_entity_get(const Qs_Scene *scene, Qs_Entity entity,
                     const Qs_ComponentType *type)
{
    if (!scene || !type || entity >= QS_MAX_ENTITIES ||
        !type->in_use || !scene->stores[type->index].data)
        return NULL;

    const ComponentStore *store = &scene->stores[type->index];
    if (!bit_test(store->mask, entity)) return NULL;

    return store->data + (size_t)entity * type->data_size;
}

void qs_entity_remove(Qs_Scene *scene, Qs_Entity entity,
                       Qs_ComponentType *type)
{
    if (!scene || !type || entity >= QS_MAX_ENTITIES ||
        !type->in_use || !scene->stores[type->index].data)
        return;

    ComponentStore *store = &scene->stores[type->index];
    if (!bit_test(store->mask, entity)) return;

    void *comp = store->data + (size_t)entity * type->data_size;
    if (type->destroy)
        type->destroy(comp, scene, entity);

    memset(comp, 0, type->data_size);
    bit_clear(store->mask, entity);
}

bool qs_entity_has(const Qs_Scene *scene, Qs_Entity entity,
                    const Qs_ComponentType *type)
{
    if (!scene || !type || entity >= QS_MAX_ENTITIES || !type->in_use)
        return false;
    return bit_test(scene->stores[type->index].mask, entity);
}

/* ================================================================
   ITERATION
   ================================================================ */

Qs_Entity qs_scene_first(const Qs_Scene *scene,
                          const Qs_ComponentType *type)
{
    if (!scene || !type || !type->in_use) return QS_ENTITY_INVALID;

    const ComponentStore *store = &scene->stores[type->index];
    uint32_t e = bit_next_set(store->mask, 0);
    return e < QS_MAX_ENTITIES ? (Qs_Entity)e : QS_ENTITY_INVALID;
}

Qs_Entity qs_scene_next(const Qs_Scene *scene,
                         const Qs_ComponentType *type,
                         Qs_Entity after)
{
    if (!scene || !type || !type->in_use || after >= QS_MAX_ENTITIES)
        return QS_ENTITY_INVALID;

    const ComponentStore *store = &scene->stores[type->index];
    uint32_t e = bit_next_set(store->mask, after + 1);
    return e < QS_MAX_ENTITIES ? (Qs_Entity)e : QS_ENTITY_INVALID;
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
        if (!store->data) continue;

        for (uint32_t e = bit_next_set(store->mask, 0);
             e < QS_MAX_ENTITIES;
             e = bit_next_set(store->mask, e + 1))
        {
            /* Skip disabled entities */
            if (!bit_test(scene->enabled, e)) continue;

            void *comp = store->data + (size_t)e * type->data_size;
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
