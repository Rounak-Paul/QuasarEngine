#include "qs_renderer.h"
#include "causality.h"

struct Qs_Renderer {
    VkDevice          device;
    VkRenderPass      render_pass;
    VkFramebuffer     framebuffer;
    VkClearColorValue  clear_color;
    uint32_t           fb_width;
    uint32_t           fb_height;
};

static void recreate_framebuffer(Qs_Renderer *r, Ca_Viewport *vp)
{
    if (r->framebuffer) {
        vkDestroyFramebuffer(r->device, r->framebuffer, NULL);
        r->framebuffer = VK_NULL_HANDLE;
    }

    uint32_t w = ca_viewport_width(vp);
    uint32_t h = ca_viewport_height(vp);
    if (w == 0 || h == 0) return;

    VkImageView view = ca_viewport_image_view(vp);
    VkFramebufferCreateInfo fb_info = {
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass      = r->render_pass,
        .attachmentCount = 1,
        .pAttachments    = &view,
        .width           = w,
        .height          = h,
        .layers          = 1,
    };
    vkCreateFramebuffer(r->device, &fb_info, NULL, &r->framebuffer);
    r->fb_width  = w;
    r->fb_height = h;
}

static void on_render(Ca_Viewport *vp, void *user_data)
{
    Qs_Renderer *r = (Qs_Renderer *)user_data;
    uint32_t w = ca_viewport_width(vp);
    uint32_t h = ca_viewport_height(vp);
    if (w == 0 || h == 0) return;

    if (w != r->fb_width || h != r->fb_height)
        recreate_framebuffer(r, vp);
    if (!r->framebuffer) return;

    VkCommandBuffer cmd = ca_viewport_cmd(vp);

    VkRenderPassBeginInfo rp_begin = {
        .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass  = r->render_pass,
        .framebuffer = r->framebuffer,
        .renderArea  = { .offset = {0, 0}, .extent = {w, h} },
        .clearValueCount = 1,
        .pClearValues    = &(VkClearValue){ .color = r->clear_color },
    };
    vkCmdBeginRenderPass(cmd, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

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

    /* Future: bind pipeline, push constants, draw calls go here */

    vkCmdEndRenderPass(cmd);
}

static void on_resize(Ca_Viewport *vp, uint32_t w, uint32_t h, void *user_data)
{
    Qs_Renderer *r = (Qs_Renderer *)user_data;
    (void)w; (void)h;
    recreate_framebuffer(r, vp);
}

static VkRenderPass create_render_pass(VkDevice device, VkFormat format)
{
    VkAttachmentDescription color_att = {
        .format         = format,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &color_ref,
    };

    VkRenderPassCreateInfo rp_info = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments    = &color_att,
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
    };

    VkRenderPass rp = VK_NULL_HANDLE;
    vkCreateRenderPass(device, &rp_info, NULL, &rp);
    return rp;
}

Qs_Renderer *qs_renderer_create(const Qs_RendererDesc *desc)
{
    if (!desc || !desc->device) return NULL;

    Qs_Renderer *r = calloc(1, sizeof(Qs_Renderer));
    if (!r) return NULL;

    r->device      = desc->device;
    r->clear_color = desc->clear_color;

    VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;
    r->render_pass = create_render_pass(r->device, fmt);
    if (!r->render_pass) {
        free(r);
        return NULL;
    }

    return r;
}

void qs_renderer_destroy(Qs_Renderer *renderer)
{
    if (!renderer) return;
    vkDeviceWaitIdle(renderer->device);
    if (renderer->framebuffer)
        vkDestroyFramebuffer(renderer->device, renderer->framebuffer, NULL);
    if (renderer->render_pass)
        vkDestroyRenderPass(renderer->device, renderer->render_pass, NULL);
    free(renderer);
}

void qs_renderer_bind(Qs_Renderer *renderer, Ca_Viewport *viewport)
{
    if (!renderer || !viewport) return;

    ca_viewport_set_callbacks(viewport,
        on_render, renderer,
        on_resize, renderer);
}

void qs_renderer_set_clear_color(Qs_Renderer *renderer, VkClearColorValue color)
{
    if (renderer) renderer->clear_color = color;
}
