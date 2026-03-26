#include "qs_light.h"
#include "qs_log.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define QS_MAX_LIGHTS 256

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

typedef struct {
    Qs_Light  lights[QS_MAX_LIGHTS];
    uint32_t  count;
} VkLightSystemData;

static VkLightSystemData *g_light_system;

/* ================================================================
   INTER-MODULE HELPERS  (used by vk_renderer.c)
   ================================================================ */

bool vk_light_is_active(const Qs_Light *l)
{
    return l && l->in_use && l->enabled;
}

void vk_light_pack_gpu(const Qs_Light *l, Qs_LightGPU *out)
{
    out->position[0]  = l->position[0];
    out->position[1]  = l->position[1];
    out->position[2]  = l->position[2];
    out->range        = l->range;
    out->direction[0] = l->direction[0];
    out->direction[1] = l->direction[1];
    out->direction[2] = l->direction[2];
    out->intensity    = l->intensity;
    out->color[0]     = l->color[0];
    out->color[1]     = l->color[1];
    out->color[2]     = l->color[2];
    out->inner_cone_cos = l->inner_cone_cos;
    out->outer_cone_cos = l->outer_cone_cos;
    out->type         = (uint32_t)l->type;
    out->cast_shadows = l->cast_shadows ? 1 : 0;
    out->_pad         = 0;
}

/* ================================================================
   BACKEND LIFECYCLE
   ================================================================ */

static bool vk_light_init(void **out_ctx)
{
    VkLightSystemData *data = calloc(1, sizeof(VkLightSystemData));
    if (!data) return false;
    g_light_system = data;
    *out_ctx = data;
    QS_LOG_INFO("VkLight: light system initialised");
    return true;
}

static void vk_light_shutdown(void *ctx)
{
    VkLightSystemData *data = ctx;
    for (uint32_t i = 0; i < QS_MAX_LIGHTS; i++) {
        if (data->lights[i].in_use) {
            data->lights[i].in_use = false;
        }
    }
    g_light_system = NULL;
    free(data);
    QS_LOG_INFO("VkLight: light system shut down");
}

/* ================================================================
   LIGHT LIFECYCLE
   ================================================================ */

static float deg_to_rad(float deg) { return deg * 3.14159265f / 180.0f; }

static Qs_Light *vk_light_create(void *ctx, Qs_Engine *engine, const Qs_LightDesc *desc)
{
    (void)engine;
    VkLightSystemData *sys = ctx;
    if (!sys || !desc) return NULL;

    Qs_Light *l = NULL;
    for (uint32_t i = 0; i < QS_MAX_LIGHTS; i++) {
        if (!sys->lights[i].in_use) {
            l = &sys->lights[i];
            break;
        }
    }
    if (!l) {
        QS_LOG_ERROR("VkLight: light limit reached (%d)", QS_MAX_LIGHTS);
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

    l->intensity      = desc->intensity != 0.0f ? desc->intensity : 1.0f;
    l->range          = desc->range;
    l->cast_shadows   = desc->cast_shadows;

    float inner = desc->inner_cone_deg > 0.0f ? desc->inner_cone_deg : 30.0f;
    float outer = desc->outer_cone_deg > 0.0f ? desc->outer_cone_deg : 45.0f;
    l->inner_cone_cos = cosf(deg_to_rad(inner));
    l->outer_cone_cos = cosf(deg_to_rad(outer));

    if (l->direction[0] == 0.0f && l->direction[1] == 0.0f && l->direction[2] == 0.0f)
        l->direction[1] = -1.0f;

    if (desc->name)
        snprintf(l->name, sizeof(l->name), "%s", desc->name);
    else
        snprintf(l->name, sizeof(l->name), "light_%u", sys->count);

    sys->count++;
    QS_LOG_INFO("VkLight: '%s' created (type=%d)", l->name, l->type);
    return l;
}

static void vk_light_destroy(void *ctx, Qs_Light *light)
{
    VkLightSystemData *sys = ctx;
    if (!light || !light->in_use) return;
    QS_LOG_INFO("VkLight: '%s' destroyed", light->name);
    light->in_use = false;
    if (sys && sys->count > 0) sys->count--;
}

/* ================================================================
   ACCESSORS
   ================================================================ */

static const char *vk_light_name(const Qs_Light *l) { return l ? l->name : NULL; }
static float *vk_light_position(Qs_Light *l)  { return l ? l->position  : NULL; }
static float *vk_light_direction(Qs_Light *l) { return l ? l->direction : NULL; }
static float *vk_light_color(Qs_Light *l)     { return l ? l->color     : NULL; }

static void  vk_light_set_intensity(Qs_Light *l, float v) { if (l) l->intensity = v; }
static float vk_light_intensity(const Qs_Light *l) { return l ? l->intensity : 0.0f; }
static void  vk_light_set_range(Qs_Light *l, float v) { if (l) l->range = v; }

static void vk_light_set_cone(Qs_Light *l, float inner, float outer)
{
    if (!l) return;
    l->inner_cone_cos = cosf(deg_to_rad(inner));
    l->outer_cone_cos = cosf(deg_to_rad(outer));
}

static void vk_light_set_enabled(Qs_Light *l, bool v) { if (l) l->enabled = v; }
static bool vk_light_enabled(const Qs_Light *l) { return l ? l->enabled : false; }

/* ================================================================
   BACKEND STRUCT
   ================================================================ */

const Qs_LightBackend vk_light_backend = {
    .name          = "Vulkan/PBR",
    .init          = vk_light_init,
    .shutdown      = vk_light_shutdown,
    .create        = vk_light_create,
    .destroy       = vk_light_destroy,
    .light_name    = vk_light_name,
    .position      = vk_light_position,
    .direction     = vk_light_direction,
    .color         = vk_light_color,
    .set_intensity = vk_light_set_intensity,
    .intensity     = vk_light_intensity,
    .set_range     = vk_light_set_range,
    .set_cone      = vk_light_set_cone,
    .set_enabled   = vk_light_set_enabled,
    .enabled       = vk_light_enabled,
};
