#include "qs_renderer.h"
#include "qs_math.h"
#include "qs_scene.h"
#include "qs_light.h"
#include "qs_gpu.h"
#include "qs_mesh.h"
#include "qs_material.h"
#include "qs_system.h"
#include "qs_log.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ================================================================
   INTERNAL STRUCT DEFINITIONS
   These are the full definitions of the opaque types declared in
   qs_renderer.h.  Only qs_renderer.c needs to see them.
   ================================================================ */

#define QS_MAX_RENDER_NODES  16
#define QS_MAX_ATTACHMENTS   16
#define QS_MAX_RENDERABLES   4096

struct Qs_RenderNode {
    char             name[64];
    int32_t          priority;
    Qs_RenderNodeFn  execute;
    void            *user_data;
    bool             active;
};

struct Qs_RenderAttachment {
    char                     name[64];
    Qs_GpuImageFormat        format;
    Qs_RenderAttachmentUsage usage;
    float                    width_scale;
    float                    height_scale;
    uint32_t                 fixed_width;
    uint32_t                 fixed_height;
    Qs_GpuImage             *image;
    Qs_GpuImageView         *view;
    bool                     in_use;
};

struct Qs_Renderer {
    char name[64];

    const Qs_RendererBackend *backend;
    void                     *ctx;
    void                     *impl;

    Qs_GpuContext *gpu;

    /* Camera and display settings */
    Qs_Camera camera;
    float     clear_color[4];

    /* Current framebuffer size */
    uint32_t fb_width;
    uint32_t fb_height;

    /* Engine-managed depth attachment */
    Qs_GpuImage    *depth;
    Qs_GpuImageView *depth_view;
    bool             depth_enabled;

    /* Plugin-declared named attachments (engine creates / resizes / destroys) */
    Qs_RenderAttachment attachments[QS_MAX_ATTACHMENTS];
    uint32_t            attachment_count;

    /* Priority-sorted render pass nodes */
    Qs_RenderNode nodes[QS_MAX_RENDER_NODES];
    uint32_t      node_count;

    /* Per-frame renderable and light submission buffers */
    Qs_Renderable renderables[QS_MAX_RENDERABLES];
    uint32_t      renderable_count;
    Qs_LightGPU   lights[QS_LIGHTS_MAX];
    uint32_t      light_count;

    /* Engine-managed per-frame uniform buffers */
    Qs_GpuBuffer *frame_ubo;
    Qs_GpuBuffer *lights_ubo;

    /* Engine-managed default material (used when a renderable has none) */
    Qs_Material  *default_material;

    /* Render settings */
    bool          wireframe;
    uint32_t      debug_flags;

    /* Bound viewport (for unbinding on destroy) */
    Qs_Viewport  *bound_viewport;
};

/* ================================================================
   BACKEND REGISTRY
   ================================================================ */

#define QS_MAX_RENDERER_BACKENDS 8

typedef struct {
    const Qs_RendererBackend *backend;
    void                     *ctx;
} BackendEntry;

static BackendEntry  g_backends[QS_MAX_RENDERER_BACKENDS];
static uint32_t      g_backend_count;
static const char   *g_default_backend_name;
static bool          g_system_running;
static Qs_Engine    *g_engine_ref;
static Qs_GpuContext *g_gpu_ref;
static float         g_render_dt;

static BackendEntry *find_backend(const char *name)
{
    if (!name) name = g_default_backend_name;
    if (name) {
        for (uint32_t i = 0; i < g_backend_count; i++)
            if (g_backends[i].backend &&
                strcmp(g_backends[i].backend->name, name) == 0)
                return &g_backends[i];
        return NULL;
    }
    return (g_backend_count > 0) ? &g_backends[0] : NULL;
}

void qs_renderer_backend_register(const Qs_RendererBackend *backend)
{
    if (!backend) return;
    if (g_backend_count >= QS_MAX_RENDERER_BACKENDS) {
        QS_LOG_ERROR("Render backend registry full (max %d)", QS_MAX_RENDERER_BACKENDS);
        return;
    }
    for (uint32_t i = 0; i < g_backend_count; i++) {
        if (g_backends[i].backend &&
            strcmp(g_backends[i].backend->name, backend->name) == 0) {
            QS_LOG_WARN("Render backend '%s' already registered", backend->name);
            return;
        }
    }
    g_backends[g_backend_count].backend = backend;
    g_backends[g_backend_count].ctx     = NULL;

    if (g_system_running && g_engine_ref && g_gpu_ref) {
        if (!backend->init(g_engine_ref, g_gpu_ref,
                           &g_backends[g_backend_count].ctx)) {
            QS_LOG_ERROR("Late-registered render backend '%s' init failed",
                         backend->name);
            g_backends[g_backend_count].backend = NULL;
            return;
        }
        QS_LOG_INFO("Render backend '%s' hot-registered", backend->name);
    }
    if (g_backend_count == 0 && !g_default_backend_name)
        g_default_backend_name = backend->name;

    g_backend_count++;
    QS_LOG_INFO("Render backend '%s' registered (%u total)",
                backend->name, g_backend_count);
}

void qs_renderer_backend_unregister(const char *name)
{
    if (!name) return;
    for (uint32_t i = 0; i < g_backend_count; i++) {
        if (!g_backends[i].backend) continue;
        if (strcmp(g_backends[i].backend->name, name) != 0) continue;

        if (g_system_running && g_backends[i].ctx)
            g_backends[i].backend->shutdown(g_backends[i].ctx);

        if (g_default_backend_name &&
            strcmp(g_default_backend_name, name) == 0) {
            g_default_backend_name = NULL;
            for (uint32_t j = 0; j < g_backend_count; j++) {
                if (j != i && g_backends[j].backend) {
                    g_default_backend_name = g_backends[j].backend->name;
                    break;
                }
            }
        }
        for (uint32_t j = i; j + 1 < g_backend_count; j++)
            g_backends[j] = g_backends[j + 1];
        g_backends[g_backend_count - 1].backend = NULL;
        g_backends[g_backend_count - 1].ctx     = NULL;
        g_backend_count--;
        QS_LOG_INFO("Render backend '%s' unregistered", name);
        return;
    }
    QS_LOG_WARN("Render backend '%s' not found for unregister", name);
}

void qs_renderer_backend_set_default(const char *name)
{
    g_default_backend_name = name;
}

/* ================================================================
   VIEW/PROJECTION COMPUTATION
   ================================================================ */

static void compute_matrices(const Qs_Camera *cam, uint32_t w, uint32_t h,
                              float view[16], float proj[16])
{
    qs_m4_look_at(view, cam->position, cam->target, cam->up);
    float aspect = (h > 0) ? (float)w/(float)h : 1.0f;
    if (cam->projection == QS_PROJECTION_ORTHOGRAPHIC) {
        float hh = cam->ortho_size > 0.0f ? cam->ortho_size : 5.0f;
        qs_m4_ortho(proj, hh, aspect,
                   cam->near_plane!=0.0f?cam->near_plane:0.1f,
                   cam->far_plane !=0.0f?cam->far_plane :100.0f);
    } else {
        float fov = cam->fov_deg > 0.0f ? cam->fov_deg : 60.0f;
        qs_m4_perspective(proj, qs_to_rad(fov), aspect,
                         cam->near_plane!=0.0f?cam->near_plane:0.1f,
                         cam->far_plane !=0.0f?cam->far_plane :1000.0f);
    }
}

static void camera_defaults(Qs_Camera *cam)
{
    if (cam->up[0]==0.0f && cam->up[1]==0.0f && cam->up[2]==0.0f)
        cam->up[1] = 1.0f;
    if (cam->position[0]==0.0f && cam->position[1]==0.0f && cam->position[2]==0.0f)
        cam->position[2] = 5.0f;
}

static int node_compare(const void *a, const void *b)
{
    const Qs_RenderNode *na = a, *nb = b;
    return (na->priority > nb->priority) - (na->priority < nb->priority);
}

/* ================================================================
   ATTACHMENT RESOURCE HELPERS
   ================================================================ */

static void destroy_attachment_resource(Qs_Renderer *r, Qs_RenderAttachment *att)
{
    if (att->view)  { qs_gpu_destroy_image_view(r->gpu, att->view);  att->view  = NULL; }
    if (att->image) { qs_gpu_destroy_image(r->gpu, att->image);      att->image = NULL; }
}

static bool create_attachment_resource(Qs_Renderer *r, Qs_RenderAttachment *att,
                                        uint32_t w, uint32_t h)
{
    Qs_GpuImageUsage usage_flags = (att->usage == QS_ATTACHMENT_DEPTH)
        ? (QS_GPU_IMAGE_DEPTH_ATTACHMENT | QS_GPU_IMAGE_SAMPLED)
        : (QS_GPU_IMAGE_COLOR_ATTACHMENT | QS_GPU_IMAGE_SAMPLED);

    att->image = qs_gpu_create_image(r->gpu, &(Qs_GpuImageDesc){
        .width      = w,
        .height     = h,
        .mip_levels = 1,
        .format     = att->format,
        .usage      = usage_flags,
    });
    if (!att->image) return false;

    Qs_GpuImageAspect aspect = (att->usage == QS_ATTACHMENT_DEPTH)
        ? QS_GPU_IMAGE_ASPECT_DEPTH
        : QS_GPU_IMAGE_ASPECT_COLOR;

    att->view = qs_gpu_create_image_view_for(r->gpu, att->image, aspect);
    if (!att->view) {
        qs_gpu_destroy_image(r->gpu, att->image); att->image = NULL;
        return false;
    }

    /* Initialise layout to SHADER_READ so the first per-frame barrier can
       transition FROM it without a validation error. */
    Qs_GpuCmd *cmd = qs_gpu_begin_transfer(r->gpu);
    qs_cmd_image_barrier(cmd, &(Qs_GpuImageBarrier){
        .image      = att->image,
        .old_layout = QS_GPU_IMAGE_LAYOUT_UNDEFINED,
        .new_layout = QS_GPU_IMAGE_LAYOUT_SHADER_READ,
        .aspect     = aspect,
        .base_mip   = 0,
        .mip_count  = 1,
    });
    qs_gpu_end_transfer(r->gpu, cmd);
    return true;
}

/* ================================================================
   DEPTH ATTACHMENT HELPERS
   ================================================================ */

static void destroy_depth(Qs_Renderer *r)
{
    if (r->depth_view) { qs_gpu_destroy_image_view(r->gpu, r->depth_view); r->depth_view = NULL; }
    if (r->depth)      { qs_gpu_destroy_image(r->gpu, r->depth);           r->depth      = NULL; }
}

static void recreate_depth(Qs_Renderer *r, uint32_t w, uint32_t h)
{
    destroy_depth(r);
    if (w == 0 || h == 0) return;
    r->depth = qs_gpu_create_image(r->gpu, &(Qs_GpuImageDesc){
        .width=w, .height=h, .mip_levels=1,
        .format=QS_GPU_FORMAT_DEPTH_AUTO,
        .usage =QS_GPU_IMAGE_DEPTH_ATTACHMENT,
    });
    if (!r->depth) return;
    r->depth_view = qs_gpu_create_image_view_for(r->gpu, r->depth,
                                                  QS_GPU_IMAGE_ASPECT_DEPTH);
    if (!r->depth_view) {
        qs_gpu_destroy_image(r->gpu, r->depth); r->depth = NULL;
    }
}

/* ================================================================
   VIEWPORT CALLBACKS  (engine-registered, not plugin-registered)
   ================================================================ */

static void renderer_on_render(const Qs_GpuFrame *frame,
                                Qs_Viewport *vp, void *user_data)
{
    (void)vp;
    Qs_Renderer *r = user_data;
    uint32_t w = frame->width, h = frame->height;
    if (w == 0 || h == 0) return;

    /* Compute view / projection matrices */
    float view[16], proj[16];
    compute_matrices(&r->camera, w, h, view, proj);

    /* Write FrameUBO */
    Qs_FrameUBO *fubo = qs_gpu_map_buffer(r->gpu, r->frame_ubo);
    if (fubo) {
        memcpy(fubo->view, view, 64);
        memcpy(fubo->proj, proj, 64);
        qs_m4_identity(fubo->inv_view_proj);
        fubo->cam_pos[0]    = r->camera.position[0];
        fubo->cam_pos[1]    = r->camera.position[1];
        fubo->cam_pos[2]    = r->camera.position[2];
        fubo->time          = g_render_dt;
        fubo->screen_width  = (float)w;
        fubo->screen_height = (float)h;
        fubo->debug_flags   = r->debug_flags;
        qs_gpu_unmap_buffer(r->gpu, r->frame_ubo);
    }

    /* Write LightsUBO */
    Qs_LightsUBO *lubo = qs_gpu_map_buffer(r->gpu, r->lights_ubo);
    if (lubo) {
        lubo->count = r->light_count < QS_LIGHTS_MAX ? r->light_count:QS_LIGHTS_MAX;
        memcpy(lubo->lights, r->lights, lubo->count * sizeof(Qs_LightGPU));
        qs_gpu_unmap_buffer(r->gpu, r->lights_ubo);
    }

    /* Invoke render nodes */
    Qs_RenderContext ctx = {
        .renderer         = r,
        .cmd              = frame->cmd,
        .width            = w,
        .height           = h,
        .dt               = g_render_dt,
        .renderables      = r->renderables,
        .renderable_count = r->renderable_count,
        .lights           = r->lights,
        .light_count      = r->light_count,
        .swapchain_view   = frame->color_target,
        .swapchain_width  = w,
        .swapchain_height = h,
    };
    memcpy(ctx.view, view, 64);
    memcpy(ctx.proj, proj, 64);

    for (uint32_t i = 0; i < r->node_count; i++) {
        if (r->nodes[i].active && r->nodes[i].execute)
            r->nodes[i].execute(&ctx, r->nodes[i].user_data);
    }
}

static void renderer_on_resize(Qs_Viewport *vp, uint32_t w, uint32_t h,
                                void *user_data)
{
    (void)vp;
    Qs_Renderer *r = user_data;
    r->fb_width  = w;
    r->fb_height = h;
    if (w == 0 || h == 0) return;

    /* Recreate engine-managed depth buffer */
    if (r->depth_enabled)
        recreate_depth(r, w, h);

    /* Recreate viewport-scaled attachments (fixed-size are skipped) */
    for (uint32_t i = 0; i < r->attachment_count; i++) {
        Qs_RenderAttachment *att = &r->attachments[i];
        if (!att->in_use || att->fixed_width > 0) continue;
        uint32_t aw = (uint32_t)(w * att->width_scale  + 0.5f);
        uint32_t ah = (uint32_t)(h * att->height_scale + 0.5f);
        if (aw < 1) aw = 1;
        if (ah < 1) ah = 1;
        destroy_attachment_resource(r, att);
        create_attachment_resource(r, att, aw, ah);
    }

    /* Notify backend so it can re-write descriptor sets */
    if (r->backend && r->backend->renderer_on_resize && r->impl)
        r->backend->renderer_on_resize(r->ctx, r->impl, w, h);
}

/* ================================================================
   ENGINE SYSTEM
   ================================================================ */

static bool render_sys_init(Qs_System *sys, Qs_Engine *engine)
{
    (void)sys;
    Qs_GpuContext *gpu = qs_engine_gpu(engine);
    g_engine_ref = engine;
    g_gpu_ref    = gpu;

    if (g_backend_count == 0) {
        QS_LOG_WARN("Render system: no backends registered (renderer plugins may be disabled)");
        g_system_running = true;
        return true;
    }

    for (uint32_t i = 0; i < g_backend_count; i++) {
        if (!g_backends[i].backend) continue;
        if (!g_backends[i].backend->init(engine, gpu, &g_backends[i].ctx)) {
            QS_LOG_ERROR("Render backend '%s' init failed", g_backends[i].backend->name);
            g_backends[i].ctx = NULL;
            continue;
        }
        QS_LOG_INFO("Render backend '%s' initialised", g_backends[i].backend->name);
    }
    g_system_running = true;
    return true;
}

static void render_sys_shutdown(Qs_System *sys, Qs_Engine *engine)
{
    (void)sys; (void)engine;
    g_system_running = false;
    for (uint32_t i = 0; i < g_backend_count; i++) {
        if (g_backends[i].backend && g_backends[i].ctx) {
            g_backends[i].backend->shutdown(g_backends[i].ctx);
            g_backends[i].ctx = NULL;
        }
    }
    g_engine_ref = NULL;
    g_gpu_ref    = NULL;
}

static void render_sys_update(Qs_System *sys, Qs_Engine *engine, float dt)
{
    (void)sys; (void)engine;
    g_render_dt = dt;
    for (uint32_t i = 0; i < g_backend_count; i++) {
        if (g_backends[i].backend && g_backends[i].ctx &&
            g_backends[i].backend->update)
            g_backends[i].backend->update(g_backends[i].ctx, dt);
    }
}

Qs_SystemDesc qs_render_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Render",
        .data_size = 0,
        .init      = render_sys_init,
        .shutdown  = render_sys_shutdown,
        .update    = render_sys_update,
    };
}

/* ================================================================
   PUBLIC API
   ================================================================ */

Qs_Renderer *qs_renderer_create(Qs_Engine *engine, const Qs_RendererDesc *desc)
{
    BackendEntry *entry = find_backend(desc ? desc->backend : NULL);
    if (!entry || !entry->ctx || !entry->backend->renderer_create) {
        QS_LOG_ERROR("qs_renderer_create: backend '%s' not available",
                     desc && desc->backend ? desc->backend : "(default)");
        return NULL;
    }

    Qs_Renderer *r = qs_calloc(1, sizeof(Qs_Renderer), QS_MEM_RENDER);
    if (!r) return NULL;

    r->backend       = entry->backend;
    r->ctx           = entry->ctx;
    r->gpu           = g_gpu_ref;
    r->depth_enabled = desc ? desc->depth_test : true;
    if (desc) {
        if (desc->name)
            snprintf(r->name, sizeof(r->name), "%s", desc->name);
        memcpy(r->clear_color, desc->clear_color, sizeof(r->clear_color));
        r->camera = desc->camera;
    }
    camera_defaults(&r->camera);

    /* Create engine-owned per-frame UBOs before calling renderer_create so the
       backend can query them via qs_renderer_get_frame_ubo / get_lights_ubo. */
    r->frame_ubo = qs_gpu_create_buffer(r->gpu, &(Qs_GpuBufferDesc){
        .size=sizeof(Qs_FrameUBO), .usage=QS_GPU_BUFFER_UNIFORM,
        .memory=QS_GPU_MEMORY_HOST_VISIBLE });
    r->lights_ubo = qs_gpu_create_buffer(r->gpu, &(Qs_GpuBufferDesc){
        .size=sizeof(Qs_LightsUBO), .usage=QS_GPU_BUFFER_UNIFORM,
        .memory=QS_GPU_MEMORY_HOST_VISIBLE });
    if (!r->frame_ubo || !r->lights_ubo) {
        QS_LOG_ERROR("qs_renderer_create: UBO allocation failed");
        if (r->frame_ubo)  qs_gpu_destroy_buffer(r->gpu, r->frame_ubo);
        if (r->lights_ubo) qs_gpu_destroy_buffer(r->gpu, r->lights_ubo);
        qs_free(r);
        return NULL;
    }

    /* Call backend with the pre-populated handle.  The backend may call
       qs_renderer_add_attachment() and qs_renderer_add_node() here. */
    r->impl = entry->backend->renderer_create(entry->ctx, engine, desc, r);
    if (!r->impl) {
        /* Destroy any attachments the backend may have declared before failing */
        for (uint32_t i = 0; i < r->attachment_count; i++)
            destroy_attachment_resource(r, &r->attachments[i]);
        qs_gpu_destroy_buffer(r->gpu, r->frame_ubo);
        qs_gpu_destroy_buffer(r->gpu, r->lights_ubo);
        qs_free(r);
        return NULL;
    }

    /* Create the engine-owned default material now that the backend (and thus
       the material system) has been fully initialised. */
    static const Qs_MaterialDesc s_fallback = {
        .name                 = "_renderer_default",
        .base_color_factor    = {0.8f, 0.8f, 0.8f, 1.0f},
        .metallic_factor      = 0.0f,
        .roughness_factor     = 0.8f,
        .occlusion_strength   = 1.0f,
        .normal_scale         = 1.0f,
        .alpha_cutoff         = 0.5f,
    };
    const Qs_MaterialDesc *mat_desc = (desc && desc->default_material)
                                      ? desc->default_material : &s_fallback;
    r->default_material = qs_material_create(engine, mat_desc);
    if (!r->default_material)
        QS_LOG_WARN("qs_renderer_create: default material creation failed");

    return r;
}

void qs_renderer_destroy(Qs_Renderer *renderer)
{
    if (!renderer) return;
    /* Unbind from viewport so stale callbacks are never invoked */
    if (renderer->bound_viewport)
        qs_viewport_set_callbacks(renderer->bound_viewport,
                                  NULL, NULL, NULL, NULL);
    /* Backend cleanup first (frees descriptor sets that reference engine UBOs) */
    if (renderer->backend && renderer->backend->renderer_destroy)
        renderer->backend->renderer_destroy(renderer->ctx, renderer->impl);
    /* Engine resource cleanup */
    if (renderer->default_material) {
        qs_material_destroy(renderer->default_material);
        renderer->default_material = NULL;
    }
    destroy_depth(renderer);
    for (uint32_t i = 0; i < renderer->attachment_count; i++)
        destroy_attachment_resource(renderer, &renderer->attachments[i]);
    if (renderer->frame_ubo)  qs_gpu_destroy_buffer(renderer->gpu, renderer->frame_ubo);
    if (renderer->lights_ubo) qs_gpu_destroy_buffer(renderer->gpu, renderer->lights_ubo);
    qs_free(renderer);
}

void qs_renderer_bind(Qs_Renderer *renderer, Qs_Viewport *viewport)
{
    if (!renderer || !viewport) return;
    renderer->bound_viewport = viewport;
    qs_viewport_set_callbacks(viewport, renderer_on_render, renderer,
                               renderer_on_resize, renderer);
    /* Trigger resize immediately for viewports that already have a size */
    uint32_t w = qs_viewport_width(viewport);
    uint32_t h = qs_viewport_height(viewport);
    if (w > 0 && h > 0)
        renderer_on_resize(viewport, w, h, renderer);
}

Qs_Camera *qs_renderer_camera(Qs_Renderer *r)
{
    return r ? &r->camera : NULL;
}

void qs_renderer_set_clear_color(Qs_Renderer *r, const float color[4])
{
    if (r && color) memcpy(r->clear_color, color, 16);
}

const float *qs_renderer_clear_color(const Qs_Renderer *r)
{
    return r ? r->clear_color : NULL;
}

const char *qs_renderer_name(const Qs_Renderer *r)
{
    return r ? r->name : NULL;
}

void qs_renderer_set_wireframe(Qs_Renderer *r, bool wireframe)
{
    if (r) r->wireframe = wireframe;
}

bool qs_renderer_wireframe(const Qs_Renderer *r)
{
    return r && r->wireframe;
}

void qs_renderer_set_debug_flags(Qs_Renderer *r, uint32_t flags)
{
    if (r) r->debug_flags = flags;
}

uint32_t qs_renderer_debug_flags(const Qs_Renderer *r)
{
    return r ? r->debug_flags : 0;
}

void qs_renderer_extents(const Qs_Renderer *r, uint32_t *out_w, uint32_t *out_h)
{
    if (out_w) *out_w = r ? r->fb_width  : 0;
    if (out_h) *out_h = r ? r->fb_height : 0;
}

Qs_RenderNode *qs_renderer_add_node(Qs_Renderer *r, const Qs_RenderNodeDesc *desc)
{
    if (!r || !desc || !desc->execute) return NULL;
    if (r->node_count >= QS_MAX_RENDER_NODES) {
        QS_LOG_WARN("qs_renderer_add_node: node limit reached on '%s'", r->name);
        return NULL;
    }
    Qs_RenderNode *node = &r->nodes[r->node_count++];
    memset(node, 0, sizeof(*node));
    node->active    = true;
    node->priority  = desc->priority;
    node->execute   = desc->execute;
    node->user_data = desc->user_data;
    if (desc->name) snprintf(node->name, sizeof(node->name), "%s", desc->name);
    qsort(r->nodes, r->node_count, sizeof(Qs_RenderNode), node_compare);
    return node;
}

void qs_renderer_remove_node(Qs_Renderer *r, Qs_RenderNode *node)
{
    if (!r || !node) return;
    for (uint32_t i = 0; i < r->node_count; i++) {
        if (&r->nodes[i] == node) {
            memmove(&r->nodes[i], &r->nodes[i+1],
                    (r->node_count-i-1)*sizeof(Qs_RenderNode));
            r->node_count--;
            return;
        }
    }
}

/* ================================================================
   RENDER ATTACHMENT API
   ================================================================ */

Qs_RenderAttachment *qs_renderer_add_attachment(Qs_Renderer *r,
                                                  const Qs_RenderAttachmentDesc *desc)
{
    if (!r || !desc) return NULL;
    if (r->attachment_count >= QS_MAX_ATTACHMENTS) {
        QS_LOG_ERROR("qs_renderer_add_attachment: attachment limit reached");
        return NULL;
    }
    Qs_RenderAttachment *att = &r->attachments[r->attachment_count++];
    memset(att, 0, sizeof(*att));
    att->in_use       = true;
    att->format       = desc->format;
    att->usage        = desc->usage;
    att->width_scale  = desc->width_scale;
    att->height_scale = desc->height_scale;
    att->fixed_width  = desc->fixed_width;
    att->fixed_height = desc->fixed_height;
    if (desc->name) snprintf(att->name, sizeof(att->name), "%s", desc->name);

    uint32_t w, h;
    if (desc->fixed_width > 0 && desc->fixed_height > 0) {
        /* Fixed-size: create immediately */
        w = desc->fixed_width;
        h = desc->fixed_height;
    } else {
        /* Viewport-scaled: create 1x1 placeholder; will be resized on first bind */
        w = 1; h = 1;
    }
    if (!create_attachment_resource(r, att, w, h)) {
        QS_LOG_ERROR("qs_renderer_add_attachment: failed to create '%s'",
                     att->name);
        att->in_use = false;
        r->attachment_count--;
        return NULL;
    }
    return att;
}

Qs_GpuImageView *qs_attachment_view(const Qs_RenderAttachment *att)
{
    return att ? att->view : NULL;
}

Qs_GpuImage *qs_attachment_image(const Qs_RenderAttachment *att)
{
    return att ? att->image : NULL;
}

Qs_GpuImageView *qs_renderer_depth_view(const Qs_Renderer *r)
{
    return (r && r->depth_enabled) ? r->depth_view : NULL;
}

Qs_GpuBuffer *qs_renderer_get_frame_ubo(const Qs_Renderer *r)
{
    return r ? r->frame_ubo : NULL;
}

Qs_GpuBuffer *qs_renderer_get_lights_ubo(const Qs_Renderer *r)
{
    return r ? r->lights_ubo : NULL;
}

/* ================================================================
   RENDERABLE / LIGHT SUBMISSION
   ================================================================ */

void qs_renderer_submit_renderable(Qs_Renderer *r, const Qs_RenderableDesc *desc)
{
    if (!r || !desc || !desc->mesh) return;
    if (r->renderable_count >= QS_MAX_RENDERABLES) {
        static bool s_overflow_warned;
        if (!s_overflow_warned) {
            QS_LOG_WARN("qs_renderer_submit_renderable: buffer full (%u) "
                        "\xe2\x80\x94 extra renderables dropped. "
                        "Increase QS_MAX_RENDERABLES in qs_renderer.c.",
                        QS_MAX_RENDERABLES);
            s_overflow_warned = true;
        }
        return;
    }

    /* Resolve material: use desc->material if provided, else renderer default */
    Qs_Material *mat = desc->material ? desc->material : r->default_material;

    Qs_Renderable *ren = &r->renderables[r->renderable_count++];

    /* Extract mesh GPU data */
    ren->vertex_buffer = qs_mesh_vertex_buffer(desc->mesh);
    ren->index_buffer  = qs_mesh_index_buffer(desc->mesh);
    ren->vertex_count  = qs_mesh_vertex_count(desc->mesh);
    ren->index_count   = qs_mesh_index_count(desc->mesh);
    ren->index_16bit   = (qs_mesh_index_type(desc->mesh) == QS_INDEX_TYPE_UINT16);

    /* Extract material GPU data */
    ren->material_set  = mat ? qs_material_descriptor_set(mat) : NULL;
    ren->alpha_mode    = mat ? qs_material_alpha_mode(mat)      : QS_ALPHA_MODE_OPAQUE;
    ren->double_sided  = mat ? qs_material_double_sided(mat)    : false;
    if (mat) {
        const Qs_PBRParams *p = qs_material_params(mat);
        if (p) ren->material_params = *p;
    }

    /* Transform and culling */
    memcpy(ren->transform, desc->transform, 64);
    ren->bounds          = desc->bounds;
    ren->entity          = desc->entity;
    ren->cast_shadows    = desc->cast_shadows;
    ren->receive_shadows = desc->receive_shadows;
}

void qs_renderer_clear_renderables(Qs_Renderer *r)
{
    if (r) r->renderable_count = 0;
}

const Qs_Renderable *qs_renderer_renderables(const Qs_Renderer *r, uint32_t *out_count)
{
    if (out_count) *out_count = r ? r->renderable_count : 0;
    return (r && r->renderable_count > 0) ? r->renderables : NULL;
}

void qs_renderer_submit_light(Qs_Renderer *r, Qs_Light *light)
{
    if (!r || !light) return;
    if (!qs_light_is_active(light)) return;
    if (r->light_count < QS_LIGHTS_MAX)
        qs_light_pack_gpu(light, &r->lights[r->light_count++]);
}

void qs_renderer_submit_light_comp(Qs_Renderer *r, const Qs_LightComp *lc)
{
    if (!r || !lc || !lc->enabled) return;
    if (r->light_count >= QS_LIGHTS_MAX) return;
    Qs_LightGPU *out = &r->lights[r->light_count++];
    out->position[0]     = 0.0f; out->position[1] = 0.0f; out->position[2] = 0.0f;
    out->direction[0]    = lc->direction[0];
    out->direction[1]    = lc->direction[1];
    out->direction[2]    = lc->direction[2];
    out->color[0]        = lc->color[0];
    out->color[1]        = lc->color[1];
    out->color[2]        = lc->color[2];
    out->intensity       = lc->intensity;
    out->range           = lc->range;
    out->type            = (uint32_t)lc->type;
    out->cast_shadows    = lc->cast_shadows ? 1u : 0u;
    /* Convert cone degrees to cosines for the GPU shader */
    float inner = qs_to_rad(lc->inner_cone_deg);
    float outer = qs_to_rad(lc->outer_cone_deg);
    out->inner_cone_cos  = cosf(inner);
    out->outer_cone_cos  = cosf(outer > inner ? outer : inner);
    out->_pad            = 0;
}

void qs_renderer_clear_lights(Qs_Renderer *r)
{
    if (r) r->light_count = 0;
}

const Qs_LightGPU *qs_renderer_lights(const Qs_Renderer *r, uint32_t *out_count)
{
    if (out_count) *out_count = r ? r->light_count : 0;
    return (r && r->light_count > 0) ? r->lights : NULL;
}
