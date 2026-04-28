#ifndef QS_SCENE_H
#define QS_SCENE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "qs_light.h"
#include "qs_reflect.h"

typedef struct Qs_Engine         Qs_Engine;
typedef struct Qs_Scene          Qs_Scene;
typedef struct Qs_ComponentType  Qs_ComponentType;
typedef struct Qs_TypeInfo       Qs_TypeInfo;
typedef struct Qs_Mesh           Qs_Mesh;
typedef struct Qs_Material       Qs_Material;
struct cJSON;

/* ================================================================
   ENTITY — lightweight index handle
   ================================================================ */

#ifndef QS_ENTITY_DEFINED
#define QS_ENTITY_DEFINED
typedef uint32_t Qs_Entity;
#define QS_ENTITY_INVALID UINT32_MAX
#endif

/* ================================================================
   COMPONENT TYPE REGISTRATION
   ================================================================ */

/// Descriptor for registering a custom component type.
/// Plugins/scripts register their own types at startup.
typedef struct Qs_ComponentTypeDesc {
    const char *name;           ///< Unique type name (e.g. "RigidBody", "AudioSource").
    size_t      data_size;      ///< Size in bytes of one component instance.

    /// Reflection metadata for auto-serialization.  May be NULL.
    const Qs_TypeInfo *type_info;

    /// Called when a component is added to an entity.  May be NULL.
    void (*init)(void *component, Qs_Scene *scene, Qs_Entity entity);

    /// Called when a component is removed or the entity is destroyed.  May be NULL.
    void (*destroy)(void *component, Qs_Scene *scene, Qs_Entity entity);

    /// Called once per frame for each entity with this component (active scene only).
    /// May be NULL to skip per-frame updates.
    void (*update)(void *component, Qs_Scene *scene, Qs_Entity entity, float dt);
} Qs_ComponentTypeDesc;

/// Registers a new component type globally.  Returns the type handle.
Qs_ComponentType *qs_component_register(Qs_Engine *engine,
                                         const Qs_ComponentTypeDesc *desc);

/// Finds a registered component type by name.  Returns NULL if not found.
Qs_ComponentType *qs_component_find(const char *name);

/// Returns the reflection type info linked to a component type, or NULL.
const Qs_TypeInfo *qs_component_type_info(const Qs_ComponentType *type);

/// Returns the name of a component type.
const char *qs_component_type_name(const Qs_ComponentType *type);

/// Returns the number of registered component types.
uint32_t qs_component_type_count(void);

/// Returns the i-th registered component type (0-based dense index).
/// Returns NULL if index >= count.
Qs_ComponentType *qs_component_type_at(uint32_t index);

/* ================================================================
   BUILT-IN COMPONENT TYPES
   ================================================================ */

/// 3D transform: position + rotation (quaternion) + scale.
typedef struct Qs_Transform {
    float position[3];
    float rotation[4];   ///< Quaternion (x, y, z, w).  Default: identity (0,0,0,1).
    float scale[3];      ///< Default: (1, 1, 1).
} Qs_Transform;

/// Renderable mesh reference.
/// Both `mesh_path` and `material_path` are stored relative to the file the
/// component lives in (a .qscene or .qproto).  Resolution happens at scene
/// load time via the runtime asset cache.
typedef struct Qs_MeshComp {
    Qs_Mesh     *mesh;             ///< Resolved at load (runtime-only).
    Qs_Material *material;         ///< Resolved at load (runtime-only).
    bool         visible;          ///< Default: true.
    char         mesh_path[256];   ///< Project-relative or scene-relative .qsmesh path.
    char         material_path[256];///< Project-relative or scene-relative .qsmat path.
} Qs_MeshComp;

/// Light component — all parameters stored inline for reflection, serialization,
/// and inspector editing.  No opaque pointer; data is packed directly to GPU at
/// submission time via qs_renderer_submit_light_comp().
typedef struct Qs_LightComp {
    Qs_LightType type;            ///< Default: QS_LIGHT_DIRECTIONAL.
    float        direction[3];    ///< Normalised direction (directional / spot).
    float        color[3];        ///< Linear RGB. Default: {1, 1, 1}.
    float        intensity;       ///< Luminous intensity. Default: 1.0.
    float        range;           ///< Max influence radius (0 = infinite).
    float        inner_cone_deg;  ///< Inner cone angle for spot lights.
    float        outer_cone_deg;  ///< Outer cone angle for spot lights.
    bool         cast_shadows;
    bool         enabled;
} Qs_LightComp;

/// Persistent entity ID (auto-assigned on creation, survives serialization).
typedef struct Qs_IdComp {
    uint32_t id;
} Qs_IdComp;

/// Entity tag for categorization / grouping.
typedef struct Qs_TagComp {
    char tag[64];
} Qs_TagComp;

/// References a prototype file (.qproto) — a reusable entity template
/// analogous to a Unity prefab.  A prototype is itself a full scene:
/// it owns its own ECS world (`inner`) and is rendered by recursively
/// composing its entities' world transforms with the parent entity's
/// world matrix.  Imported models (e.g. glTF) are automatically
/// represented as prototypes by the asset import pipeline.
///
/// Per-instance overrides:
///   When a prototype is placed in a scene, individual fields of inner-scene
///   entities can be customised without modifying the source .qproto file.
///   Overrides are keyed by (inner_entity_id, comp_name, field_name) and
///   applied to the inner scene each frame before rendering.  See the
///   qs_prototype_*_override API below.
typedef struct Qs_PrototypeOverride {
    uint32_t       inner_entity_id;     ///< Stable IdComp.id within the inner scene.
    char           comp_name[32];       ///< Component type name, e.g. "Transform".
    char           field_name[32];      ///< Field name, e.g. "position".
    Qs_FieldType   type;                ///< Value type tag.
    union {
        float    fv[4];                 ///< FLOAT / FLOAT2 / FLOAT3 / FLOAT4.
        int32_t  iv;                    ///< INT32.
        uint32_t uv;                    ///< UINT32 / ENTITY.
        bool     bv;                    ///< BOOL.
        char     sv[256];               ///< STRING (truncated if larger).
    } value;
} Qs_PrototypeOverride;

typedef struct Qs_PrototypeComp {
    char            path[256];           ///< Project- or scene-relative .qproto path.
    /* ---- runtime fields (not serialized via reflection) ---- */
    Qs_Scene       *inner;               ///< Inner ECS world (lazy-loaded on first use).
    bool            load_failed;         ///< Set after a failed lazy load to avoid retries.
    Qs_PrototypeOverride *overrides;     ///< Heap-allocated; may be NULL.
    uint32_t        override_count;
    uint32_t        override_cap;
} Qs_PrototypeComp;

/* ---- Prototype override API ----------------------------------------- */

/// Sets (or replaces) an override on the prototype instance.  `value` must
/// point to data matching `type` (e.g. float[3] for FLOAT3, char* for
/// STRING).  Returns true on success.  The override is applied to the
/// inner scene immediately if it is loaded, and re-applied each frame.
bool qs_prototype_set_override(Qs_PrototypeComp *pc,
                               uint32_t inner_entity_id,
                               const char *comp_name,
                               const char *field_name,
                               Qs_FieldType type,
                               const void *value);

/// Removes an override.  Returns true if one existed.  After clearing, the
/// inner-scene field reverts to its source-prototype value on the next
/// override-apply pass; if you need an immediate revert, reload the inner
/// scene via qs_prototype_reload().
bool qs_prototype_clear_override(Qs_PrototypeComp *pc,
                                 uint32_t inner_entity_id,
                                 const char *comp_name,
                                 const char *field_name);

/// Returns the override matching the key, or NULL if none.
const Qs_PrototypeOverride *qs_prototype_find_override(
    const Qs_PrototypeComp *pc,
    uint32_t inner_entity_id,
    const char *comp_name,
    const char *field_name);

/// Number of overrides currently stored on the instance.
uint32_t qs_prototype_override_count(const Qs_PrototypeComp *pc);

/// Indexed access to the overrides list (0 .. count-1).
const Qs_PrototypeOverride *qs_prototype_override_at(
    const Qs_PrototypeComp *pc, uint32_t index);

/// Applies all stored overrides to the inner scene's component data.
/// Safe to call when inner is NULL (no-op).  Called automatically before
/// rendering, but can be invoked manually after editing for an
/// immediate UI refresh.
void qs_prototype_apply_overrides(Qs_PrototypeComp *pc);

/// Drops the lazy-loaded inner scene so it will be reloaded fresh from
/// disk on the next render.  Useful after clearing overrides to discard
/// any side-effects in the inner ECS data.
void qs_prototype_reload(Qs_PrototypeComp *pc);

/// Looks up an inner-scene entity by its stable IdComp.id.  Returns
/// QS_ENTITY_INVALID if no entity with that id exists.
Qs_Entity qs_scene_find_by_id(Qs_Scene *scene, uint32_t id);


/// Returns the built-in Transform component type handle.
Qs_ComponentType *qs_transform_type(void);

/// Returns the built-in MeshComp component type handle.
Qs_ComponentType *qs_mesh_comp_type(void);

/// Returns the built-in LightComp component type handle.
Qs_ComponentType *qs_light_comp_type(void);

/// Returns the built-in IdComp component type handle.
Qs_ComponentType *qs_id_comp_type(void);

/// Returns the built-in TagComp component type handle.
Qs_ComponentType *qs_tag_comp_type(void);

/// Returns the built-in PrototypeComp component type handle.
Qs_ComponentType *qs_prototype_comp_type(void);

/* ================================================================
   SCENE
   ================================================================ */

typedef void (*Qs_SceneCallback)(Qs_Scene *scene, void *user_data);

/// Configuration for creating a scene.
typedef struct Qs_SceneDesc {
    const char        *name;
    Qs_SceneCallback   on_activate;    ///< Called when this scene becomes active.
    Qs_SceneCallback   on_deactivate;  ///< Called when this scene is deactivated.
    void              *user_data;      ///< Passed to callbacks.
} Qs_SceneDesc;

/// Creates a new scene.  Returns NULL on failure.
Qs_Scene *qs_scene_create(Qs_Engine *engine, const Qs_SceneDesc *desc);

/// Destroys a scene and all its entities.
void qs_scene_destroy(Qs_Scene *scene);

/// Returns the scene name.
const char *qs_scene_name(const Qs_Scene *scene);

/// Sets the active scene.  Pass NULL to deactivate all.
/// Fires on_deactivate for the previous scene, on_activate for the new one.
void qs_scene_set_active(Qs_Scene *scene);

/// Returns the currently active scene, or NULL.
Qs_Scene *qs_scene_active(void);

/* ================================================================
   ENTITY LIFECYCLE
   ================================================================ */

/// Creates an entity in the scene.  A Transform component is auto-added.
Qs_Entity qs_entity_create(Qs_Scene *scene, const char *name);

/// Destroys an entity and all its components.
void qs_entity_destroy(Qs_Scene *scene, Qs_Entity entity);

/// Returns true if the entity handle is alive.
bool qs_entity_valid(const Qs_Scene *scene, Qs_Entity entity);

/// Returns the entity's name.
const char *qs_entity_name(const Qs_Scene *scene, Qs_Entity entity);

/// Sets the entity's display name.
void qs_entity_set_name(Qs_Scene *scene, Qs_Entity entity, const char *name);

/// Enables or disables an entity.  Disabled entities skip component updates.
void qs_entity_set_enabled(Qs_Scene *scene, Qs_Entity entity, bool enabled);

/// Returns true if the entity is enabled.
bool qs_entity_enabled(const Qs_Scene *scene, Qs_Entity entity);

/// Sets the parent of an entity.  Pass QS_ENTITY_INVALID to make it a root entity.
void qs_entity_set_parent(Qs_Scene *scene, Qs_Entity entity, Qs_Entity parent);

/// Returns the parent entity, or QS_ENTITY_INVALID if the entity is a root.
Qs_Entity qs_entity_get_parent(const Qs_Scene *scene, Qs_Entity entity);

/// Returns the number of alive entities in the scene.
uint32_t qs_scene_entity_count(const Qs_Scene *scene);

/* ================================================================
   COMPONENT CRUD
   ================================================================ */

/// Adds a component of the given type to an entity.  Returns a pointer to
/// the zeroed (then init'd) component data.  Returns NULL if already present
/// or on failure.
void *qs_entity_add(Qs_Scene *scene, Qs_Entity entity,
                     Qs_ComponentType *type);

/// Returns a pointer to the entity's component data, or NULL if absent.
void *qs_entity_get(const Qs_Scene *scene, Qs_Entity entity,
                     const Qs_ComponentType *type);

/// Removes a component from an entity.
void qs_entity_remove(Qs_Scene *scene, Qs_Entity entity,
                       Qs_ComponentType *type);

/// Returns true if the entity has the given component type.
bool qs_entity_has(const Qs_Scene *scene, Qs_Entity entity,
                    const Qs_ComponentType *type);

/* ================================================================
   ITERATION
   ================================================================ */

/// Returns the first entity with the given component, or QS_ENTITY_INVALID.
Qs_Entity qs_scene_first(const Qs_Scene *scene,
                          const Qs_ComponentType *type);

/// Returns the next entity after `after` with the component,
/// or QS_ENTITY_INVALID when done.
Qs_Entity qs_scene_next(const Qs_Scene *scene,
                         const Qs_ComponentType *type,
                         Qs_Entity after);

/* ================================================================
   SCENE SERIALIZATION
   ================================================================ */

/// Serializes the entire scene to a cJSON object.  Caller owns the result.
/// Components with reflection type_info are auto-serialized.
struct cJSON *qs_scene_to_json(const Qs_Scene *scene);

/// Deserializes a cJSON object into a scene, creating entities and components.
/// The scene should be empty or newly created.  Returns true on success.
bool qs_scene_from_json(Qs_Scene *scene, Qs_Engine *engine,
                        const struct cJSON *json);

/// Saves the scene to a .qscene JSON file at the given path.
/// Returns true on success.
bool qs_scene_save(const Qs_Scene *scene, const char *path);

/// Loads a scene from a .qscene JSON file.  Creates entities and components
/// inside the given (empty/new) scene.  Returns true on success.
bool qs_scene_load(Qs_Scene *scene, Qs_Engine *engine, const char *path);

/* ================================================================
   WORLD TRANSFORM
   ================================================================ */

/// Computes the world-space model matrix for an entity by composing
/// local transforms up the parent chain.  Result is column-major 4×4.
void qs_scene_world_matrix(const Qs_Scene *scene, Qs_Entity entity,
                           float out[16]);

/* ================================================================
   RENDERING SUBMISSION
   ================================================================
   Scenes know how to submit their renderables and lights to a
   renderer.  PrototypeComp entities recurse into their inner scene,
   composing the parent's world matrix so nested prototypes render
   correctly.
   ================================================================ */

typedef struct Qs_Renderer Qs_Renderer;

/// Submit every visible MeshComp + LightComp entity in `scene` (and
/// recursively in nested PrototypeComp scenes) to `renderer`.
/// `parent_world` is the world matrix to compose with each entity's
/// local transform — pass an identity matrix for top-level scenes.
void qs_scene_submit_renderables(Qs_Scene *scene,
                                 Qs_Engine *engine,
                                 Qs_Renderer *renderer,
                                 const float parent_world[16]);

#endif
