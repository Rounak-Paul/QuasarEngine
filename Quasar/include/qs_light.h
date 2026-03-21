#ifndef QS_LIGHT_H
#define QS_LIGHT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct Qs_SystemDesc Qs_SystemDesc;
typedef struct Qs_Engine     Qs_Engine;
typedef struct Qs_Renderer   Qs_Renderer;
typedef struct Qs_Light      Qs_Light;

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
    float         direction[3];       ///< Normalized direction (directional/spot).
    float         color[3];           ///< Linear RGB color (default: {1,1,1}).
    float         intensity;          ///< Luminous intensity (default: 1.0).

    /* Attenuation (point/spot) */
    float         range;              ///< Maximum influence radius (0 = infinite).

    /* Spot cone */
    float         inner_cone_deg;     ///< Inner cone angle in degrees (full intensity).
    float         outer_cone_deg;     ///< Outer cone angle in degrees (falloff edge).

    bool          cast_shadows;
} Qs_LightDesc;

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
    float    inner_cone_cos;    ///< cos(inner_cone_deg).
    float    outer_cone_cos;    ///< cos(outer_cone_deg).
    uint32_t type;              ///< 0=directional, 1=point, 2=spot.
    uint32_t cast_shadows;
    uint32_t _pad;
} Qs_LightGPU;

/* ================================================================
   LIGHT API
   ================================================================ */

/// Creates a light. Returns NULL on failure.
Qs_Light *qs_light_create(Qs_Engine *engine, const Qs_LightDesc *desc);

/// Destroys a light.
void qs_light_destroy(Qs_Light *light);

/// Returns the debug name.
const char *qs_light_name(const Qs_Light *light);

/// Returns a mutable pointer to the light's position.
float *qs_light_position(Qs_Light *light);

/// Returns a mutable pointer to the light's direction.
float *qs_light_direction(Qs_Light *light);

/// Returns a mutable pointer to the light's color.
float *qs_light_color(Qs_Light *light);

/// Sets the light intensity.
void qs_light_set_intensity(Qs_Light *light, float intensity);

/// Returns the light intensity.
float qs_light_intensity(const Qs_Light *light);

/// Sets the light range (point/spot).
void qs_light_set_range(Qs_Light *light, float range);

/// Sets the spot cone angles in degrees.
void qs_light_set_cone(Qs_Light *light, float inner_deg, float outer_deg);

/// Enables or disables a light.
void qs_light_set_enabled(Qs_Light *light, bool enabled);

/// Returns true if the light is enabled.
bool qs_light_enabled(const Qs_Light *light);

/* ================================================================
   RENDERER LIGHT SUBMISSION
   ================================================================ */

/// Submits a light to a renderer for the current frame.
/// Lights must be submitted each frame before the render pass executes.
void qs_renderer_submit_light(Qs_Renderer *renderer, Qs_Light *light);

/// Clears all submitted lights from a renderer (called automatically each frame).
void qs_renderer_clear_lights(Qs_Renderer *renderer);

/// Returns the array of GPU-packed lights submitted to this renderer.
/// The pointer is valid until the next clear_lights call.
const Qs_LightGPU *qs_renderer_lights(const Qs_Renderer *renderer,
                                       uint32_t *out_count);

/* ================================================================
   LIGHT SYSTEM
   ================================================================ */

/// Returns the system descriptor for registration with the engine.
Qs_SystemDesc qs_light_system_desc(void);

#endif
