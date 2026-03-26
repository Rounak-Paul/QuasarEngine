#ifndef QS_TEXTURE_H
#define QS_TEXTURE_H

#include <vulkan/vulkan.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct Qs_Engine       Qs_Engine;
typedef struct Ca_Instance     Ca_Instance;
typedef struct Qs_Texture      Qs_Texture;     ///< Opaque â€” defined by the texture backend.

/* ================================================================
   TEXTURE FORMAT
   ================================================================ */

/// Pixel format for texture data.
typedef enum Qs_TextureFormat {
    QS_TEXTURE_FORMAT_RGBA8_UNORM  = 0,   ///< 4 bytes per pixel, linear.
    QS_TEXTURE_FORMAT_RGBA8_SRGB   = 1,   ///< 4 bytes per pixel, sRGB.
    QS_TEXTURE_FORMAT_RG8_UNORM    = 2,   ///< 2 bytes per pixel (metallic-roughness).
    QS_TEXTURE_FORMAT_R8_UNORM     = 3,   ///< 1 byte per pixel (AO, alpha).
    QS_TEXTURE_FORMAT_RGBA16_SFLOAT = 4,  ///< 8 bytes per pixel, HDR.
} Qs_TextureFormat;

/// Addressing mode for texture coordinates outside [0,1].
typedef enum Qs_TextureWrap {
    QS_TEXTURE_WRAP_REPEAT          = 0,
    QS_TEXTURE_WRAP_MIRRORED_REPEAT = 1,
    QS_TEXTURE_WRAP_CLAMP_TO_EDGE   = 2,
} Qs_TextureWrap;

/// Filtering mode for texture sampling.
typedef enum Qs_TextureFilter {
    QS_TEXTURE_FILTER_NEAREST = 0,
    QS_TEXTURE_FILTER_LINEAR  = 1,
} Qs_TextureFilter;

/* ================================================================
   TEXTURE DESCRIPTOR
   ================================================================ */

/// Configuration for creating a GPU texture.
typedef struct Qs_TextureDesc {
    const char       *name;           ///< Debug label.
    uint32_t          width;
    uint32_t          height;
    Qs_TextureFormat  format;         ///< Pixel format (default: RGBA8_UNORM).
    const void       *pixels;         ///< Pixel data to upload (may be NULL for empty texture).
    bool              generate_mips;  ///< Generate a full mip chain.
    Qs_TextureFilter  min_filter;     ///< Minification filter (default: LINEAR).
    Qs_TextureFilter  mag_filter;     ///< Magnification filter (default: LINEAR).
    Qs_TextureWrap    wrap_u;         ///< U-axis wrap mode (default: REPEAT).
    Qs_TextureWrap    wrap_v;         ///< V-axis wrap mode (default: REPEAT).
} Qs_TextureDesc;

/* ================================================================
   TEXTURE BACKEND
   ================================================================ */

typedef struct Qs_TextureBackend {
    const char *name;

    /// Set up descriptor pools and default textures.  VkDevice is obtained
    /// via Ca_Instance (the render system will already be initialised).
    bool (*init)(Ca_Instance *ca, void **out_ctx);

    /// Destroy all textures and release ctx.
    void (*shutdown)(void *ctx);

    /// Create a GPU-side texture from the given descriptor.
    Qs_Texture *(*create)(void *ctx, Qs_Engine *engine, const Qs_TextureDesc *desc);

    /// Destroy an individual texture.
    void        (*destroy)(void *ctx, Qs_Texture *texture);

    /* Accessors */
    const char  *(*tex_name)(const Qs_Texture *texture);
    VkImageView  (*image_view)(const Qs_Texture *texture);
    VkSampler    (*sampler)(const Qs_Texture *texture);
    void         (*extents)(const Qs_Texture *texture,
                            uint32_t *out_w, uint32_t *out_h);
    uint32_t     (*mip_levels)(const Qs_Texture *texture);
} Qs_TextureBackend;

/// Registers the texture backend.  Must be called before the Texture
/// system initialises (i.e. in the pluginâ€™s on_load callback).
void qs_texture_backend_register(const Qs_TextureBackend *backend);

/* ================================================================
   PUBLIC TEXTURE API
   ================================================================ */

/// Creates a GPU texture.  Destroy with qs_texture_destroy.
Qs_Texture *qs_texture_create(Qs_Engine *engine, const Qs_TextureDesc *desc);

/// Destroys a texture and frees its GPU resources.
void qs_texture_destroy(Qs_Texture *texture);

/// Returns the debug name.
const char *qs_texture_name(const Qs_Texture *texture);

/// Returns the VkImageView for shader binding.
VkImageView qs_texture_image_view(const Qs_Texture *texture);

/// Returns the VkSampler for shader binding.
VkSampler qs_texture_sampler(const Qs_Texture *texture);

/// Returns the texture dimensions.
void qs_texture_extents(const Qs_Texture *texture,
                        uint32_t *out_width, uint32_t *out_height);

/// Returns the mip level count.
uint32_t qs_texture_mip_levels(const Qs_Texture *texture);

#endif