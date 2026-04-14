/*
 * qs_texture_system.c — Engine texture system.
 *
 * Manages GPU texture resources (images, image views, samplers) as a
 * first-class engine system.  No backend indirection — Causality always
 * uses Vulkan.
 */

#include "qs_texture.h"
#include "qs_system.h"
#include "qs_gpu.h"
#include "qs_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define QS_MAX_TEXTURES 512

struct Qs_Texture {
    char             name[64];
    bool             in_use;
    Qs_GpuContext   *gpu;
    Qs_GpuImage     *image;
    Qs_GpuImageView *view;
    Qs_GpuSampler   *sampler;
    uint32_t         width;
    uint32_t         height;
    uint32_t         mip_levels;
};

typedef struct {
    Qs_GpuContext *gpu;
    Qs_Texture     textures[QS_MAX_TEXTURES];
    uint32_t       count;
} TextureSystemData;

static TextureSystemData *g_texture_sys;

/* ================================================================
   FORMAT HELPERS
   ================================================================ */

static Qs_GpuImageFormat texture_to_gpu_format(Qs_TextureFormat fmt)
{
    switch (fmt) {
    case QS_TEXTURE_FORMAT_RGBA8_SRGB:    return QS_GPU_FORMAT_RGBA8_SRGB;
    case QS_TEXTURE_FORMAT_RG8_UNORM:     return QS_GPU_FORMAT_RG8_UNORM;
    case QS_TEXTURE_FORMAT_R8_UNORM:      return QS_GPU_FORMAT_R8_UNORM;
    case QS_TEXTURE_FORMAT_RGBA16_SFLOAT: return QS_GPU_FORMAT_RGBA16_SFLOAT;
    default:                              return QS_GPU_FORMAT_RGBA8_UNORM;
    }
}

static uint32_t bytes_per_pixel(Qs_TextureFormat fmt)
{
    switch (fmt) {
    case QS_TEXTURE_FORMAT_RG8_UNORM:     return 2;
    case QS_TEXTURE_FORMAT_R8_UNORM:      return 1;
    case QS_TEXTURE_FORMAT_RGBA16_SFLOAT: return 8;
    default:                              return 4;
    }
}

static uint32_t compute_mip_levels(uint32_t w, uint32_t h)
{
    uint32_t levels = 1, dim = w > h ? w : h;
    while (dim > 1) { dim >>= 1; levels++; }
    return levels;
}

/* ================================================================
   SYSTEM LIFECYCLE
   ================================================================ */

static void texture_destroy_one(Qs_Texture *t)
{
    if (!t || !t->in_use) return;
    if (t->sampler) { qs_gpu_destroy_sampler   (t->gpu, t->sampler); t->sampler = NULL; }
    if (t->view)    { qs_gpu_destroy_image_view(t->gpu, t->view);    t->view    = NULL; }
    if (t->image)   { qs_gpu_destroy_image     (t->gpu, t->image);   t->image   = NULL; }
    t->in_use = false;
}

static bool texture_sys_init(Qs_System *sys, Qs_Engine *engine)
{
    TextureSystemData *data = (TextureSystemData *)qs_system_data(sys);
    data->gpu     = qs_engine_gpu(engine);
    g_texture_sys = data;
    QS_LOG_INFO("Texture system initialised");
    return true;
}

static void texture_sys_shutdown(Qs_System *sys, Qs_Engine *engine)
{
    (void)engine;
    TextureSystemData *data = (TextureSystemData *)qs_system_data(sys);
    for (uint32_t i = 0; i < QS_MAX_TEXTURES; i++)
        texture_destroy_one(&data->textures[i]);
    g_texture_sys = NULL;
    QS_LOG_INFO("Texture system shut down");
}

Qs_SystemDesc qs_texture_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Texture",
        .data_size = sizeof(TextureSystemData),
        .init      = texture_sys_init,
        .shutdown  = texture_sys_shutdown,
        .update    = NULL,
    };
}

/* ================================================================
   PUBLIC API
   ================================================================ */

Qs_Texture *qs_texture_create(Qs_Engine *engine, const Qs_TextureDesc *desc)
{
    (void)engine;
    if (!g_texture_sys || !desc || desc->width == 0 || desc->height == 0) return NULL;

    Qs_Texture *t = NULL;
    for (uint32_t i = 0; i < QS_MAX_TEXTURES; i++) {
        if (!g_texture_sys->textures[i].in_use) { t = &g_texture_sys->textures[i]; break; }
    }
    if (!t) {
        QS_LOG_ERROR("Texture system: texture limit reached (%d)", QS_MAX_TEXTURES);
        return NULL;
    }

    memset(t, 0, sizeof(*t));
    t->in_use  = true;
    t->gpu     = g_texture_sys->gpu;
    t->width   = desc->width;
    t->height  = desc->height;

    if (desc->name) snprintf(t->name, sizeof(t->name), "%s", desc->name);
    else            snprintf(t->name, sizeof(t->name), "texture_%u", g_texture_sys->count);

    t->mip_levels = desc->generate_mips ? compute_mip_levels(desc->width, desc->height) : 1;

    Qs_GpuImageFormat fmt   = texture_to_gpu_format(desc->format);
    Qs_GpuImageUsage  usage = (Qs_GpuImageUsage)(QS_GPU_IMAGE_TRANSFER_DST | QS_GPU_IMAGE_SAMPLED);
    if (t->mip_levels > 1)
        usage = (Qs_GpuImageUsage)(usage | QS_GPU_IMAGE_TRANSFER_SRC);

    t->image = qs_gpu_create_image(t->gpu, &(Qs_GpuImageDesc){
        .width      = desc->width,
        .height     = desc->height,
        .mip_levels = t->mip_levels,
        .format     = fmt,
        .usage      = usage,
    });
    if (!t->image) {
        QS_LOG_ERROR("Texture system: failed to create image for '%s'", t->name);
        t->in_use = false;
        return NULL;
    }

    if (desc->pixels) {
        const uint64_t data_size = (uint64_t)desc->width * desc->height
                                   * bytes_per_pixel(desc->format);
        if (!qs_gpu_upload_image(t->gpu, t->image, desc->pixels, data_size, desc->generate_mips)) {
            QS_LOG_ERROR("Texture system: failed to upload pixels for '%s'", t->name);
            texture_destroy_one(t);
            return NULL;
        }
    }

    t->view = qs_gpu_create_image_view_for(t->gpu, t->image, QS_GPU_IMAGE_ASPECT_COLOR);
    if (!t->view) {
        QS_LOG_ERROR("Texture system: failed to create image view for '%s'", t->name);
        texture_destroy_one(t);
        return NULL;
    }

    t->sampler = qs_gpu_create_sampler(t->gpu, &(Qs_GpuSamplerDesc){
        .min_filter = (Qs_GpuFilter)desc->min_filter,
        .mag_filter = (Qs_GpuFilter)desc->mag_filter,
        .wrap_u     = (Qs_GpuWrap)desc->wrap_u,
        .wrap_v     = (Qs_GpuWrap)desc->wrap_v,
        .mip_levels = t->mip_levels,
        .anisotropy = true,
    });
    if (!t->sampler) {
        QS_LOG_ERROR("Texture system: failed to create sampler for '%s'", t->name);
        texture_destroy_one(t);
        return NULL;
    }

    g_texture_sys->count++;
    QS_LOG_INFO("Texture system: '%s' created (%ux%u, %u mips)",
                t->name, t->width, t->height, t->mip_levels);
    return t;
}

void qs_texture_destroy(Qs_Texture *texture)
{
    if (!texture || !texture->in_use) return;
    QS_LOG_INFO("Texture system: '%s' destroyed", texture->name);
    texture_destroy_one(texture);
    if (g_texture_sys && g_texture_sys->count > 0)
        g_texture_sys->count--;
}

const char      *qs_texture_name      (const Qs_Texture *t) { return t ? t->name       : NULL; }
Qs_GpuImageView *qs_texture_image_view(const Qs_Texture *t) { return t ? t->view       : NULL; }
Qs_GpuSampler   *qs_texture_sampler   (const Qs_Texture *t) { return t ? t->sampler    : NULL; }
uint32_t         qs_texture_mip_levels(const Qs_Texture *t) { return t ? t->mip_levels : 0; }

void qs_texture_extents(const Qs_Texture *t, uint32_t *out_w, uint32_t *out_h)
{
    if (out_w) *out_w = t ? t->width  : 0;
    if (out_h) *out_h = t ? t->height : 0;
}
