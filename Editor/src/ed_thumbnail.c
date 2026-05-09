/*
 * ed_thumbnail.c — Async GPU-backed asset thumbnail service.
 *
 * Architecture
 * ------------
 * Each thumbnail request goes through a background job (the engine job
 * system) that does all CPU-heavy work off the main thread:
 *
 *   • .qstex — file read + bilinear downsample → RGBA8 cpu_pixels buffer.
 *   • .qsmesh — file read + AABB camera / push-constant computation.
 *
 * Once cpu_done is set (atomic), the main thread performs the final GPU
 * step on its next pass:
 *
 *   • Texture: ca_image_create from cpu_pixels (fast staging copy).
 *   • Mesh: full offscreen Lambert render + readback via a dedicated
 *     GPU pipeline (gpu_render_mesh).  Throttled to at most
 *     THUMB_MESH_GPU_PER_FRAME per frame to avoid stalling the UI when
 *     a folder of new meshes first enters thumbnail view.
 *
 * Call ed_thumbnail_begin_frame() once per frame (before any get() calls)
 * to reset the per-frame mesh GPU budget.
 *
 * Cache
 * -----
 * LRU, THUMB_CACHE_MAX (256) slots.  In-flight entries are never evicted.
 * Entries are invalidated when the file mtime changes.  ed_thumbnail_flush()
 * destroys all idle cached images (useful on project switch).
 */

#include "ed_thumbnail.h"
#include "qs_gpu.h"
#include "qs_math.h"
#include "qs_asset_pack.h"
#include "qs_mesh.h"
#include "qs_log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#ifndef _WIN32
    #include <sched.h>
#endif

/* ================================================================
   Constants
   ================================================================ */

#define THUMB_RENDER_SIZE        128   /* Offscreen render resolution (px)  */
#define THUMB_CACHE_MAX          256   /* Maximum LRU cache entries          */
#define THUMB_MAX_PATH          1024   /* Maximum absolute path length       */
#define THUMB_MESH_GPU_PER_FRAME   2   /* Max mesh GPU renders per frame     */

/* ================================================================
   Async thumbnail state machine
   ================================================================ */

typedef enum {
    THUMB_STATE_IDLE    = 0, /* not yet requested               */
    THUMB_STATE_LOADING,     /* background job dispatched       */
    THUMB_STATE_READY,       /* CPU data ready; awaiting GPU    */
    THUMB_STATE_DONE,        /* Ca_Image* available             */
    THUMB_STATE_FAILED,      /* error; do not retry             */
} ThumbState;

/* ================================================================
   LRU cache entry
   ================================================================ */

typedef struct ThumbEntry {
    bool        used;
    ThumbState  state;
    char        abs_path[THUMB_MAX_PATH];
    int64_t     mtime_sec;
    uint64_t    tick;            /* LRU timestamp                     */
    Ca_Image   *image;
    /* Written by background job; read by main thread after cpu_done. */
    atomic_bool  cpu_done;
    uint8_t     *cpu_pixels;    /* RGBA8 THUMB_RENDER_SIZE² (texture) */
    Qs_Vertex   *mesh_verts;    /* raw geometry (mesh)               */
    uint32_t    *mesh_idx;
    uint32_t     mesh_vert_count;
    uint32_t     mesh_idx_count;
    /* Push-constant block for mesh Lambert preview (80 bytes). */
    struct {
        float mvp[16];
        float light_dir[3];
        float ambient;
    } mesh_pc;
} ThumbEntry;

static ThumbEntry  s_cache[THUMB_CACHE_MAX];
static uint64_t    s_tick;

/* ================================================================
   Module globals (set at init)
   ================================================================ */

static Qs_GpuContext *s_gpu;
static Ca_Instance   *s_ca_instance;
static Qs_JobSystem  *s_jobs;
static bool           s_mesh_pipeline_ready;
static int            s_frame_mesh_uploads;

/* ================================================================
   GPU mesh-preview pipeline
   ================================================================ */

static Qs_GpuPipeline       *s_mesh_pipeline;
static Qs_GpuPipelineLayout *s_mesh_layout;

/* Push-constant type alias for GPU commands */
typedef struct {
    float mvp[16];
    float light_dir[3];
    float ambient;
} ThumbMeshPC;

/* ----------------------------------------------------------------
   GLSL shaders — Lambert preview
   ---------------------------------------------------------------- */

static const char *THUMB_MESH_VERT =
    "#version 450\n"
    "layout(location = 0) in vec3 in_pos;\n"
    "layout(location = 1) in vec3 in_normal;\n"
    "layout(push_constant) uniform PC {\n"
    "    mat4  mvp;\n"
    "    vec3  light_dir;\n"
    "    float ambient;\n"
    "} pc;\n"
    "layout(location = 0) out vec3 out_normal;\n"
    "void main() {\n"
    "    gl_Position = pc.mvp * vec4(in_pos, 1.0);\n"
    "    out_normal  = in_normal;\n"
    "}\n";

static const char *THUMB_MESH_FRAG =
    "#version 450\n"
    "layout(push_constant) uniform PC {\n"
    "    mat4  mvp;\n"
    "    vec3  light_dir;\n"
    "    float ambient;\n"
    "} pc;\n"
    "layout(location = 0) in  vec3 in_normal;\n"
    "layout(location = 0) out vec4 out_color;\n"
    "void main() {\n"
    "    vec3  N    = normalize(in_normal);\n"
    "    vec3  L    = normalize(pc.light_dir);\n"
    "    float ndl  = max(dot(N, L), 0.0);\n"
    "    float diff = pc.ambient + (1.0 - pc.ambient) * ndl;\n"
    "    /* Steel-blue base colour */\n"
    "    vec3  col  = vec3(0.60, 0.64, 0.72) * diff;\n"
    "    /* Gamma-encode to approximate sRGB for ca_image_create */\n"
    "    col = pow(clamp(col, 0.0, 1.0), vec3(1.0 / 2.2));\n"
    "    out_color  = vec4(col, 1.0);\n"
    "}\n";

/* ----------------------------------------------------------------
   Pipeline construction
   ---------------------------------------------------------------- */

static bool init_mesh_pipeline(void)
{
    Qs_GpuShader *vs = qs_gpu_compile_shader(s_gpu, THUMB_MESH_VERT,
                                              QS_GPU_SHADER_VERTEX);
    Qs_GpuShader *fs = qs_gpu_compile_shader(s_gpu, THUMB_MESH_FRAG,
                                              QS_GPU_SHADER_FRAGMENT);
    if (!vs || !fs) {
        QS_LOG_ERROR("Thumbnail: mesh shader compilation failed");
        if (vs) qs_gpu_destroy_shader(s_gpu, vs);
        if (fs) qs_gpu_destroy_shader(s_gpu, fs);
        return false;
    }

    Qs_GpuPushConstantRange pc_range = {
        .stages = QS_GPU_SHADER_VERTEX | QS_GPU_SHADER_FRAGMENT,
        .offset = 0,
        .size   = sizeof(ThumbMeshPC),
    };
    s_mesh_layout = qs_gpu_create_pipeline_layout(s_gpu,
        &(Qs_GpuPipelineLayoutDesc){
            .push_constants      = &pc_range,
            .push_constant_count = 1,
        });
    if (!s_mesh_layout) {
        QS_LOG_ERROR("Thumbnail: mesh pipeline layout creation failed");
        qs_gpu_destroy_shader(s_gpu, vs);
        qs_gpu_destroy_shader(s_gpu, fs);
        return false;
    }

    /* Vertex binding: position + normal.  Stride = sizeof(Qs_Vertex) = 48
       so the GPU advances correctly over tangent and UV padding. */
    Qs_GpuVertexAttribute attrs[2] = {
        { .location = 0, .format = QS_GPU_VERTEX_FORMAT_FLOAT3,
          .offset = (uint32_t)offsetof(Qs_Vertex, position) },
        { .location = 1, .format = QS_GPU_VERTEX_FORMAT_FLOAT3,
          .offset = (uint32_t)offsetof(Qs_Vertex, normal)   },
    };
    Qs_GpuVertexBinding vbinding = {
        .binding         = 0,
        .stride          = sizeof(Qs_Vertex),
        .attributes      = attrs,
        .attribute_count = 2,
    };

    s_mesh_pipeline = qs_gpu_create_graphics_pipeline(s_gpu,
        &(Qs_GpuGraphicsPipelineDesc){
            .layout               = s_mesh_layout,
            .vertex_shader        = vs,
            .fragment_shader      = fs,
            .vertex_bindings      = &vbinding,
            .vertex_binding_count = 1,
            .topology             = QS_GPU_TOPOLOGY_TRIANGLES,
            .cull_mode            = QS_GPU_CULL_BACK,
            .depth_test           = true,
            .depth_write          = true,
            .color_format         = QS_GPU_FORMAT_RGBA8_UNORM,
            .depth_format         = QS_GPU_FORMAT_D32_SFLOAT,
        });

    qs_gpu_destroy_shader(s_gpu, vs);
    qs_gpu_destroy_shader(s_gpu, fs);

    if (!s_mesh_pipeline) {
        QS_LOG_ERROR("Thumbnail: mesh pipeline creation failed");
        qs_gpu_destroy_pipeline_layout(s_gpu, s_mesh_layout);
        s_mesh_layout = NULL;
        return false;
    }
    return true;
}

/* ================================================================
   CPU pixel helpers (texture decode)
   ================================================================ */

/* Returns bytes-per-pixel for a Qs_TextureFormat value. */
static uint32_t tex_bpp(uint32_t fmt)
{
    switch (fmt) {
    case 2:  return 2;   /* RG8_UNORM        */
    case 3:  return 1;   /* R8_UNORM         */
    case 4:  return 8;   /* RGBA16_SFLOAT    */
    default: return 4;   /* RGBA8_UNORM/SRGB */
    }
}

/* IEEE 754 half-precision to float. */
static float half_to_float(uint16_t h)
{
    uint32_t sign = (uint32_t)(h >> 15) & 0x1u;
    uint32_t exp  = (uint32_t)(h >> 10) & 0x1Fu;
    uint32_t mant = (uint32_t)h & 0x3FFu;

    if (exp == 0) {
        if (!mant) return sign ? -0.0f : 0.0f;
        float m = (float)mant / 1024.0f;
        float v = ldexpf(m, -14);
        return sign ? -v : v;
    }
    if (exp == 31)
        return (mant == 0) ? (sign ? -INFINITY : INFINITY) : NAN;

    float m = 1.0f + (float)mant / 1024.0f;
    float v = ldexpf(m, (int)exp - 15);
    return sign ? -v : v;
}

/* Reinhard tone-map then gamma-encode a linear float → uint8. */
static uint8_t linear_to_u8(float x)
{
    if (!(x > 0.0f)) return 0;
    x = x / (x + 1.0f);
    x = powf(x < 1.0f ? x : 1.0f, 1.0f / 2.2f);
    return (uint8_t)(int)(x * 255.0f + 0.5f);
}

/*
 * Bilinear downsample from src (sw × sh, bpp bytes/pixel) to dst
 * (dw × dh, 4 bytes/pixel RGBA8).  Handles RGBA8, RG8, R8, RGBA16F.
 */
static void downsample_bilinear(const uint8_t *src, int sw, int sh,
                                 uint32_t bpp,
                                 uint8_t *dst,  int dw, int dh)
{
    if (!src || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;

    for (int dy = 0; dy < dh; ++dy) {
        float fy = ((float)dy + 0.5f) * (float)sh / (float)dh - 0.5f;
        int   y0 = (int)floorf(fy);
        int   y1 = y0 + 1;
        float ty = fy - (float)y0;
        if (y0 < 0)   y0 = 0;
        if (y0 >= sh) y0 = sh - 1;
        if (y1 < 0)   y1 = 0;
        if (y1 >= sh) y1 = sh - 1;

        for (int dx = 0; dx < dw; ++dx) {
            float fx = ((float)dx + 0.5f) * (float)sw / (float)dw - 0.5f;
            int   x0 = (int)floorf(fx);
            int   x1 = x0 + 1;
            float tx = fx - (float)x0;
            if (x0 < 0)   x0 = 0;
            if (x0 >= sw) x0 = sw - 1;
            if (x1 < 0)   x1 = 0;
            if (x1 >= sw) x1 = sw - 1;

            const uint8_t *s00 = src + ((size_t)y0*(size_t)sw + (size_t)x0)*bpp;
            const uint8_t *s01 = src + ((size_t)y0*(size_t)sw + (size_t)x1)*bpp;
            const uint8_t *s10 = src + ((size_t)y1*(size_t)sw + (size_t)x0)*bpp;
            const uint8_t *s11 = src + ((size_t)y1*(size_t)sw + (size_t)x1)*bpp;
            uint8_t       *dp  = dst + ((size_t)dy*(size_t)dw + (size_t)dx)*4u;

            if (bpp == 8) {
                for (int c = 0; c < 3; c++) {
                    float v00 = half_to_float((uint16_t)(s00[c*2]|(uint16_t)(s00[c*2+1]<<8)));
                    float v01 = half_to_float((uint16_t)(s01[c*2]|(uint16_t)(s01[c*2+1]<<8)));
                    float v10 = half_to_float((uint16_t)(s10[c*2]|(uint16_t)(s10[c*2+1]<<8)));
                    float v11 = half_to_float((uint16_t)(s11[c*2]|(uint16_t)(s11[c*2+1]<<8)));
                    float v   = v00*(1.0f-tx)*(1.0f-ty) + v01*tx*(1.0f-ty)
                              + v10*(1.0f-tx)*ty          + v11*tx*ty;
                    dp[c] = linear_to_u8(v);
                }
                dp[3] = 255;
            } else if (bpp == 4) {
                for (int c = 0; c < 3; c++) {
                    float v = s00[c]*(1.0f-tx)*(1.0f-ty) + s01[c]*tx*(1.0f-ty)
                            + s10[c]*(1.0f-tx)*ty          + s11[c]*tx*ty;
                    dp[c] = (uint8_t)(int)(v + 0.5f);
                }
                dp[3] = 255;
            } else if (bpp == 2) {
                float r = s00[0]*(1.0f-tx)*(1.0f-ty) + s01[0]*tx*(1.0f-ty)
                        + s10[0]*(1.0f-tx)*ty          + s11[0]*tx*ty;
                float g = s00[1]*(1.0f-tx)*(1.0f-ty) + s01[1]*tx*(1.0f-ty)
                        + s10[1]*(1.0f-tx)*ty          + s11[1]*tx*ty;
                dp[0] = (uint8_t)(int)(r + 0.5f);
                dp[1] = (uint8_t)(int)(g + 0.5f);
                dp[2] = 0;
                dp[3] = 255;
            } else {
                float v = s00[0]*(1.0f-tx)*(1.0f-ty) + s01[0]*tx*(1.0f-ty)
                        + s10[0]*(1.0f-tx)*ty          + s11[0]*tx*ty;
                uint8_t u = (uint8_t)(int)(v + 0.5f);
                dp[0] = dp[1] = dp[2] = u;
                dp[3] = 255;
            }
        }
    }
}

/* ================================================================
   Background job functions (worker threads)
   ================================================================ */

/* Texture job: read file + bilinear downsample → slot->cpu_pixels. */
static void job_decode_texture(void *data)
{
    ThumbEntry *slot = (ThumbEntry *)data;

    Qs_TexFileHeader hdr;
    void    *raw  = NULL;
    uint32_t size = 0;

    if (!qs_asset_pack_read_texture(slot->abs_path, &hdr, &raw, &size))
        goto done;

    {
        uint32_t bpp  = tex_bpp(hdr.format);
        uint8_t *rgba = (uint8_t *)qs_malloc(
            (size_t)THUMB_RENDER_SIZE * (size_t)THUMB_RENDER_SIZE * 4u, QS_MEM_EDITOR);
        if (!rgba) { qs_free(raw); goto done; }

        downsample_bilinear((const uint8_t *)raw,
                            (int)hdr.width, (int)hdr.height, bpp,
                            rgba, THUMB_RENDER_SIZE, THUMB_RENDER_SIZE);
        qs_free(raw);
        slot->cpu_pixels = rgba;
    }

done:
    atomic_store(&slot->cpu_done, true);
    qs_engine_wake();
}

/* Mesh job: read geometry + pre-compute push constants.
   Fills slot->mesh_verts/idx/pc; no GPU calls. */
static void job_decode_mesh(void *data)
{
    ThumbEntry *slot = (ThumbEntry *)data;

    Qs_MeshFileHeader hdr;
    Qs_Vertex *verts = NULL;
    uint32_t  *idx   = NULL;

    if (!qs_asset_pack_read_mesh(slot->abs_path, &hdr, &verts, &idx))
        goto done;
    if (hdr.vertex_count == 0 || hdr.index_count < 3) {
        qs_free(verts); qs_free(idx); goto done;
    }

    {
        float cx = (hdr.aabb_min[0] + hdr.aabb_max[0]) * 0.5f;
        float cy = (hdr.aabb_min[1] + hdr.aabb_max[1]) * 0.5f;
        float cz = (hdr.aabb_min[2] + hdr.aabb_max[2]) * 0.5f;
        float center[3] = { cx, cy, cz };

        float dx = hdr.aabb_max[0] - hdr.aabb_min[0];
        float dy = hdr.aabb_max[1] - hdr.aabb_min[1];
        float dz = hdr.aabb_max[2] - hdr.aabb_min[2];
        float radius = sqrtf(dx*dx + dy*dy + dz*dz) * 0.5f;
        if (radius < 1e-4f) radius = 1.0f;

        float eye_raw[3] = { -0.6f, 0.75f, 1.0f };
        float eye_dir[3];
        qs_v3_norm(eye_raw, eye_dir);
        float cam_dist = radius * 2.4f;
        float eye[3] = {
            center[0] + eye_dir[0] * cam_dist,
            center[1] + eye_dir[1] * cam_dist,
            center[2] + eye_dir[2] * cam_dist,
        };
        float up[3] = { 0.0f, 1.0f, 0.0f };

        float view[16], proj[16], mvp[16];
        qs_m4_look_at(view, eye, center, up);
        qs_m4_perspective(proj, qs_to_rad(45.0f), 1.0f,
                          radius * 0.01f, radius * 10.0f);
        qs_m4_mul(proj, view, mvp);

        float lraw[3] = { 0.5f, 1.0f, 0.8f };
        float ldir[3];
        qs_v3_norm(lraw, ldir);

        memcpy(slot->mesh_pc.mvp,       mvp,  sizeof(mvp));
        memcpy(slot->mesh_pc.light_dir, ldir, sizeof(ldir));
        slot->mesh_pc.ambient = 0.25f;
    }

    slot->mesh_verts      = verts;
    slot->mesh_idx        = idx;
    slot->mesh_vert_count = hdr.vertex_count;
    slot->mesh_idx_count  = hdr.index_count;

done:
    atomic_store(&slot->cpu_done, true);
    qs_engine_wake();
}

/* ================================================================
   GPU mesh render — main thread only
   ================================================================ */

/* Performs the offscreen Lambert render for a mesh entry whose CPU work
   (geometry loading + push-constant computation) has already finished.
   Frees slot->mesh_verts and slot->mesh_idx on return (success or failure). */
static Ca_Image *gpu_render_mesh(ThumbEntry *slot)
{
    Ca_Image        *result     = NULL;
    Qs_GpuBuffer    *vbuf       = NULL;
    Qs_GpuBuffer    *ibuf       = NULL;
    Qs_GpuBuffer    *readback   = NULL;
    Qs_GpuImage     *color_img  = NULL;
    Qs_GpuImage     *depth_img  = NULL;
    Qs_GpuImageView *color_view = NULL;
    Qs_GpuImageView *depth_view = NULL;

    if (!s_mesh_pipeline) goto done;

    vbuf = qs_gpu_create_buffer_from_data(s_gpu, QS_GPU_BUFFER_VERTEX,
                                           slot->mesh_verts,
                                           sizeof(Qs_Vertex) * slot->mesh_vert_count);
    ibuf = qs_gpu_create_buffer_from_data(s_gpu, QS_GPU_BUFFER_INDEX,
                                           slot->mesh_idx,
                                           sizeof(uint32_t) * slot->mesh_idx_count);
    qs_free(slot->mesh_verts); slot->mesh_verts = NULL;
    qs_free(slot->mesh_idx);   slot->mesh_idx   = NULL;
    if (!vbuf || !ibuf) goto done;

    color_img = qs_gpu_create_image(s_gpu, &(Qs_GpuImageDesc){
        .width      = THUMB_RENDER_SIZE,
        .height     = THUMB_RENDER_SIZE,
        .mip_levels = 1,
        .format     = QS_GPU_FORMAT_RGBA8_UNORM,
        .usage      = QS_GPU_IMAGE_COLOR_ATTACHMENT | QS_GPU_IMAGE_TRANSFER_SRC,
    });
    depth_img = qs_gpu_create_image(s_gpu, &(Qs_GpuImageDesc){
        .width      = THUMB_RENDER_SIZE,
        .height     = THUMB_RENDER_SIZE,
        .mip_levels = 1,
        .format     = QS_GPU_FORMAT_D32_SFLOAT,
        .usage      = QS_GPU_IMAGE_DEPTH_ATTACHMENT,
    });
    if (!color_img || !depth_img) goto done;

    color_view = qs_gpu_create_image_view_for(s_gpu, color_img,
                                               QS_GPU_IMAGE_ASPECT_COLOR);
    depth_view = qs_gpu_create_image_view_for(s_gpu, depth_img,
                                               QS_GPU_IMAGE_ASPECT_DEPTH);
    if (!color_view || !depth_view) goto done;

    readback = qs_gpu_create_buffer(s_gpu, &(Qs_GpuBufferDesc){
        .size   = (uint64_t)THUMB_RENDER_SIZE * THUMB_RENDER_SIZE * 4u,
        .usage  = QS_GPU_BUFFER_TRANSFER,
        .memory = QS_GPU_MEMORY_HOST_VISIBLE,
    });
    if (!readback) goto done;

    {
        static const float BG[4] = { 0.094f, 0.102f, 0.133f, 1.0f };
        ThumbMeshPC pc;
        memcpy(&pc, &slot->mesh_pc, sizeof(pc));

        Qs_GpuCmd *cmd = qs_gpu_begin_transfer(s_gpu);

        qs_cmd_image_barrier(cmd, &(Qs_GpuImageBarrier){
            .image      = color_img,
            .old_layout = QS_GPU_IMAGE_LAYOUT_UNDEFINED,
            .new_layout = QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
            .aspect     = QS_GPU_IMAGE_ASPECT_COLOR,
            .base_mip   = 0, .mip_count = 1,
        });
        qs_cmd_image_barrier(cmd, &(Qs_GpuImageBarrier){
            .image      = depth_img,
            .old_layout = QS_GPU_IMAGE_LAYOUT_UNDEFINED,
            .new_layout = QS_GPU_IMAGE_LAYOUT_DEPTH_ATTACHMENT,
            .aspect     = QS_GPU_IMAGE_ASPECT_DEPTH,
            .base_mip   = 0, .mip_count = 1,
        });

        qs_cmd_begin_rendering(cmd, &(Qs_GpuRenderTarget){
            .color       = color_view,
            .depth       = depth_view,
            .clear_color = { BG[0], BG[1], BG[2], BG[3] },
            .clear_depth = 1.0f,
            .width       = THUMB_RENDER_SIZE,
            .height      = THUMB_RENDER_SIZE,
        });

        qs_cmd_set_viewport(cmd, THUMB_RENDER_SIZE, THUMB_RENDER_SIZE);
        qs_cmd_bind_pipeline(cmd, s_mesh_pipeline);
        qs_cmd_push_constants(cmd, s_mesh_layout,
                              QS_GPU_SHADER_VERTEX | QS_GPU_SHADER_FRAGMENT,
                              0, sizeof(ThumbMeshPC), &pc);
        qs_cmd_bind_vertex_buffer(cmd, 0, vbuf, 0);
        qs_cmd_bind_index_buffer(cmd, ibuf, false);
        qs_cmd_draw_indexed(cmd, slot->mesh_idx_count, 0, 0);
        qs_cmd_end_rendering(cmd);

        qs_cmd_image_barrier(cmd, &(Qs_GpuImageBarrier){
            .image      = color_img,
            .old_layout = QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
            .new_layout = QS_GPU_IMAGE_LAYOUT_TRANSFER_SRC,
            .aspect     = QS_GPU_IMAGE_ASPECT_COLOR,
            .base_mip   = 0, .mip_count = 1,
        });
        qs_cmd_copy_image_to_buffer(cmd, color_img, readback,
                                    0, 0, THUMB_RENDER_SIZE, THUMB_RENDER_SIZE);
        qs_gpu_end_transfer(s_gpu, cmd);
    }

    {
        const uint8_t *px = (const uint8_t *)qs_gpu_map_buffer(s_gpu, readback);
        if (px)
            result = ca_image_create(s_ca_instance, px,
                                     THUMB_RENDER_SIZE, THUMB_RENDER_SIZE);
        qs_gpu_unmap_buffer(s_gpu, readback);
    }

done:
    /* Free geometry on any exit path (uploaded path freed above already). */
    qs_free(slot->mesh_verts); slot->mesh_verts = NULL;
    qs_free(slot->mesh_idx);   slot->mesh_idx   = NULL;

    if (readback)    qs_gpu_destroy_buffer    (s_gpu, readback);
    if (color_view)  qs_gpu_destroy_image_view(s_gpu, color_view);
    if (depth_view)  qs_gpu_destroy_image_view(s_gpu, depth_view);
    if (color_img)   qs_gpu_destroy_image     (s_gpu, color_img);
    if (depth_img)   qs_gpu_destroy_image     (s_gpu, depth_img);
    if (vbuf)        qs_gpu_destroy_buffer    (s_gpu, vbuf);
    if (ibuf)        qs_gpu_destroy_buffer    (s_gpu, ibuf);
    return result;
}

/* ================================================================
   Cache helpers
   ================================================================ */

/* Frees all heap data owned by a slot and zeros it completely. */
static void evict_slot(ThumbEntry *e)
{
    if (e->image)      { ca_image_destroy(s_ca_instance, e->image); }
    qs_free(e->cpu_pixels);
    qs_free(e->mesh_verts);
    qs_free(e->mesh_idx);
    memset(e, 0, sizeof(*e));
}

static bool has_ext_ci(const char *path, const char *ext)
{
    size_t n = strlen(path), m = strlen(ext);
    if (n < m) return false;
    for (size_t i = 0; i < m; i++) {
        char a = path[n - m + i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

/* Returns the cache slot for abs_path, evicting the LRU entry if full.
   Never evicts entries whose background job is still in-flight. */
static ThumbEntry *cache_slot(const char *abs_path)
{
    ThumbEntry *empty = NULL;
    ThumbEntry *lru   = NULL;

    for (int i = 0; i < THUMB_CACHE_MAX; i++) {
        ThumbEntry *e = &s_cache[i];
        if (e->used && strcmp(e->abs_path, abs_path) == 0) return e;
        if (!e->used && !empty) empty = e;
        /* Skip in-flight entries — cannot safely evict them. */
        if (e->used && e->state != THUMB_STATE_LOADING &&
            (!lru || e->tick < lru->tick)) lru = e;
    }

    if (empty) return empty;
    if (!lru)  return NULL;   /* all slots in-flight */

    evict_slot(lru);
    return lru;
}

/* ================================================================
   Public API
   ================================================================ */

void ed_thumbnail_init(Qs_Engine *engine, Ca_Window *window)
{
    if (s_gpu && s_ca_instance) return;   /* already initialised */

    s_gpu         = qs_engine_gpu(engine);
    s_ca_instance = ca_window_instance(window);
    s_jobs        = qs_engine_job_system(engine);

    memset(s_cache, 0, sizeof(s_cache));
    s_tick                = 0;
    s_frame_mesh_uploads  = 0;
    s_mesh_pipeline_ready = false;

    s_mesh_pipeline_ready = init_mesh_pipeline();
    if (!s_mesh_pipeline_ready)
        QS_LOG_WARN("Thumbnail service: mesh GPU pipeline unavailable; "
                    "mesh previews will be disabled");
}

void ed_thumbnail_shutdown(void)
{
    /* Wait for any in-flight jobs before destroying GPU resources.
       Jobs only touch their own slot; they don't touch pipeline objects,
       so it is safe to destroy the pipeline while they finish. */
    for (int i = 0; i < THUMB_CACHE_MAX; i++) {
        ThumbEntry *e = &s_cache[i];
        if (e->used && e->state == THUMB_STATE_LOADING) {
            /* Spin until the job signals cpu_done, then we can free safely. */
            while (!atomic_load(&e->cpu_done)) {
#ifdef _WIN32
                Sleep(0);
#else
                sched_yield();
#endif
            }
        }
        if (e->used) evict_slot(e);
    }
    memset(s_cache, 0, sizeof(s_cache));

    if (s_mesh_pipeline) {
        qs_gpu_destroy_pipeline(s_gpu, s_mesh_pipeline);
        s_mesh_pipeline = NULL;
    }
    if (s_mesh_layout) {
        qs_gpu_destroy_pipeline_layout(s_gpu, s_mesh_layout);
        s_mesh_layout = NULL;
    }

    s_gpu                 = NULL;
    s_ca_instance         = NULL;
    s_jobs                = NULL;
    s_mesh_pipeline_ready = false;
    s_frame_mesh_uploads  = 0;
}

void ed_thumbnail_begin_frame(void)
{
    s_frame_mesh_uploads = 0;
}

Ca_Image *ed_thumbnail_get(const char *abs_path, int64_t mtime_sec)
{
    if (!abs_path || !abs_path[0]) return NULL;
    if (!s_gpu || !s_ca_instance || !s_jobs) return NULL;

    bool is_tex  = has_ext_ci(abs_path, ".qstex");
    bool is_mesh = has_ext_ci(abs_path, ".qsmesh");
    if (!is_tex && !is_mesh) return NULL;
    if (is_mesh && !s_mesh_pipeline_ready) return NULL;

    ThumbEntry *slot = cache_slot(abs_path);
    if (!slot) return NULL;

    /* Initialise a new slot. */
    if (!slot->used || strcmp(slot->abs_path, abs_path) != 0) {
        if (slot->used) evict_slot(slot);
        memset(slot, 0, sizeof(*slot));
        slot->used      = true;
        slot->state     = THUMB_STATE_IDLE;
        slot->mtime_sec = mtime_sec;
        snprintf(slot->abs_path, sizeof(slot->abs_path), "%s", abs_path);
    }

    slot->tick = ++s_tick;

    /* Invalidate on file change (only when no job is running). */
    if (slot->mtime_sec != mtime_sec && slot->state != THUMB_STATE_LOADING) {
        if (slot->image) { ca_image_destroy(s_ca_instance, slot->image); slot->image = NULL; }
        qs_free(slot->cpu_pixels); slot->cpu_pixels = NULL;
        slot->state     = THUMB_STATE_IDLE;
        slot->mtime_sec = mtime_sec;
    }

    switch (slot->state) {

    case THUMB_STATE_IDLE:
        /* Dispatch background job. */
        slot->state = THUMB_STATE_LOADING;
        atomic_store(&slot->cpu_done, false);
        qs_job_dispatch(s_jobs,
            &(Qs_JobDesc){ .fn = is_tex ? job_decode_texture : job_decode_mesh,
                           .data = slot },
            NULL);
        return NULL;   /* placeholder until job completes */

    case THUMB_STATE_LOADING:
        if (!atomic_load(&slot->cpu_done))
            return NULL;   /* job still running */
        slot->state = THUMB_STATE_READY;
        /* fall through */

    case THUMB_STATE_READY:
        if (is_tex) {
            /* Fast path: just upload pixels to a Ca_Image (no render stall). */
            if (slot->cpu_pixels) {
                slot->image = ca_image_create(s_ca_instance, slot->cpu_pixels,
                                              THUMB_RENDER_SIZE, THUMB_RENDER_SIZE);
                qs_free(slot->cpu_pixels);
                slot->cpu_pixels = NULL;
            }
            slot->state = slot->image ? THUMB_STATE_DONE : THUMB_STATE_FAILED;
        } else {
            /* Mesh GPU render — throttled to avoid multi-stall per frame. */
            if (s_frame_mesh_uploads >= THUMB_MESH_GPU_PER_FRAME)
                return NULL;   /* defer to next frame */
            if (!slot->mesh_verts) {
                /* Job failed to load geometry. */
                slot->state = THUMB_STATE_FAILED;
                break;
            }
            slot->image = gpu_render_mesh(slot);
            s_frame_mesh_uploads++;
            slot->state = slot->image ? THUMB_STATE_DONE : THUMB_STATE_FAILED;
        }
        return slot->image;

    case THUMB_STATE_DONE:
        return slot->image;

    case THUMB_STATE_FAILED:
        return NULL;
    }
    return NULL;
}

bool ed_thumbnail_any_ready(void)
{
    if (!s_gpu || !s_ca_instance) return false;

    for (int i = 0; i < THUMB_CACHE_MAX; i++) {
        const ThumbEntry *e = &s_cache[i];
        if (!e->used) continue;
        /* Job just finished — main thread hasn't polled it yet. */
        if (e->state == THUMB_STATE_LOADING && atomic_load(&e->cpu_done))
            return true;
        /* Mesh geometry ready but held back by the per-frame GPU budget;
           needs another frame to complete the render. */
        if (e->state == THUMB_STATE_READY && e->mesh_verts)
            return true;
    }
    return false;
}

void ed_thumbnail_flush(void)
{
    for (int i = 0; i < THUMB_CACHE_MAX; i++) {
        ThumbEntry *e = &s_cache[i];
        if (!e->used) continue;

        /* Leave in-flight entries alone; they will self-expire once their
           job completes and the mtime check re-dispatches on the next get(). */
        if (e->state == THUMB_STATE_LOADING) continue;

        evict_slot(e);
    }
}
