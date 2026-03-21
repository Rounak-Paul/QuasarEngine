#ifndef QS_MATERIAL_H
#define QS_MATERIAL_H

#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct Qs_SystemDesc Qs_SystemDesc;
typedef struct Qs_Engine     Qs_Engine;
typedef struct Ca_Instance   Ca_Instance;
typedef struct Qs_Material   Qs_Material;
typedef struct Qs_Texture    Qs_Texture;

/* ================================================================
   ALPHA MODE
   ================================================================ */

/// Alpha compositing mode (matches glTF spec).
typedef enum Qs_AlphaMode {
    QS_ALPHA_MODE_OPAQUE = 0,
    QS_ALPHA_MODE_MASK   = 1,
    QS_ALPHA_MODE_BLEND  = 2,
} Qs_AlphaMode;

/* ================================================================
   PBR MATERIAL DESCRIPTOR
   ================================================================ */

/// Configuration for creating a PBR metallic-roughness material.
typedef struct Qs_MaterialDesc {
    const char   *name;

    /* Base color (albedo) */
    float         base_color_factor[4];   ///< RGBA multiplier (default: {1,1,1,1}).
    Qs_Texture   *base_color_texture;     ///< Optional albedo map.

    /* Metallic-roughness */
    float         metallic_factor;        ///< 0.0 = dielectric, 1.0 = metal (default: 1.0).
    float         roughness_factor;       ///< 0.0 = smooth, 1.0 = rough (default: 1.0).
    Qs_Texture   *metallic_roughness_texture; ///< Green = roughness, Blue = metallic (glTF).

    /* Normal map */
    float         normal_scale;           ///< Normal map strength (default: 1.0).
    Qs_Texture   *normal_texture;

    /* Ambient occlusion */
    float         occlusion_strength;     ///< AO strength (default: 1.0).
    Qs_Texture   *occlusion_texture;      ///< R-channel occlusion.

    /* Emissive */
    float         emissive_factor[3];     ///< RGB emissive multiplier (default: {0,0,0}).
    Qs_Texture   *emissive_texture;

    /* Alpha */
    Qs_AlphaMode  alpha_mode;             ///< Default: OPAQUE.
    float         alpha_cutoff;           ///< Threshold for MASK mode (default: 0.5).

    bool          double_sided;
} Qs_MaterialDesc;

/* ================================================================
   MATERIAL API
   ================================================================ */

/// Creates a PBR material. Returns NULL on failure.
Qs_Material *qs_material_create(Qs_Engine *engine, const Qs_MaterialDesc *desc);

/// Destroys a material and frees its GPU resources.
void qs_material_destroy(Qs_Material *material);

/// Returns the debug name.
const char *qs_material_name(const Qs_Material *material);

/// Returns the descriptor set containing all PBR texture bindings.
/// Layout: binding 0 = base_color, 1 = metallic_roughness, 2 = normal,
///         3 = occlusion, 4 = emissive.
VkDescriptorSet qs_material_descriptor_set(const Qs_Material *material);

/// Returns the descriptor set layout shared by all materials.
VkDescriptorSetLayout qs_material_set_layout(void);

/// Returns a pointer to the material's PBR parameters (GPU-ready struct).
typedef struct Qs_PBRParams {
    float base_color_factor[4];
    float metallic_factor;
    float roughness_factor;
    float normal_scale;
    float occlusion_strength;
    float emissive_factor[3];
    float alpha_cutoff;
    uint32_t alpha_mode;
    uint32_t has_base_color_tex;
    uint32_t has_metallic_roughness_tex;
    uint32_t has_normal_tex;
    uint32_t has_occlusion_tex;
    uint32_t has_emissive_tex;
    uint32_t double_sided;
    uint32_t _pad[2];
} Qs_PBRParams;

/// Returns the material's PBR parameters for upload to a uniform buffer.
const Qs_PBRParams *qs_material_params(const Qs_Material *material);

/// Returns the alpha mode of the material.
Qs_AlphaMode qs_material_alpha_mode(const Qs_Material *material);

/// Returns true if the material is double-sided.
bool qs_material_double_sided(const Qs_Material *material);

/* ================================================================
   MATERIAL SYSTEM
   ================================================================ */

/// Returns the system descriptor for registration with the engine.
Qs_SystemDesc qs_material_system_desc(Ca_Instance *ca_instance);

#endif
