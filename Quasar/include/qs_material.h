#ifndef QS_MATERIAL_H
#define QS_MATERIAL_H

#include "qs_gpu.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct Qs_Engine     Qs_Engine;
typedef struct Qs_Material   Qs_Material;  ///< Opaque — defined by the material backend.
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
    Qs_Texture   *metallic_roughness_texture;

    /* Normal map */
    float         normal_scale;           ///< Normal map strength (default: 1.0).
    Qs_Texture   *normal_texture;

    /* Ambient occlusion */
    float         occlusion_strength;     ///< AO strength (default: 1.0).
    Qs_Texture   *occlusion_texture;

    /* Emissive */
    float         emissive_factor[3];     ///< RGB emissive multiplier (default: {0,0,0}).
    Qs_Texture   *emissive_texture;

    /* Alpha */
    Qs_AlphaMode  alpha_mode;             ///< Default: OPAQUE.
    float         alpha_cutoff;           ///< Threshold for MASK mode (default: 0.5).

    bool          double_sided;
} Qs_MaterialDesc;

/// Returns a Qs_MaterialDesc with PBR-sane defaults pre-filled.
/// Callers should modify individual fields after calling this.
static inline Qs_MaterialDesc qs_material_desc_defaults(void)
{
    return (Qs_MaterialDesc){
        .base_color_factor  = {1.0f, 1.0f, 1.0f, 1.0f},
        .roughness_factor   = 1.0f,
        .normal_scale       = 1.0f,
        .occlusion_strength = 1.0f,
        .alpha_cutoff       = 0.5f,
    };
}

/* ================================================================
   PBR PARAMS - GPU-ready parameter block
   ================================================================ */

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

/* ================================================================
   PUBLIC MATERIAL API
   ================================================================ */

/// Creates a PBR material.  Destroy with qs_material_destroy.
Qs_Material *qs_material_create(Qs_Engine *engine, const Qs_MaterialDesc *desc);

/// Destroys a material and frees its GPU resources.
void qs_material_destroy(Qs_Material *material);

/// Returns the debug name.
const char *qs_material_name(const Qs_Material *material);

/// Returns the descriptor set containing all PBR texture bindings.
/// Layout: binding 0 = base_color, 1 = metallic_roughness, 2 = normal,
///         3 = occlusion, 4 = emissive.
Qs_GpuDescriptorSet *qs_material_descriptor_set(const Qs_Material *material);

/// Returns the descriptor set layout shared by all materials.
Qs_GpuDescriptorSetLayout *qs_material_set_layout(void);

/// Returns the materialâ€™s PBR parameters for GPU upload.
const Qs_PBRParams *qs_material_params(const Qs_Material *material);

/// Returns the alpha mode of the material.
Qs_AlphaMode qs_material_alpha_mode(const Qs_Material *material);

/// Returns true if the material is double-sided.
bool qs_material_double_sided(const Qs_Material *material);

#endif