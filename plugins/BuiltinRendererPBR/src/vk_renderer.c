#include "qs_renderer.h"
#include "qs_light.h"
#include "qs_log.h"
#include "vk_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define QS_MAX_RENDERERS       32
#define QS_MAX_RENDER_NODES    16
#define QS_MAX_LIGHTS_PER_RENDERER 128

/* ================================================================
   CONCRETE TYPES   (plugin-internal)
   ================================================================ */

struct Qs_RenderNode {
    char              name[64];
    int32_t           priority;
    Qs_RenderNodeFn   execute;
    void             *user_data;
    bool              active;
};

struct Qs_Renderer {
    char              name[64];
    bool              in_use;

    VkDevice          device;
    VkPhysicalDevice  physical_device;

    VkImage           depth_image;
    VkDeviceMemory    depth_memory;
    VkImageView       depth_view;
    bool              depth_enabled;

    VkClearColorValue clear_color;
    uint32_t          fb_width;
    uint32_t          fb_height;

    Qs_Camera         camera;

    Qs_RenderNode     nodes[QS_MAX_RENDER_NODES];
    uint32_t          node_count;

    Ca_Instance      *ca_instance;
    float             dt;

    /* Per-frame light accumulation */
    Qs_LightGPU       lights[QS_MAX_LIGHTS_PER_RENDERER];
    uint32_t          light_count;
};

typedef struct {
    Ca_Instance      *ca_instance;
    VkDevice          device;
    VkPhysicalDevice  physical_device;
    Qs_Renderer       renderers[QS_MAX_RENDERERS];
    uint32_t          count;
    float             dt;
} VkRenderSystemData;

static VkRenderSystemData *g_render_system;

/* ================================================================
   MATH HELPERS
   ================================================================ */

static void mat4_identity(float m[16])
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_look_at(float out[16], const float eye[3],
                          const float center[3], const float up[3])
{
    float fx = center[0] - eye[0], fy = center[1] - eye[1], fz = center[2] - eye[2];
    float len = sqrtf(fx*fx + fy*fy + fz*fz);
    if (len > 1e-6f) { fx /= len; fy /= len; fz /= len; }
    float sx = fy*up[2] - fz*up[1], sy = fz*up[0] - fx*up[2], sz = fx*up[1] - fy*up[0];
    len = sqrtf(sx*sx + sy*sy + sz*sz);
    if (len > 1e-6f) { sx /= len; sy /= len; sz /= len; }
    float ux = sy*fz - sz*fy, uy = sz*fx - sx*fz, uz = sx*fy - sy*fx;
    memset(out, 0, 16 * sizeof(float));
    out[0] = sx;  out[4] = sy;  out[8]  = sz;  out[12] = -(sx*eye[0] + sy*eye[1] + sz*eye[2]);
    out[1] = ux;  out[5] = uy;  out[9]  = uz;  out[13] = -(ux*eye[0] + uy*eye[1] + uz*eye[2]);
    out[2] = -fx; out[6] = -fy; out[10] = -fz; out[14] =  (fx*eye[0] + fy*eye[1] + fz*eye[2]);
    out[3] = 0;   out[7] = 0;   out[11] = 0;   out[15] = 1.0f;
}

static void mat4_perspective(float out[16], float fov_rad, float aspect,
                              float near_p, float far_p)
{
    memset(out, 0, 16 * sizeof(float));
    float tan_half = tanf(fov_rad * 0.5f);
    out[0]  = 1.0f / (aspect * tan_half);
    out[5]  = -1.0f / tan_half;
    out[10] = -(far_p + near_p) / (far_p - near_p);
    out[11] = -1.0f;
    out[14] = -(2.0f * far_p * near_p) / (far_p - near_p);
}

static void mat4_ortho(float out[16], float half_h, float aspect,
                        float near_p, float far_p)
{
    memset(out, 0, 16 * sizeof(float));
    float half_w = half_h * aspect;
    out[0]  =  1.0f / half_w;
    out[5]  = -1.0f / half_h;
    out[10] = -2.0f / (far_p - near_p);
    out[14] = -(far_p + near_p) / (far_p - near_p);
    out[15] =  1.0f;
}

/* ================================================================
   DEPTH BUFFER
   ================================================================ */

static VkFormat find_depth_format(VkPhysicalDevice pd)
{
    VkFormat candidates[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };
    for (uint32_t i = 0; i < 3; i++) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(pd, candidates[i], &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return candidates[i];
    }
    return VK_FORMAT_D32_SFLOAT;
}

static void destroy_depth(Qs_Renderer *r)
{
    if (r->depth_view)   { vkDestroyImageView(r->device, r->depth_view, NULL);  r->depth_view   = VK_NULL_HANDLE; }
    if (r->depth_image)  { vkDestroyImage(r->device, r->depth_image, NULL);     r->depth_image  = VK_NULL_HANDLE; }
    if (r->depth_memory) { vkFreeMemory(r->device, r->depth_memory, NULL);      r->depth_memory = VK_NULL_HANDLE; }
}

static bool create_depth(Qs_Renderer *r, uint32_t w, uint32_t h)
{
    VkFormat fmt = find_depth_format(r->physical_device);
    VkImageCreateInfo ci = {
        .sType       = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType   = VK_IMAGE_TYPE_2D,
        .format      = fmt,
        .extent      = { w, h, 1 },
        .mipLevels   = 1,
        .arrayLayers = 1,
        .samples     = VK_SAMPLE_COUNT_1_BIT,
        .tiling      = VK_IMAGE_TILING_OPTIMAL,
        .usage       = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    };
    if (vkCreateImage(r->device, &ci, NULL, &r->depth_image) != VK_SUCCESS) return false;

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(r->device, r->depth_image, &req);
    uint32_t mi = vk_find_memory_type(r->physical_device, req.memoryTypeBits,
                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mi == UINT32_MAX) { vkDestroyImage(r->device, r->depth_image, NULL); r->depth_image = VK_NULL_HANDLE; return false; }

    VkMemoryAllocateInfo ai = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = req.size,
        .memoryTypeIndex = mi,
    };
    if (vkAllocateMemory(r->device, &ai, NULL, &r->depth_memory) != VK_SUCCESS) {
        vkDestroyImage(r->device, r->depth_image, NULL); r->depth_image = VK_NULL_HANDLE; return false;
    }
    vkBindImageMemory(r->device, r->depth_image, r->depth_memory, 0);

    VkImageViewCreateInfo vi = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = r->depth_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = fmt,
        .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 },
    };
    if (vkCreateImageView(r->device, &vi, NULL, &r->depth_view) != VK_SUCCESS) {
        destroy_depth(r); return false;
    }
    return true;
}

static void recreate_depth(Qs_Renderer *r, uint32_t w, uint32_t h)
{
    destroy_depth(r);
    if (w == 0 || h == 0) return;
    create_depth(r, w, h);
    r->fb_width  = w;
    r->fb_height = h;
}

static void compute_matrices(const Qs_Camera *cam, uint32_t w, uint32_t h,
                              float view[16], float proj[16])
{
    mat4_look_at(view, cam->position, cam->target, cam->up);
    float aspect = (h > 0) ? (float)w / (float)h : 1.0f;
    if (cam->projection == QS_PROJECTION_ORTHOGRAPHIC) {
        float half_h = cam->ortho_size > 0.0f ? cam->ortho_size : 5.0f;
        mat4_ortho(proj, half_h, aspect,
                   cam->near_plane != 0.0f ? cam->near_plane : 0.1f,
                   cam->far_plane  != 0.0f ? cam->far_plane  : 100.0f);
    } else {
        float fov = cam->fov_deg > 0.0f ? cam->fov_deg : 60.0f;
        mat4_perspective(proj, fov * 3.14159265f / 180.0f, aspect,
                         cam->near_plane != 0.0f ? cam->near_plane : 0.1f,
                         cam->far_plane  != 0.0f ? cam->far_plane  : 1000.0f);
    }
}

static int node_compare(const void *a, const void *b)
{
    const Qs_RenderNode *na = a, *nb = b;
    return (na->priority > nb->priority) - (na->priority < nb->priority);
}

/* ================================================================
   VIEWPORT CALLBACKS
   ================================================================ */

static void on_render(Ca_Viewport *vp, void *user_data)
{
    Qs_Renderer *r = user_data;
    uint32_t w = ca_viewport_width(vp), h = ca_viewport_height(vp);
    if (w == 0 || h == 0) return;

    if (r->depth_enabled && (w != r->fb_width || h != r->fb_height))
        recreate_depth(r, w, h);
    if (!r->depth_enabled) { r->fb_width = w; r->fb_height = h; }

    VkCommandBuffer cmd = ca_viewport_cmd(vp);

    VkRenderingAttachmentInfo color_att = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = ca_viewport_image_view(vp),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue  = { .color = r->clear_color },
    };
    VkRenderingAttachmentInfo depth_att = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = r->depth_view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue  = { .depthStencil = { 1.0f, 0 } },
    };
    VkRenderingInfo rendering_info = {
        .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea           = { .offset = {0,0}, .extent = {w,h} },
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &color_att,
        .pDepthAttachment     = (r->depth_enabled && r->depth_view) ? &depth_att : NULL,
    };
    vkCmdBeginRendering(cmd, &rendering_info);

    VkViewport viewport = { .x=0,.y=0,.width=(float)w,.height=(float)h,.minDepth=0.0f,.maxDepth=1.0f };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor = { .offset={0,0}, .extent={w,h} };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    Qs_RenderContext ctx = { .renderer=r, .cmd=cmd, .width=w, .height=h, .dt=r->dt };
    compute_matrices(&r->camera, w, h, ctx.view, ctx.proj);

    for (uint32_t i = 0; i < r->node_count; i++) {
        if (r->nodes[i].active && r->nodes[i].execute)
            r->nodes[i].execute(&ctx, r->nodes[i].user_data);
    }

    vkCmdEndRendering(cmd);
}

static void on_resize(Ca_Viewport *vp, uint32_t w, uint32_t h, void *user_data)
{
    (void)vp;
    Qs_Renderer *r = user_data;
    if (r->depth_enabled && w > 0 && h > 0)
        recreate_depth(r, w, h);
    else { r->fb_width = w; r->fb_height = h; }
}

/* ================================================================
   BACKEND LIFECYCLE
   ================================================================ */

static bool vk_render_init(Qs_Engine *engine, Ca_Instance *ca, void **out_ctx)
{
    (void)engine;
    VkRenderSystemData *data = calloc(1, sizeof(VkRenderSystemData));
    if (!data) return false;

    data->ca_instance     = ca;
    data->device          = ca_gpu_device(ca);
    data->physical_device = ca_gpu_physical_device(ca);
    if (!data->device || !data->physical_device) { free(data); return false; }

    g_render_system = data;
    *out_ctx = data;
    QS_LOG_INFO("VkRenderer: render system initialised (device %p)", (void *)data->device);
    return true;
}

static void vk_render_shutdown(void *ctx)
{
    VkRenderSystemData *data = ctx;
    for (uint32_t i = 0; i < QS_MAX_RENDERERS; i++) {
        if (data->renderers[i].in_use) {
            vkDeviceWaitIdle(data->renderers[i].device);
            destroy_depth(&data->renderers[i]);
            data->renderers[i].in_use = false;
        }
    }
    g_render_system = NULL;
    free(data);
    QS_LOG_INFO("VkRenderer: render system shut down");
}

static void vk_render_update(void *ctx, float dt)
{
    VkRenderSystemData *data = ctx;
    data->dt = dt;
    for (uint32_t i = 0; i < QS_MAX_RENDERERS; i++) {
        if (data->renderers[i].in_use)
            data->renderers[i].dt = dt;
    }
}

/* ================================================================
   RENDERER LIFECYCLE
   ================================================================ */

static void renderer_defaults(Qs_Camera *cam)
{
    if (cam->up[0] == 0.0f && cam->up[1] == 0.0f && cam->up[2] == 0.0f)
        cam->up[1] = 1.0f;
    if (cam->position[0] == 0.0f && cam->position[1] == 0.0f && cam->position[2] == 0.0f)
        cam->position[2] = 5.0f;
}

static Qs_Renderer *vk_renderer_create(void *ctx, Qs_Engine *engine,
                                        const Qs_RendererDesc *desc)
{
    (void)engine;
    VkRenderSystemData *sys = ctx;
    if (!sys || !desc) return NULL;

    Qs_Renderer *r = NULL;
    for (uint32_t i = 0; i < QS_MAX_RENDERERS; i++) {
        if (!sys->renderers[i].in_use) { r = &sys->renderers[i]; break; }
    }
    if (!r) {
        QS_LOG_ERROR("VkRenderer: renderer limit reached (%d)", QS_MAX_RENDERERS);
        return NULL;
    }

    memset(r, 0, sizeof(*r));
    r->in_use          = true;
    r->device          = sys->device;
    r->physical_device = sys->physical_device;
    r->ca_instance     = sys->ca_instance;
    r->clear_color     = desc->clear_color;
    r->camera          = desc->camera;
    r->depth_enabled   = desc->depth_test;

    if (desc->name)
        snprintf(r->name, sizeof(r->name), "%s", desc->name);
    else
        snprintf(r->name, sizeof(r->name), "renderer_%u", sys->count);

    renderer_defaults(&r->camera);
    sys->count++;
    QS_LOG_INFO("VkRenderer: '%s' created (depth=%s)", r->name, r->depth_enabled ? "on" : "off");
    return r;
}

static void vk_renderer_destroy(void *ctx, Qs_Renderer *renderer)
{
    VkRenderSystemData *sys = ctx;
    if (!renderer || !renderer->in_use) return;
    vkDeviceWaitIdle(renderer->device);
    destroy_depth(renderer);
    QS_LOG_INFO("VkRenderer: '%s' destroyed", renderer->name);
    renderer->in_use = false;
    if (sys && sys->count > 0) sys->count--;
}

static void vk_renderer_bind(void *ctx, Qs_Renderer *renderer, Ca_Viewport *viewport)
{
    (void)ctx;
    if (!renderer || !viewport) return;
    ca_viewport_set_callbacks(viewport, on_render, renderer, on_resize, renderer);
}

/* ================================================================
   ACCESSORS
   ================================================================ */

static Qs_Camera *vk_renderer_camera(Qs_Renderer *r) { return r ? &r->camera : NULL; }

static void vk_renderer_set_clear_color(Qs_Renderer *r, VkClearColorValue c)
{ if (r) r->clear_color = c; }

static Qs_RenderNode *vk_renderer_add_node(Qs_Renderer *r, const Qs_RenderNodeDesc *desc)
{
    if (!r || !desc || !desc->execute) return NULL;
    if (r->node_count >= QS_MAX_RENDER_NODES) {
        QS_LOG_WARN("VkRenderer '%s': render node limit reached", r->name);
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

static void vk_renderer_remove_node(Qs_Renderer *r, Qs_RenderNode *node)
{
    if (!r || !node) return;
    for (uint32_t i = 0; i < r->node_count; i++) {
        if (&r->nodes[i] == node) {
            memmove(&r->nodes[i], &r->nodes[i + 1],
                    (r->node_count - i - 1) * sizeof(Qs_RenderNode));
            r->node_count--;
            return;
        }
    }
}

static const char *vk_renderer_name(const Qs_Renderer *r)    { return r ? r->name : NULL; }
static VkDevice    vk_renderer_device(const Qs_Renderer *r)   { return r ? r->device : VK_NULL_HANDLE; }

static void vk_renderer_extents(const Qs_Renderer *r,
                                  uint32_t *out_w, uint32_t *out_h)
{
    if (out_w) *out_w = r ? r->fb_width  : 0;
    if (out_h) *out_h = r ? r->fb_height : 0;
}

/* ================================================================
   LIGHT SUBMISSION
   ================================================================ */

static void vk_submit_light(Qs_Renderer *r, Qs_Light *light)
{
    if (!r || !vk_light_is_active(light)) return;
    if (r->light_count < QS_MAX_LIGHTS_PER_RENDERER)
        vk_light_pack_gpu(light, &r->lights[r->light_count++]);
}

static void vk_clear_lights(Qs_Renderer *r)
{
    if (r) r->light_count = 0;
}

static const Qs_LightGPU *vk_get_lights(const Qs_Renderer *r, uint32_t *out_count)
{
    if (!r || r->light_count == 0) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    if (out_count) *out_count = r->light_count;
    return r->lights;
}

/* ================================================================
   BACKEND STRUCT
   ================================================================ */

const Qs_RendererBackend vk_renderer_backend = {
    .name                    = "Vulkan/PBR",
    .init                    = vk_render_init,
    .shutdown                = vk_render_shutdown,
    .update                  = vk_render_update,
    .renderer_create         = vk_renderer_create,
    .renderer_destroy        = vk_renderer_destroy,
    .renderer_bind           = vk_renderer_bind,
    .renderer_camera         = vk_renderer_camera,
    .renderer_set_clear_color= vk_renderer_set_clear_color,
    .renderer_add_node       = vk_renderer_add_node,
    .renderer_remove_node    = vk_renderer_remove_node,
    .renderer_name           = vk_renderer_name,
    .renderer_device         = vk_renderer_device,
    .renderer_extents        = vk_renderer_extents,
    .submit_light            = vk_submit_light,
    .clear_lights            = vk_clear_lights,
    .get_lights              = vk_get_lights,
    .forward_init            = vk_forward_init_impl,
    .forward_shutdown        = vk_forward_shutdown_impl,
};
