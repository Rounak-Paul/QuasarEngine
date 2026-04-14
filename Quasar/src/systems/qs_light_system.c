/*
 * qs_light_system.c — Engine light system.
 *
 * Manages CPU-side light instances and their GPU packing.  Lights hold
 * position, direction, color and other properties; the renderer packs
 * them into Qs_LightGPU structs each frame.  No GPU resources are owned
 * by this system.
 */

#include "qs_light.h"
#include "qs_math.h"
#include "qs_system.h"
#include "qs_log.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define QS_MAX_LIGHTS 256

struct Qs_Light {
    char         name[64];
    bool         in_use;
    bool         enabled;
    Qs_LightType type;
    float        position[3];
    float        direction[3];
    float        color[3];
    float        intensity;
    float        range;
    float        inner_cone_cos;
    float        outer_cone_cos;
    bool         cast_shadows;
};

typedef struct {
    Qs_Light lights[QS_MAX_LIGHTS];
    uint32_t count;
} LightSystemData;

static LightSystemData *g_light_sys;

/* ================================================================
   SYSTEM LIFECYCLE
   ================================================================ */

static bool light_sys_init(Qs_System *sys, Qs_Engine *engine)
{
    (void)engine;
    g_light_sys = (LightSystemData *)qs_system_data(sys);
    QS_LOG_INFO("Light system initialised");
    return true;
}

static void light_sys_shutdown(Qs_System *sys, Qs_Engine *engine)
{
    (void)sys;
    (void)engine;
    g_light_sys = NULL;
    QS_LOG_INFO("Light system shut down");
}

Qs_SystemDesc qs_light_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Light",
        .data_size = sizeof(LightSystemData),
        .init      = light_sys_init,
        .shutdown  = light_sys_shutdown,
        .update    = NULL,
    };
}

/* ================================================================
   PUBLIC API
   ================================================================ */

Qs_Light *qs_light_create(Qs_Engine *engine, const Qs_LightDesc *desc)
{
    (void)engine;
    if (!g_light_sys || !desc) return NULL;

    Qs_Light *l = NULL;
    for (uint32_t i = 0; i < QS_MAX_LIGHTS; i++) {
        if (!g_light_sys->lights[i].in_use) { l = &g_light_sys->lights[i]; break; }
    }
    if (!l) {
        QS_LOG_ERROR("Light system: light limit reached (%d)", QS_MAX_LIGHTS);
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

    l->color[0] = desc->color[0];
    l->color[1] = desc->color[1];
    l->color[2] = desc->color[2];

    l->intensity    = desc->intensity;
    l->range        = desc->range;
    l->cast_shadows = desc->cast_shadows;

    l->inner_cone_cos = cosf(qs_to_rad(desc->inner_cone_deg));
    l->outer_cone_cos = cosf(qs_to_rad(desc->outer_cone_deg));

    /* Ensure direction is never a zero vector */
    if (l->direction[0] == 0.0f && l->direction[1] == 0.0f && l->direction[2] == 0.0f)
        l->direction[1] = -1.0f;

    if (desc->name) snprintf(l->name, sizeof(l->name), "%s", desc->name);
    else            snprintf(l->name, sizeof(l->name), "light_%u", g_light_sys->count);

    g_light_sys->count++;
    QS_LOG_INFO("Light system: '%s' created (type=%d)", l->name, l->type);
    return l;
}

void qs_light_destroy(Qs_Light *light)
{
    if (!light || !light->in_use) return;
    QS_LOG_INFO("Light system: '%s' destroyed", light->name);
    light->in_use = false;
    if (g_light_sys && g_light_sys->count > 0)
        g_light_sys->count--;
}

const char *qs_light_name     (const Qs_Light *l) { return l ? l->name      : NULL; }
float      *qs_light_position (Qs_Light *l)        { return l ? l->position  : NULL; }
float      *qs_light_direction(Qs_Light *l)        { return l ? l->direction : NULL; }
float      *qs_light_color    (Qs_Light *l)        { return l ? l->color     : NULL; }
float       qs_light_intensity(const Qs_Light *l)  { return l ? l->intensity : 0.0f; }

void qs_light_set_intensity(Qs_Light *l, float v) { if (l) l->intensity = v; }
void qs_light_set_range    (Qs_Light *l, float v) { if (l) l->range     = v; }
void qs_light_set_enabled  (Qs_Light *l, bool  v) { if (l) l->enabled   = v; }
bool qs_light_enabled      (const Qs_Light *l)    { return l ? l->enabled : false; }

void qs_light_set_cone(Qs_Light *l, float inner_deg, float outer_deg)
{
    if (!l) return;
    l->inner_cone_cos = cosf(qs_to_rad(inner_deg));
    l->outer_cone_cos = cosf(qs_to_rad(outer_deg));
}

bool qs_light_is_active(const Qs_Light *l)
{
    return l && l->in_use && l->enabled;
}

void qs_light_pack_gpu(const Qs_Light *l, Qs_LightGPU *out)
{
    if (!l || !out) return;
    out->position[0]    = l->position[0];
    out->position[1]    = l->position[1];
    out->position[2]    = l->position[2];
    out->range          = l->range;
    out->direction[0]   = l->direction[0];
    out->direction[1]   = l->direction[1];
    out->direction[2]   = l->direction[2];
    out->intensity      = l->intensity;
    out->color[0]       = l->color[0];
    out->color[1]       = l->color[1];
    out->color[2]       = l->color[2];
    out->inner_cone_cos = l->inner_cone_cos;
    out->outer_cone_cos = l->outer_cone_cos;
    out->type           = (uint32_t)l->type;
    out->cast_shadows   = l->cast_shadows ? 1 : 0;
    out->_pad           = 0;
}
