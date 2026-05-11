#ifndef QS_TEXTURE_H
#define QS_TEXTURE_H

#include "qs_gpu.h"
#include "qs_api.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct Qs_Engine       Qs_Engine;
typedef struct Qs_Texture      Qs_Texture;     ///< Opaque — defined by the texture backend.

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
   PUBLIC TEXTURE API
   ================================================================ */

/// Creates a GPU texture.  Destroy with qs_texture_destroy.
QS_API Qs_Texture *qs_texture_create(Qs_Engine *engine, const Qs_TextureDesc *desc);

/// Destroys a texture and frees its GPU resources.
QS_API void qs_texture_destroy(Qs_Texture *texture);

/// Returns the debug name.
QS_API const char *qs_texture_name(const Qs_Texture *texture);

/// Returns the image view for shader binding.
QS_API Qs_GpuImageView *qs_texture_image_view(const Qs_Texture *texture);

/// Returns the sampler for shader binding.
QS_API Qs_GpuSampler *qs_texture_sampler(const Qs_Texture *texture);

/// Returns the texture dimensions.
QS_API void qs_texture_extents(const Qs_Texture *texture,
                        uint32_t *out_width, uint32_t *out_height);

/// Returns the mip level count.
QS_API uint32_t qs_texture_mip_levels(const Qs_Texture *texture);

/// Returns the number of live (in-use) textures managed by the texture system.
QS_API uint32_t qs_texture_count(void);

/// Returns the i-th live texture (0-based, dense order).
/// Returns NULL if index >= qs_texture_count().
QS_API Qs_Texture *qs_texture_at(uint32_t index);

#endif