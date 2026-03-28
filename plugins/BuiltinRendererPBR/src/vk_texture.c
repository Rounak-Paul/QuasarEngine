#include "qs_texture.h"
#include "qs_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define QS_MAX_TEXTURES 512

struct Qs_Texture {
    char              name[64];
    bool              in_use;

    Qs_GpuContext    *gpu;
    Qs_GpuImage      *image;
    Qs_GpuImageView  *view;
    Qs_GpuSampler    *sampler;

    uint32_t          width;
    uint32_t          height;
    uint32_t          mip_levels;
};

typedef struct {
    Qs_GpuContext    *gpu;
    Qs_Texture        textures[QS_MAX_TEXTURES];
    uint32_t          count;
} VkTextureSystemData;

static VkTextureSystemData *g_texture_system;

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
   BACKEND LIFECYCLE
   ================================================================ */

static bool vk_texture_init(Qs_GpuContext *gpu, void **out_ctx)
{
    VkTextureSystemData *data = calloc(1, sizeof(VkTextureSystemData));
    if (!data) return false;
    data->gpu = gpu;
    g_texture_system = data;
    *out_ctx = data;
    QS_LOG_INFO("VkTexture: texture system initialised");
    return true;
}

/* Forward declare so vk_texture_create can call it on failure */
static void vk_texture_destroy(void *ctx, Qs_Texture *texture);

static void vk_texture_shutdown(void *ctx)
{
    VkTextureSystemData *data = ctx;
    for (uint32_t i = 0; i < QS_MAX_TEXTURES; i++) {
        if (data->textures[i].in_use)
            vk_texture_destroy(ctx, &data->textures[i]);
    }
    g_texture_system = NULL;
    free(data);
    QS_LOG_INFO("VkTexture: texture system shut down");
}

/* ================================================================
   TEXTURE LIFECYCLE
   ================================================================ */

static Qs_Texture *vk_texture_create(void *ctx, Qs_Engine *engine,
                                      const Qs_TextureDesc *desc)
{
    (void)engine;
    VkTextureSystemData *sys = ctx;
    if (!sys || !desc || desc->width == 0 || desc->height == 0) return NULL;

    Qs_Texture *t = NULL;
    for (uint32_t i = 0; i < QS_MAX_TEXTURES; i++) {
        if (!sys->textures[i].in_use) { t = &sys->textures[i]; break; }
    }
    if (!t) {
        QS_LOG_ERROR("VkTexture: texture limit reached (%d)", QS_MAX_TEXTURES);
        return NULL;
    }

    memset(t, 0, sizeof(*t));
    t->in_use  = true;
    t->gpu     = sys->gpu;
    t->width   = desc->width;
    t->height  = desc->height;

    if (desc->name) snprintf(t->name, sizeof(t->name), "%s", desc->name);
    else            snprintf(t->name, sizeof(t->name), "texture_%u", sys->count);

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
        QS_LOG_ERROR("VkTexture: failed to create image for '%s'", t->name);
        t->in_use = false; return NULL;
    }

    if (desc->pixels) {
        uint64_t data_size = (uint64_t)(desc->width * desc->height * bytes_per_pixel(desc->format));
        if (!qs_gpu_upload_image(t->gpu, t->image, desc->pixels, data_size, desc->generate_mips)) {
            QS_LOG_ERROR("VkTexture: failed to upload pixels for '%s'", t->name);
            vk_texture_destroy(ctx, t); return NULL;
        }
    }

    t->view = qs_gpu_create_image_view_for(t->gpu, t->image, QS_GPU_IMAGE_ASPECT_COLOR);
    if (!t->view) {
        QS_LOG_ERROR("VkTexture: failed to create image view for '%s'", t->name);
        vk_texture_destroy(ctx, t); return NULL;
    }

    /* Filter and wrap enums share the same numeric values as the engine GPU enums */
    t->sampler = qs_gpu_create_sampler(t->gpu, &(Qs_GpuSamplerDesc){
        .min_filter = (Qs_GpuFilter)desc->min_filter,
        .mag_filter = (Qs_GpuFilter)desc->mag_filter,
        .wrap_u     = (Qs_GpuWrap)desc->wrap_u,
        .wrap_v     = (Qs_GpuWrap)desc->wrap_v,
        .mip_levels = t->mip_levels,
        .anisotropy = true,
    });
    if (!t->sampler) {
        QS_LOG_ERROR("VkTexture: failed to create sampler for '%s'", t->name);
        vk_texture_destroy(ctx, t); return NULL;
    }

    sys->count++;
    QS_LOG_INFO("VkTexture: '%s' created (%ux%u, %u mips)", t->name, t->width, t->height, t->mip_levels);
    return t;
}

static void vk_texture_destroy(void *ctx, Qs_Texture *texture)
{
    VkTextureSystemData *sys = ctx;
    if (!texture || !texture->in_use) return;
    if (texture->sampler) { qs_gpu_destroy_sampler(texture->gpu, texture->sampler);    texture->sampler = NULL; }
    if (texture->view)    { qs_gpu_destroy_image_view(texture->gpu, texture->view);    texture->view    = NULL; }
    if (texture->image)   { qs_gpu_destroy_image(texture->gpu, texture->image);        texture->image   = NULL; }
    QS_LOG_INFO("VkTexture: '%s' destroyed", texture->name);
    texture->in_use = false;
    if (sys && sys->count > 0) sys->count--;
}

/* ================================================================
   ACCESSORS
   ================================================================ */

static const char      *vk_tex_name(const Qs_Texture *t) { return t ? t->name    : NULL; }
static Qs_GpuImageView *vk_tex_view(const Qs_Texture *t) { return t ? t->view    : NULL; }
static Qs_GpuSampler   *vk_tex_samp(const Qs_Texture *t) { return t ? t->sampler : NULL; }

static void vk_tex_extents(const Qs_Texture *t, uint32_t *w, uint32_t *h)
{
    if (w) *w = t ? t->width  : 0;
    if (h) *h = t ? t->height : 0;
}

static uint32_t vk_tex_mips(const Qs_Texture *t) { return t ? t->mip_levels : 0; }

/* ================================================================
   BACKEND STRUCT
   ================================================================ */

const Qs_TextureBackend vk_texture_backend = {
    .name       = "Vulkan/PBR",
    .init       = vk_texture_init,
    .shutdown   = vk_texture_shutdown,
    .create     = vk_texture_create,
    .destroy    = vk_texture_destroy,
    .tex_name   = vk_tex_name,
    .image_view = vk_tex_view,
    .sampler    = vk_tex_samp,
    .extents    = vk_tex_extents,
    .mip_levels = vk_tex_mips,
};

#include <stdio.h>
