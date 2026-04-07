#ifndef QS_LIGHT_H
#define QS_LIGHT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct Qs_Engine     Qs_Engine;
typedef struct Qs_Light      Qs_Light;   ///< Opaque â€” defined by the light backend.

/* ================================================================
   LIGHT TYPES
   ================================================================ */

typedef enum Qs_LightType {
    QS_LIGHT_DIRECTIONAL = 0,   ///< Infinite parallel rays (sun).
    QS_LIGHT_POINT       = 1,   ///< Omnidirectional from a position.
    QS_LIGHT_SPOT        = 2,   ///< Cone from a position along a direction.
} Qs_LightType;

/* ================================================================
   LIGHT DESCRIPTOR
   ================================================================ */

/// Configuration for creating a light.
typedef struct Qs_LightDesc {
    const char   *name;
    Qs_LightType  type;

    float         position[3];        ///< World-space position (point/spot).
    float         direction[3];       ///< Normalised direction (directional/spot).
    float         color[3];           ///< Linear RGB color (default: {1,1,1}).
    float         intensity;          ///< Luminous intensity (default: 1.0).

    float         range;              ///< Maximum influence radius (0 = infinite).

    float         inner_cone_deg;     ///< Inner cone angle in degrees (full intensity).
    float         outer_cone_deg;     ///< Outer cone angle in degrees (falloff edge).

    bool          cast_shadows;
} Qs_LightDesc;

/// Returns a Qs_LightDesc with sensible defaults pre-filled.
/// Callers should modify individual fields after calling this.
static inline Qs_LightDesc qs_light_desc_defaults(void)
{
    return (Qs_LightDesc){
        .color          = {1.0f, 1.0f, 1.0f},
        .intensity      = 1.0f,
        .inner_cone_deg = 30.0f,
        .outer_cone_deg = 45.0f,
    };
}

/* ================================================================
   GPU-READY LIGHT DATA
   ================================================================ */

/// Packed light structure for GPU upload (std140-friendly, 64 bytes).
typedef struct Qs_LightGPU {
    float    position[3];
    float    range;
    float    direction[3];
    float    intensity;
    float    color[3];
    float    inner_cone_cos;
    float    outer_cone_cos;
    uint32_t type;
    uint32_t cast_shadows;
    uint32_t _pad;
} Qs_LightGPU;

/* ================================================================
   PUBLIC LIGHT API
   ================================================================ */

/// Creates a light.  Destroy with qs_light_destroy.
Qs_Light *qs_light_create(Qs_Engine *engine, const Qs_LightDesc *desc);

/// Destroys a light.
void qs_light_destroy(Qs_Light *light);

/// Returns the debug name.
const char *qs_light_name(const Qs_Light *light);

/// Returns a mutable pointer to the lightâ€™s position.
float *qs_light_position(Qs_Light *light);

/// Returns a mutable pointer to the lightâ€™s direction.
float *qs_light_direction(Qs_Light *light);

/// Returns a mutable pointer to the lightâ€™s color.
float *qs_light_color(Qs_Light *light);

void  qs_light_set_intensity(Qs_Light *light, float intensity);
float qs_light_intensity(const Qs_Light *light);
void  qs_light_set_range(Qs_Light *light, float range);
void  qs_light_set_cone(Qs_Light *light, float inner_deg, float outer_deg);
void  qs_light_set_enabled(Qs_Light *light, bool enabled);
bool  qs_light_enabled(const Qs_Light *light);

/// Returns true if the light is active and should be submitted for rendering.
bool  qs_light_is_active(const Qs_Light *light);

/// Packs a light's data into a GPU-ready struct for UBO upload.
void  qs_light_pack_gpu(const Qs_Light *light, Qs_LightGPU *out);

#endif