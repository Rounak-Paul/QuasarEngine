/*
 * vendor_impl.c — Single-compilation-unit definitions for cgltf and stb_image.
 *
 * These single-header libraries require exactly one translation unit that
 * defines their implementation macros.  Isolating them here keeps gltf_loader.c
 * clean and prevents duplicate-symbol conflicts if other plugins include stb.
 */

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_SIMD
#include "stb_image.h"
