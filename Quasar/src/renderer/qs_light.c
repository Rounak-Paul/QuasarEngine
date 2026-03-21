#include "qs_light.h"
#include "qs_renderer.h"
#include "qs_log.h"
#include "qs_system.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ================================================================
   LIMITS
   ================================================================ */

#define QS_MAX_LIGHTS              256
#define QS_MAX_LIGHTS_PER_RENDERER 128

/* ================================================================
   INTERNAL TYPES
   ================================================================ */

struct Qs_Light {
    char          name[64];
    bool          in_use;
    bool          enabled;
    Qs_LightType  type;

    float         position[3];
    float         direction[3];
    float         color[3];
    float         intensity;
    float         range;
    float         inner_cone_cos;
    float         outer_cone_cos;
    bool          cast_shadows;
};

typedef struct Qs_LightSystemData {
    Qs_Light  lights[QS_MAX_LIGHTS];
    uint32_t  count;
} Qs_LightSystemData;

static Qs_LightSystemData *g_light_system;

/* ================================================================
   HELPERS
   ================================================================ */

static float deg_to_rad(float deg) { return deg * 3.14159265f / 180.0f; }

static void pack_light_gpu(const Qs_Light *l, Qs_LightGPU *out)
{
    out->position[0] = l->position[0];
    out->position[1] = l->position[1];
    out->position[2] = l->position[2];
    out->range        = l->range;

    out->direction[0] = l->direction[0];
    out->direction[1] = l->direction[1];
    out->direction[2] = l->direction[2];
    out->intensity     = l->intensity;

    out->color[0] = l->color[0];
    out->color[1] = l->color[1];
    out->color[2] = l->color[2];

    out->inner_cone_cos = l->inner_cone_cos;
    out->outer_cone_cos = l->outer_cone_cos;
    out->type           = (uint32_t)l->type;
    out->cast_shadows   = l->cast_shadows ? 1 : 0;
    out->_pad           = 0;
}

/* ================================================================
   LIGHT LIFECYCLE
   ================================================================ */

Qs_Light *qs_light_create(Qs_Engine *engine, const Qs_LightDesc *desc)
{
    (void)engine;
    if (!g_light_system || !desc) return NULL;

    Qs_Light *l = NULL;
    for (uint32_t i = 0; i < QS_MAX_LIGHTS; i++) {
        if (!g_light_system->lights[i].in_use) {
            l = &g_light_system->lights[i];
            break;
        }
    }
    if (!l) {
        QS_LOG_ERROR("Light limit reached (%d)", QS_MAX_LIGHTS);
        return NULL;
    }

    memset(l, 0, sizeof(*l));
    l->in_use  = true;
    l->enabled = true;
    l->type    = desc->type;

    l->position[0]  = desc->position[0];
    l->position[1]  = desc->position[1];
    l->position[2]  = desc->position[2];
    l->direction[0] = desc->direction[0];
    l->direction[1] = desc->direction[1];
    l->direction[2] = desc->direction[2];

    l->color[0] = desc->color[0] != 0.0f ? desc->color[0] : 1.0f;
    l->color[1] = desc->color[1] != 0.0f ? desc->color[1] : 1.0f;
    l->color[2] = desc->color[2] != 0.0f ? desc->color[2] : 1.0f;

    l->intensity = desc->intensity != 0.0f ? desc->intensity : 1.0f;
    l->range     = desc->range;

    float inner = desc->inner_cone_deg > 0.0f ? desc->inner_cone_deg : 30.0f;
    float outer = desc->outer_cone_deg > 0.0f ? desc->outer_cone_deg : 45.0f;
    l->inner_cone_cos = cosf(deg_to_rad(inner));
    l->outer_cone_cos = cosf(deg_to_rad(outer));

    l->cast_shadows = desc->cast_shadows;

    /* Default direction for directional/spot if zero */
    if (l->direction[0] == 0.0f && l->direction[1] == 0.0f && l->direction[2] == 0.0f) {
        l->direction[1] = -1.0f;  /* Down */
    }

    if (desc->name)
        snprintf(l->name, sizeof(l->name), "%s", desc->name);
    else
        snprintf(l->name, sizeof(l->name), "light_%u", g_light_system->count);

    g_light_system->count++;
    QS_LOG_INFO("Light '%s' created (type=%d, intensity=%.1f)",
                l->name, l->type, l->intensity);
    return l;
}

void qs_light_destroy(Qs_Light *light)
{
    if (!light || !light->in_use) return;

    QS_LOG_INFO("Light '%s' destroyed", light->name);
    light->in_use = false;

    if (g_light_system && g_light_system->count > 0)
        g_light_system->count--;
}

/* ================================================================
   ACCESSORS
   ================================================================ */

const char *qs_light_name(const Qs_Light *light)
{
    return light ? light->name : NULL;
}

float *qs_light_position(Qs_Light *light)
{
    return light ? light->position : NULL;
}

float *qs_light_direction(Qs_Light *light)
{
    return light ? light->direction : NULL;
}

float *qs_light_color(Qs_Light *light)
{
    return light ? light->color : NULL;
}

void qs_light_set_intensity(Qs_Light *light, float intensity)
{
    if (light) light->intensity = intensity;
}

float qs_light_intensity(const Qs_Light *light)
{
    return light ? light->intensity : 0.0f;
}

void qs_light_set_range(Qs_Light *light, float range)
{
    if (light) light->range = range;
}

void qs_light_set_cone(Qs_Light *light, float inner_deg, float outer_deg)
{
    if (!light) return;
    light->inner_cone_cos = cosf(deg_to_rad(inner_deg));
    light->outer_cone_cos = cosf(deg_to_rad(outer_deg));
}

void qs_light_set_enabled(Qs_Light *light, bool enabled)
{
    if (light) light->enabled = enabled;
}

bool qs_light_enabled(const Qs_Light *light)
{
    return light ? light->enabled : false;
}

/* ================================================================
   RENDERER LIGHT SUBMISSION
   ================================================================ */

/// Per-renderer light list — stored externally and accessed via renderer pointer.
/// We use a simple parallel array keyed by renderer pointer.

#define QS_MAX_RENDERER_SLOTS 32

typedef struct RendererLights {
    Qs_Renderer  *renderer;
    Qs_LightGPU   lights[QS_MAX_LIGHTS_PER_RENDERER];
    uint32_t       count;
} RendererLights;

static RendererLights s_renderer_lights[QS_MAX_RENDERER_SLOTS];

static RendererLights *find_renderer_slot(Qs_Renderer *renderer, bool create)
{
    RendererLights *empty = NULL;
    for (uint32_t i = 0; i < QS_MAX_RENDERER_SLOTS; i++) {
        if (s_renderer_lights[i].renderer == renderer)
            return &s_renderer_lights[i];
        if (!empty && !s_renderer_lights[i].renderer)
            empty = &s_renderer_lights[i];
    }
    if (create && empty) {
        empty->renderer = renderer;
        empty->count = 0;
        return empty;
    }
    return NULL;
}

void qs_renderer_submit_light(Qs_Renderer *renderer, Qs_Light *light)
{
    if (!renderer || !light || !light->in_use || !light->enabled) return;

    RendererLights *slot = find_renderer_slot(renderer, true);
    if (!slot || slot->count >= QS_MAX_LIGHTS_PER_RENDERER) return;

    pack_light_gpu(light, &slot->lights[slot->count++]);
}

void qs_renderer_clear_lights(Qs_Renderer *renderer)
{
    if (!renderer) return;
    RendererLights *slot = find_renderer_slot(renderer, false);
    if (slot) slot->count = 0;
}

const Qs_LightGPU *qs_renderer_lights(const Qs_Renderer *renderer,
                                       uint32_t *out_count)
{
    if (!renderer) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    /* Cast away const for lookup — we don't modify */
    RendererLights *slot = find_renderer_slot((Qs_Renderer *)renderer, false);
    if (!slot || slot->count == 0) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    if (out_count) *out_count = slot->count;
    return slot->lights;
}

/* ================================================================
   LIGHT SYSTEM — engine system callbacks
   ================================================================ */

static bool light_system_init(Qs_System *system, Qs_Engine *engine)
{
    (void)engine;
    Qs_LightSystemData *data = (Qs_LightSystemData *)qs_system_data(system);
    g_light_system = data;

    memset(s_renderer_lights, 0, sizeof(s_renderer_lights));

    QS_LOG_INFO("Light system initialized");
    return true;
}

static void light_system_shutdown(Qs_System *system, Qs_Engine *engine)
{
    (void)engine;
    Qs_LightSystemData *data = (Qs_LightSystemData *)qs_system_data(system);

    for (uint32_t i = 0; i < QS_MAX_LIGHTS; i++) {
        if (data->lights[i].in_use)
            qs_light_destroy(&data->lights[i]);
    }

    memset(s_renderer_lights, 0, sizeof(s_renderer_lights));
    g_light_system = NULL;
    QS_LOG_INFO("Light system shut down");
}

Qs_SystemDesc qs_light_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Light",
        .data_size = sizeof(Qs_LightSystemData),
        .init      = light_system_init,
        .shutdown  = light_system_shutdown,
        .update    = NULL,
    };
}
