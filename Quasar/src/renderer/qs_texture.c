#include "qs_texture.h"
#include "qs_system.h"
#include "qs_log.h"

#include <stddef.h>

/* ================================================================
   REGISTERED BACKEND
   ================================================================ */

static const Qs_TextureBackend *g_texture_backend;
static void                    *g_texture_ctx;

void qs_texture_backend_register(const Qs_TextureBackend *backend)
{
    g_texture_backend = backend;
}

/* ================================================================
   ENGINE SYSTEM
   ================================================================ */

typedef struct { void *ctx; } Qs_TextureSystemState;

static bool texture_sys_init(Qs_System *sys, Qs_Engine *engine)
{
    (void)engine;
    Qs_TextureSystemState *state = (Qs_TextureSystemState *)qs_system_data(sys);
    if (!g_texture_backend) {
        QS_LOG_ERROR("Texture system: no backend registered");
        return false;
    }
    /* Ca_Instance comes from the causality instance already in the engine.
       The render backend init runs first and sets up VkDevice; the texture
       backend acquires it from Ca_Instance internally. */
    typedef struct Ca_Instance Ca_Instance;
    Ca_Instance *qs_engine_ca_instance(Qs_Engine *);
    Ca_Instance *ca = qs_engine_ca_instance(engine);
    if (!g_texture_backend->init(ca, &state->ctx)) {
        QS_LOG_ERROR("Texture backend '%s' init failed", g_texture_backend->name);
        return false;
    }
    g_texture_ctx = state->ctx;
    QS_LOG_INFO("Texture backend '%s' initialised", g_texture_backend->name);
    return true;
}

static void texture_sys_shutdown(Qs_System *sys, Qs_Engine *engine)
{
    (void)engine;
    Qs_TextureSystemState *state = (Qs_TextureSystemState *)qs_system_data(sys);
    if (g_texture_backend && state->ctx)
        g_texture_backend->shutdown(state->ctx);
    g_texture_ctx = NULL;
    QS_LOG_INFO("Texture backend shut down");
}

/* Internal — called from engine.c. */
Qs_SystemDesc qs_texture_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Texture",
        .data_size = sizeof(Qs_TextureSystemState),
        .init      = texture_sys_init,
        .shutdown  = texture_sys_shutdown,
        .update    = NULL,
    };
}

/* ================================================================
   PUBLIC API DISPATCHERS
   ================================================================ */

Qs_Texture *qs_texture_create(Qs_Engine *engine, const Qs_TextureDesc *desc)
{
    if (!g_texture_backend || !g_texture_backend->create) return NULL;
    return g_texture_backend->create(g_texture_ctx, engine, desc);
}

void qs_texture_destroy(Qs_Texture *texture)
{
    if (!texture || !g_texture_backend || !g_texture_backend->destroy) return;
    g_texture_backend->destroy(g_texture_ctx, texture);
}

const char *qs_texture_name(const Qs_Texture *texture)
{
    if (!texture || !g_texture_backend || !g_texture_backend->tex_name) return NULL;
    return g_texture_backend->tex_name(texture);
}

VkImageView qs_texture_image_view(const Qs_Texture *texture)
{
    if (!texture || !g_texture_backend || !g_texture_backend->image_view) return VK_NULL_HANDLE;
    return g_texture_backend->image_view(texture);
}

VkSampler qs_texture_sampler(const Qs_Texture *texture)
{
    if (!texture || !g_texture_backend || !g_texture_backend->sampler) return VK_NULL_HANDLE;
    return g_texture_backend->sampler(texture);
}

void qs_texture_extents(const Qs_Texture *texture,
                        uint32_t *out_width, uint32_t *out_height)
{
    if (out_width)  *out_width  = 0;
    if (out_height) *out_height = 0;
    if (!texture || !g_texture_backend || !g_texture_backend->extents) return;
    g_texture_backend->extents(texture, out_width, out_height);
}

uint32_t qs_texture_mip_levels(const Qs_Texture *texture)
{
    if (!texture || !g_texture_backend || !g_texture_backend->mip_levels) return 0;
    return g_texture_backend->mip_levels(texture);
}
