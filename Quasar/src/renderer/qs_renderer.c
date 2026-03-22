#include "qs_renderer.h"
#include "qs_log.h"
#include "qs_system.h"
#include "causality.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ================================================================
   LIMITS
   ================================================================ */

#define QS_MAX_RENDERERS       32
#define QS_MAX_RENDER_NODES    16

/* ================================================================
   INTERNAL TYPES
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

    /* GPU resources (shared refs — not owned) */
    VkDevice          device;
    VkPhysicalDevice  physical_device;

    /* Per-renderer GPU resources (owned) */
    VkImage           depth_image;
    VkDeviceMemory    depth_memory;
    VkImageView       depth_view;
    bool              depth_enabled;

    VkClearColorValue clear_color;
    uint32_t          fb_width;
    uint32_t          fb_height;

    /* Camera */
    Qs_Camera         camera;

    /* Pipeline nodes */
    Qs_RenderNode     nodes[QS_MAX_RENDER_NODES];
    uint32_t          node_count;

    /* Back-pointer for system access */
    Ca_Instance      *ca_instance;

    /* Delta time (set each frame by system update) */
    float             dt;
};

/// Render system data — stored in the engine system manager.
typedef struct Qs_RenderSystemData {
    Ca_Instance  *ca_instance;
    VkDevice      device;
    VkPhysicalDevice physical_device;
    Qs_Renderer   renderers[QS_MAX_RENDERERS];
    uint32_t      count;
    float         dt;
} Qs_RenderSystemData;

/* Global pointer set during system init, used by qs_renderer_create */
static Qs_RenderSystemData *g_render_system;

/* ================================================================
   MATH HELPERS — column-major 4x4
   ================================================================ */

static void mat4_identity(float m[16])
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_look_at(float out[16],
                          const float eye[3],
                          const float center[3],
                          const float up[3])
{
    float fx = center[0] - eye[0];
    float fy = center[1] - eye[1];
    float fz = center[2] - eye[2];
    float len = sqrtf(fx*fx + fy*fy + fz*fz);
    if (len > 1e-6f) { fx /= len; fy /= len; fz /= len; }

    /* side = forward × up */
    float sx = fy * up[2] - fz * up[1];
    float sy = fz * up[0] - fx * up[2];
    float sz = fx * up[1] - fy * up[0];
    len = sqrtf(sx*sx + sy*sy + sz*sz);
    if (len > 1e-6f) { sx /= len; sy /= len; sz /= len; }

    /* true up = side × forward */
    float ux = sy * fz - sz * fy;
    float uy = sz * fx - sx * fz;
    float uz = sx * fy - sy * fx;

    memset(out, 0, 16 * sizeof(float));
    out[0] = sx;  out[4] = sy;  out[8]  = sz;  out[12] = -(sx*eye[0] + sy*eye[1] + sz*eye[2]);
    out[1] = ux;  out[5] = uy;  out[9]  = uz;  out[13] = -(ux*eye[0] + uy*eye[1] + uz*eye[2]);
    out[2] = -fx; out[6] = -fy; out[10] = -fz; out[14] =  (fx*eye[0] + fy*eye[1] + fz*eye[2]);
    out[3] = 0;   out[7] = 0;   out[11] = 0;   out[15] = 1.0f;
}

static void mat4_perspective(float out[16],
                              float fov_rad, float aspect,
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

static void mat4_ortho(float out[16],
                        float half_h, float aspect,
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

static uint32_t find_memory_type(VkPhysicalDevice pd, uint32_t type_bits,
                                  VkMemoryPropertyFlags flags)
{
    VkPhysicalDeviceMemoryProperties mem;
    vkGetPhysicalDeviceMemoryProperties(pd, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) &&
            (mem.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    }
    return UINT32_MAX;
}

static void destroy_depth(Qs_Renderer *r)
{
    if (r->depth_view)   { vkDestroyImageView(r->device, r->depth_view, NULL); r->depth_view = VK_NULL_HANDLE; }
    if (r->depth_image)  { vkDestroyImage(r->device, r->depth_image, NULL);    r->depth_image = VK_NULL_HANDLE; }
    if (r->depth_memory) { vkFreeMemory(r->device, r->depth_memory, NULL);     r->depth_memory = VK_NULL_HANDLE; }
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
    if (vkCreateImage(r->device, &ci, NULL, &r->depth_image) != VK_SUCCESS)
        return false;

    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(r->device, r->depth_image, &req);
    uint32_t mi = find_memory_type(r->physical_device, req.memoryTypeBits,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mi == UINT32_MAX) { vkDestroyImage(r->device, r->depth_image, NULL); r->depth_image = VK_NULL_HANDLE; return false; }

    VkMemoryAllocateInfo ai = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = req.size,
        .memoryTypeIndex = mi,
    };
    if (vkAllocateMemory(r->device, &ai, NULL, &r->depth_memory) != VK_SUCCESS) {
        vkDestroyImage(r->device, r->depth_image, NULL);
        r->depth_image = VK_NULL_HANDLE;
        return false;
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
        destroy_depth(r);
        return false;
    }
    return true;
}

/* ================================================================
   DEPTH BUFFER RESIZE
   ================================================================ */

static void recreate_depth_buffer(Qs_Renderer *r, uint32_t w, uint32_t h)
{
    destroy_depth(r);
    if (w == 0 || h == 0) return;
    create_depth(r, w, h);
    r->fb_width  = w;
    r->fb_height = h;
}

/* ================================================================
   CAMERA MATRICES
   ================================================================ */

static void compute_matrices(const Qs_Camera *cam, uint32_t w, uint32_t h,
                              float view[16], float proj[16])
{
    mat4_look_at(view, cam->position, cam->target, cam->up);

    float aspect = (h > 0) ? (float)w / (float)h : 1.0f;

    if (cam->projection == QS_PROJECTION_ORTHOGRAPHIC) {
        float half_h = cam->ortho_size > 0.0f ? cam->ortho_size : 5.0f;
        float near_p = cam->near_plane != 0.0f ? cam->near_plane : 0.1f;
        float far_p  = cam->far_plane  != 0.0f ? cam->far_plane  : 100.0f;
        mat4_ortho(proj, half_h, aspect, near_p, far_p);
    } else {
        float fov = cam->fov_deg > 0.0f ? cam->fov_deg : 60.0f;
        float near_p = cam->near_plane != 0.0f ? cam->near_plane : 0.1f;
        float far_p  = cam->far_plane  != 0.0f ? cam->far_plane  : 1000.0f;
        mat4_perspective(proj, fov * 3.14159265f / 180.0f, aspect, near_p, far_p);
    }
}

/* ================================================================
   VIEWPORT CALLBACKS
   ================================================================ */

static int node_compare(const void *a, const void *b)
{
    const Qs_RenderNode *na = (const Qs_RenderNode *)a;
    const Qs_RenderNode *nb = (const Qs_RenderNode *)b;
    return (na->priority > nb->priority) - (na->priority < nb->priority);
}

static void on_render(Ca_Viewport *vp, void *user_data)
{
    Qs_Renderer *r = (Qs_Renderer *)user_data;
    uint32_t w = ca_viewport_width(vp);
    uint32_t h = ca_viewport_height(vp);
    if (w == 0 || h == 0) return;

    /* Recreate depth buffer on resize */
    if (r->depth_enabled && (w != r->fb_width || h != r->fb_height))
        recreate_depth_buffer(r, w, h);

    if (!r->depth_enabled) {
        r->fb_width  = w;
        r->fb_height = h;
    }

    VkCommandBuffer cmd = ca_viewport_cmd(vp);

    /* --- Dynamic rendering (Vulkan 1.3) --- */

    /* Color attachment */
    VkRenderingAttachmentInfo color_att = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = ca_viewport_image_view(vp),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue  = { .color = r->clear_color },
    };

    /* Depth attachment (optional) */
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
        .renderArea           = { .offset = {0, 0}, .extent = {w, h} },
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &color_att,
        .pDepthAttachment     = (r->depth_enabled && r->depth_view) ? &depth_att : NULL,
    };

    vkCmdBeginRendering(cmd, &rendering_info);

    VkViewport viewport = {
        .x = 0, .y = 0,
        .width  = (float)w,
        .height = (float)h,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor = { .offset = {0, 0}, .extent = {w, h} };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    /* Compute camera matrices */
    Qs_RenderContext ctx = {
        .renderer = r,
        .cmd      = cmd,
        .width    = w,
        .height   = h,
        .dt       = r->dt,
    };
    compute_matrices(&r->camera, w, h, ctx.view, ctx.proj);

    /* Execute pipeline nodes in priority order */
    for (uint32_t i = 0; i < r->node_count; i++) {
        if (r->nodes[i].active && r->nodes[i].execute)
            r->nodes[i].execute(&ctx, r->nodes[i].user_data);
    }

    vkCmdEndRendering(cmd);
}

static void on_resize(Ca_Viewport *vp, uint32_t w, uint32_t h, void *user_data)
{
    (void)vp;
    Qs_Renderer *r = (Qs_Renderer *)user_data;
    if (r->depth_enabled && w > 0 && h > 0)
        recreate_depth_buffer(r, w, h);
    else {
        r->fb_width  = w;
        r->fb_height = h;
    }
}

/* ================================================================
   RENDERER LIFECYCLE
   ================================================================ */

static void renderer_defaults(Qs_Camera *cam)
{
    if (cam->up[0] == 0.0f && cam->up[1] == 0.0f && cam->up[2] == 0.0f) {
        cam->up[1] = 1.0f;  /* Y-up default */
    }
    if (cam->position[0] == 0.0f && cam->position[1] == 0.0f && cam->position[2] == 0.0f) {
        cam->position[2] = 5.0f;
    }
}

Qs_Renderer *qs_renderer_create(Qs_Engine *engine, const Qs_RendererDesc *desc)
{
    (void)engine;
    if (!g_render_system || !desc) return NULL;

    /* Find a free slot */
    Qs_Renderer *r = NULL;
    for (uint32_t i = 0; i < QS_MAX_RENDERERS; i++) {
        if (!g_render_system->renderers[i].in_use) {
            r = &g_render_system->renderers[i];
            break;
        }
    }
    if (!r) {
        QS_LOG_ERROR("Renderer limit reached (%d)", QS_MAX_RENDERERS);
        return NULL;
    }

    memset(r, 0, sizeof(*r));
    r->in_use          = true;
    r->device          = g_render_system->device;
    r->physical_device = g_render_system->physical_device;
    r->ca_instance     = g_render_system->ca_instance;
    r->clear_color     = desc->clear_color;
    r->camera          = desc->camera;
    r->depth_enabled   = desc->depth_test;

    if (desc->name)
        snprintf(r->name, sizeof(r->name), "%s", desc->name);
    else
        snprintf(r->name, sizeof(r->name), "renderer_%u", g_render_system->count);

    renderer_defaults(&r->camera);

    g_render_system->count++;
    QS_LOG_INFO("Renderer '%s' created (depth=%s)", r->name,
                r->depth_enabled ? "on" : "off");
    return r;
}

void qs_renderer_destroy(Qs_Renderer *renderer)
{
    if (!renderer || !renderer->in_use) return;

    vkDeviceWaitIdle(renderer->device);

    destroy_depth(renderer);

    QS_LOG_INFO("Renderer '%s' destroyed", renderer->name);

    renderer->in_use = false;
    if (g_render_system && g_render_system->count > 0)
        g_render_system->count--;
}

void qs_renderer_bind(Qs_Renderer *renderer, Ca_Viewport *viewport)
{
    if (!renderer || !viewport) return;
    ca_viewport_set_callbacks(viewport,
        on_render, renderer,
        on_resize, renderer);
}

Qs_Camera *qs_renderer_camera(Qs_Renderer *renderer)
{
    return renderer ? &renderer->camera : NULL;
}

void qs_renderer_set_clear_color(Qs_Renderer *renderer, VkClearColorValue color)
{
    if (renderer) renderer->clear_color = color;
}

Qs_RenderNode *qs_renderer_add_node(Qs_Renderer *renderer,
                                     const Qs_RenderNodeDesc *desc)
{
    if (!renderer || !desc || !desc->execute) return NULL;
    if (renderer->node_count >= QS_MAX_RENDER_NODES) {
        QS_LOG_WARN("Renderer '%s': render node limit reached", renderer->name);
        return NULL;
    }

    Qs_RenderNode *node = &renderer->nodes[renderer->node_count++];
    memset(node, 0, sizeof(*node));
    node->active    = true;
    node->priority  = desc->priority;
    node->execute   = desc->execute;
    node->user_data = desc->user_data;
    if (desc->name)
        snprintf(node->name, sizeof(node->name), "%s", desc->name);

    /* Keep sorted by priority */
    qsort(renderer->nodes, renderer->node_count, sizeof(Qs_RenderNode), node_compare);

    QS_LOG_DEBUG("Renderer '%s': added node '%s' (priority %d)",
                 renderer->name, node->name, node->priority);
    return node;
}

void qs_renderer_remove_node(Qs_Renderer *renderer, Qs_RenderNode *node)
{
    if (!renderer || !node) return;

    for (uint32_t i = 0; i < renderer->node_count; i++) {
        if (&renderer->nodes[i] == node) {
            /* Shift remaining nodes down */
            memmove(&renderer->nodes[i], &renderer->nodes[i + 1],
                    (renderer->node_count - i - 1) * sizeof(Qs_RenderNode));
            renderer->node_count--;
            return;
        }
    }
}

const char *qs_renderer_name(const Qs_Renderer *renderer)
{
    return renderer ? renderer->name : NULL;
}

VkDevice qs_renderer_device(const Qs_Renderer *renderer)
{
    return renderer ? renderer->device : VK_NULL_HANDLE;
}

void qs_renderer_extents(const Qs_Renderer *renderer,
                         uint32_t *out_width, uint32_t *out_height)
{
    if (!renderer) {
        if (out_width)  *out_width  = 0;
        if (out_height) *out_height = 0;
        return;
    }
    if (out_width)  *out_width  = renderer->fb_width;
    if (out_height) *out_height = renderer->fb_height;
}

/* ================================================================
   RENDER SYSTEM — engine system callbacks
   ================================================================ */

static bool render_system_init(Qs_System *system, Qs_Engine *engine)
{
    (void)engine;
    Qs_RenderSystemData *data = (Qs_RenderSystemData *)qs_system_data(system);
    if (!data->ca_instance) return false;

    data->device          = ca_gpu_device(data->ca_instance);
    data->physical_device = ca_gpu_physical_device(data->ca_instance);
    if (!data->device || !data->physical_device) return false;

    g_render_system = data;

    QS_LOG_INFO("Render system initialized (device %p)", (void *)data->device);
    return true;
}

static void render_system_shutdown(Qs_System *system, Qs_Engine *engine)
{
    (void)engine;
    Qs_RenderSystemData *data = (Qs_RenderSystemData *)qs_system_data(system);

    /* Destroy all live renderers */
    for (uint32_t i = 0; i < QS_MAX_RENDERERS; i++) {
        if (data->renderers[i].in_use)
            qs_renderer_destroy(&data->renderers[i]);
    }

    g_render_system = NULL;
    QS_LOG_INFO("Render system shut down");
}

static void render_system_update(Qs_System *system, Qs_Engine *engine, float dt)
{
    (void)engine;
    Qs_RenderSystemData *data = (Qs_RenderSystemData *)qs_system_data(system);
    data->dt = dt;

    /* Propagate dt to all active renderers */
    for (uint32_t i = 0; i < QS_MAX_RENDERERS; i++) {
        if (data->renderers[i].in_use)
            data->renderers[i].dt = dt;
    }
}

/* Pending Ca_Instance for the init callback — set by qs_render_system_desc,
   consumed by render_system_init_wrapper. */
static Ca_Instance *s_pending_instance;

static bool render_system_init_wrapper(Qs_System *sys, Qs_Engine *eng)
{
    Qs_RenderSystemData *d = (Qs_RenderSystemData *)qs_system_data(sys);
    d->ca_instance = s_pending_instance;
    return render_system_init(sys, eng);
}

Qs_SystemDesc qs_render_system_desc(Ca_Instance *ca_instance)
{
    s_pending_instance = ca_instance;

    return (Qs_SystemDesc){
        .name      = "Render",
        .data_size = sizeof(Qs_RenderSystemData),
        .init      = render_system_init_wrapper,
        .shutdown  = render_system_shutdown,
        .update    = render_system_update,
    };
}
