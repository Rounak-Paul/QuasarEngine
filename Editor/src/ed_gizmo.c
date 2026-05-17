#include "ed_gizmo.h"
#include "ed_pick.h"
#include "ui/ed_toolbar.h"
#include "editor.h"
#include "ed_commands.h"
#include "qs_input.h"
#include "qs_math.h"
#include "qs_renderer.h"
#include "qs_scene.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

/* ================================================================
   Convenience aliases for readability (forward to qs_math.h)
   ================================================================ */

#define v3_set    qs_v3_set
#define v3_copy   qs_v3_copy
#define v3_sub    qs_v3_sub
#define v3_add    qs_v3_add
#define v3_scale  qs_v3_scale
#define v3_dot    qs_v3_dot
#define v3_len    qs_v3_len
#define v3_norm   qs_v3_norm
#define v3_cross  qs_v3_cross

/* ================================================================
   Build view + projection from Qs_Camera
   ================================================================ */

static void compute_view_proj(const Qs_Camera *cam, uint32_t w, uint32_t h,
                              float view[16], float proj[16])
{
    qs_m4_look_at(view, cam->position, cam->target, cam->up);
    float aspect = (h > 0) ? (float)w / (float)h : 1.0f;
    float fov = cam->fov_deg > 0.0f ? cam->fov_deg : 60.0f;
    float near_p = cam->near_plane != 0.0f ? cam->near_plane : 0.1f;
    float far_p  = cam->far_plane  != 0.0f ? cam->far_plane  : 1000.0f;
    qs_m4_perspective(proj, qs_to_rad(fov), aspect, near_p, far_p);
}

/* ================================================================
   Ray from screen (NDC) coordinates
   ================================================================ */

static void screen_to_ray(const float view[16], const float proj[16],
                          float ndc_x, float ndc_y,
                          const float cam_pos[3],
                          float ray_o[3], float ray_d[3])
{
    float vp[16], ivp[16];
    qs_m4_mul(proj, view, vp);
    if (!qs_m4_inverse(vp, ivp)) {
        v3_copy(cam_pos, ray_o);
        v3_set(ray_d, 0, 0, -1);
        return;
    }

    float near4[4] = { ndc_x, ndc_y, 0.0f, 1.0f };
    float far4[4]  = { ndc_x, ndc_y, 1.0f, 1.0f };
    float wn[4], wf[4];
    qs_m4_mul_v4(ivp, near4, wn);
    qs_m4_mul_v4(ivp, far4,  wf);

    if (qs_absf(wn[3]) > 1e-7f) { wn[0]/=wn[3]; wn[1]/=wn[3]; wn[2]/=wn[3]; }
    if (qs_absf(wf[3]) > 1e-7f) { wf[0]/=wf[3]; wf[1]/=wf[3]; wf[2]/=wf[3]; }

    v3_copy(cam_pos, ray_o);
    float d[3]; v3_sub(wf, wn, d); v3_norm(d, ray_d);
}

/* ================================================================
   Closest point between two rays — used for gizmo axis picking
   Returns parameter t2 along ray2  (and distance between rays).
   ================================================================ */

static float ray_ray_closest(const float o1[3], const float d1[3],
                             const float o2[3], const float d2[3],
                             float *out_dist)
{
    float w[3]; v3_sub(o1, o2, w);
    float a = v3_dot(d1, d1);
    float b = v3_dot(d1, d2);
    float c = v3_dot(d2, d2);
    float d = v3_dot(d1, w);
    float e = v3_dot(d2, w);
    float denom = a * c - b * b;
    float t2 = 0.0f;
    if (qs_absf(denom) > 1e-7f) {
        t2 = (a * e - b * d) / denom;
    }

    float p1[3], p2[3], diff[3];
    float t1 = (qs_absf(denom) > 1e-7f) ? (b * e - c * d) / denom : 0.0f;
    v3_scale(d1, t1, p1); v3_add(o1, p1, p1);
    v3_scale(d2, t2, p2); v3_add(o2, p2, p2);
    v3_sub(p1, p2, diff);
    *out_dist = v3_len(diff);
    return t2;
}

/* ================================================================
   Gizmo vertex type
   ================================================================ */

typedef struct {
    float pos[3];
    float col[4];
} GizmoVert;

/* ================================================================
   Static state
   ================================================================ */

#define GIZMO_MAX_VERTS 4096

/* Proportions (fractions of gizmo size) */
#define GIZMO_SHAFT_WIDTH   0.042f
#define GIZMO_SHAFT_OFFSET  0.06f   /* gap at center */
#define GIZMO_CONE_LENGTH   0.22f
#define GIZMO_CONE_RADIUS   0.075f
#define GIZMO_CONE_SEGS     12
#define GIZMO_RING_WIDTH    0.038f
#define GIZMO_RING_SEGS     64
#define GIZMO_CUBE_HALF     0.065f

static EdGizmoMode  s_mode = ED_GIZMO_TRANSLATE;
static Qs_Engine   *s_engine;

/* GPU resources */
static Qs_GpuPipeline       *s_pipeline;
static Qs_GpuPipelineLayout *s_layout;
static Qs_GpuBuffer         *s_vbuf;
static Qs_RenderNode        *s_node;

/* Frame geometry */
static GizmoVert s_verts[GIZMO_MAX_VERTS];
static uint32_t  s_vert_count;

/* Interaction state */
static int   s_hover_axis = -1;   /* axis the mouse hovers over */
static int   s_drag_axis  = -1;   /* axis being dragged (-1 = none) */

/* Drag reference values (captured at drag start for stable interaction) */
static float s_drag_start_pos[3]; /* entity world position at drag start */
static float s_drag_start_rot[4]; /* entity rotation at drag start */
static float s_drag_origin_val;   /* initial projection / angle */
static float s_drag_start_comp;   /* initial transform component value */

/* Full transform snapshot taken at drag begin so we can push a single
   undo command on release, regardless of which axis / mode was used. */
static Qs_Transform s_drag_start_xform;
static Qs_Entity    s_drag_entity;

/* Per-frame camera position for geometry builders */
static float s_cam_pos[3];

/* ================================================================
   Axis definitions
   ================================================================ */

static const float AXIS_DIR[3][3] = {
    { 1, 0, 0 },   /* X */
    { 0, 1, 0 },   /* Y */
    { 0, 0, 1 },   /* Z */
};

static const float AXIS_COL[3][4] = {
    { 0.91f, 0.27f, 0.33f, 1.0f },  /* X — red   */
    { 0.45f, 0.85f, 0.20f, 1.0f },  /* Y — green */
    { 0.20f, 0.51f, 0.98f, 1.0f },  /* Z — blue  */
};

static const float HIGHLIGHT_COL[4] = { 1.0f, 0.92f, 0.016f, 1.0f };

static const float *axis_color(int axis, int highlight)
{
    return (axis == highlight) ? HIGHLIGHT_COL : AXIS_COL[axis];
}

/* ================================================================
   GLSL shaders
   ================================================================ */

static const char *GIZMO_VERT_SRC =
    "#version 450\n"
    "layout(push_constant) uniform PC { mat4 mvp; };\n"
    "layout(location=0) in vec3 in_pos;\n"
    "layout(location=1) in vec4 in_col;\n"
    "layout(location=0) out vec4 v_col;\n"
    "void main() {\n"
    "    gl_Position = mvp * vec4(in_pos, 1.0);\n"
    "    v_col = in_col;\n"
    "}\n";

static const char *GIZMO_FRAG_SRC =
    "#version 450\n"
    "layout(location=0) in vec4 v_col;\n"
    "layout(location=0) out vec4 out_color;\n"
    "void main() {\n"
    "    out_color = v_col;\n"
    "}\n";

/* ================================================================
   Geometry primitives  (triangle-based for bold visuals)
   ================================================================ */

static void push_vert(const float pos[3], const float col[4])
{
    if (s_vert_count >= GIZMO_MAX_VERTS) return;
    GizmoVert *v = &s_verts[s_vert_count++];
    v3_copy(pos, v->pos);
    memcpy(v->col, col, 16);
}

static void push_tri(const float a[3], const float b[3], const float c[3],
                     const float col[4])
{
    push_vert(a, col);
    push_vert(b, col);
    push_vert(c, col);
}

/* Camera-facing quad between two world-space points. */
static void push_thick_line(const float a[3], const float b[3],
                            const float col[4], float thickness)
{
    float dir[3]; v3_sub(b, a, dir);
    float mid[3] = { (a[0]+b[0])*0.5f, (a[1]+b[1])*0.5f, (a[2]+b[2])*0.5f };
    float to_cam[3]; v3_sub(s_cam_pos, mid, to_cam);
    float perp[3]; v3_cross(dir, to_cam, perp);
    float pl = v3_len(perp);
    if (pl < 1e-7f) return;
    v3_scale(perp, thickness * 0.5f / pl, perp);

    float a0[3], a1[3], b0[3], b1[3];
    v3_sub(a, perp, a0); v3_add(a, perp, a1);
    v3_sub(b, perp, b0); v3_add(b, perp, b1);
    push_tri(a0, a1, b1, col);
    push_tri(a0, b1, b0, col);
}

/* Solid cone from base to tip. */
static void push_cone(const float base[3], const float tip[3],
                      float radius, const float col[4], int segs)
{
    float axis[3]; v3_sub(tip, base, axis); v3_norm(axis, axis);
    float ref[3] = {0, 1, 0};
    if (qs_absf(v3_dot(axis, ref)) > 0.9f) v3_set(ref, 1, 0, 0);
    float u[3], w[3];
    v3_cross(axis, ref, u); v3_norm(u, u);
    v3_cross(axis, u, w);

    for (int i = 0; i < segs; i++) {
        float a0 = (float)i / segs * 2.0f * QS_PI;
        float a1 = (float)(i + 1) / segs * 2.0f * QS_PI;
        float c0 = cosf(a0), s0 = sinf(a0);
        float c1 = cosf(a1), s1 = sinf(a1);
        float p0[3], p1[3];
        for (int j = 0; j < 3; j++) {
            p0[j] = base[j] + (c0 * u[j] + s0 * w[j]) * radius;
            p1[j] = base[j] + (c1 * u[j] + s1 * w[j]) * radius;
        }
        push_tri(tip, p0, p1, col);
        push_tri(base, p1, p0, col);
    }
}

/* Solid axis-aligned cube at center. */
static void push_cube(const float center[3], float half, const float col[4])
{
    float vt[8][3];
    for (int i = 0; i < 8; i++) {
        vt[i][0] = center[0] + ((i & 1) ? half : -half);
        vt[i][1] = center[1] + ((i & 2) ? half : -half);
        vt[i][2] = center[2] + ((i & 4) ? half : -half);
    }
    static const int faces[6][4] = {
        {0,1,3,2},{4,6,7,5},{0,4,5,1},{2,3,7,6},{0,2,6,4},{1,5,7,3}
    };
    for (int f = 0; f < 6; f++) {
        push_tri(vt[faces[f][0]], vt[faces[f][1]], vt[faces[f][2]], col);
        push_tri(vt[faces[f][0]], vt[faces[f][2]], vt[faces[f][3]], col);
    }
}

/* ================================================================
   Gizmo size from camera distance
   ================================================================ */

static float gizmo_scale(const float cam_pos[3], const float entity_pos[3])
{
    float d[3]; v3_sub(entity_pos, cam_pos, d);
    return qs_maxf(v3_len(d) * 0.12f, 0.1f);
}

/* ================================================================
   Geometry builders
   ================================================================ */

static void build_translate(const float pos[3], float size, int hl)
{
    float sw = size * GIZMO_SHAFT_WIDTH;
    float so = size * GIZMO_SHAFT_OFFSET;
    float cl = size * GIZMO_CONE_LENGTH;
    float cr = size * GIZMO_CONE_RADIUS;

    for (int i = 0; i < 3; i++) {
        const float *col = axis_color(i, hl);
        float start[3], shaft_end[3], tip[3];
        v3_scale(AXIS_DIR[i], so, start);         v3_add(pos, start, start);
        v3_scale(AXIS_DIR[i], size - cl, shaft_end); v3_add(pos, shaft_end, shaft_end);
        v3_scale(AXIS_DIR[i], size, tip);         v3_add(pos, tip, tip);

        push_thick_line(start, shaft_end, col, sw);
        push_cone(shaft_end, tip, cr, col, GIZMO_CONE_SEGS);
    }
}

static void build_rotate(const float pos[3], float size, int hl)
{
    float rw = size * GIZMO_RING_WIDTH;

    for (int axis = 0; axis < 3; axis++) {
        const float *col = axis_color(axis, hl);
        int a1 = (axis + 1) % 3;
        int a2 = (axis + 2) % 3;

        for (int s = 0; s < GIZMO_RING_SEGS; s++) {
            float t0 = (float)s       / GIZMO_RING_SEGS * 2.0f * QS_PI;
            float t1 = (float)(s + 1) / GIZMO_RING_SEGS * 2.0f * QS_PI;

            float p0[3], p1[3];
            v3_copy(pos, p0); v3_copy(pos, p1);
            p0[a1] += cosf(t0) * size;  p0[a2] += sinf(t0) * size;
            p1[a1] += cosf(t1) * size;  p1[a2] += sinf(t1) * size;

            push_thick_line(p0, p1, col, rw);
        }
    }
}

static void build_scale(const float pos[3], float size, int hl)
{
    float sw = size * GIZMO_SHAFT_WIDTH;
    float so = size * GIZMO_SHAFT_OFFSET;
    float ch = size * GIZMO_CUBE_HALF;

    for (int i = 0; i < 3; i++) {
        const float *col = axis_color(i, hl);
        float start[3], tip[3];
        v3_scale(AXIS_DIR[i], so, start); v3_add(pos, start, start);
        v3_scale(AXIS_DIR[i], size, tip); v3_add(pos, tip, tip);

        push_thick_line(start, tip, col, sw);
        push_cube(tip, ch, col);
    }
}

/* ================================================================
   Render node callback
   ================================================================ */

static void gizmo_render(const Qs_RenderContext *ctx, void *user_data)
{
    (void)user_data;
    if (s_vert_count == 0 || !s_pipeline || !s_vbuf) return;
    if (!ctx->swapchain_view || ctx->swapchain_width == 0) return;

    Qs_GpuContext *gpu = qs_engine_gpu(s_engine);
    void *mapped = qs_gpu_map_buffer(gpu, s_vbuf);
    if (!mapped) return;
    memcpy(mapped, s_verts, s_vert_count * sizeof(GizmoVert));
    qs_gpu_unmap_buffer(gpu, s_vbuf);

    qs_cmd_begin_rendering(ctx->cmd, &(Qs_GpuRenderTarget){
        .color      = ctx->swapchain_view,
        .depth      = NULL,
        .load_color = true,
        .width      = ctx->swapchain_width,
        .height     = ctx->swapchain_height,
    });

    qs_cmd_set_viewport(ctx->cmd, ctx->swapchain_width, ctx->swapchain_height);
    qs_cmd_bind_pipeline(ctx->cmd, s_pipeline);

    float mvp[16];
    qs_m4_mul(ctx->proj, ctx->view, mvp);
    qs_cmd_push_constants(ctx->cmd, s_layout,
                          QS_GPU_SHADER_VERTEX | QS_GPU_SHADER_FRAGMENT,
                          0, 64, mvp);

    qs_cmd_bind_vertex_buffer(ctx->cmd, 0, s_vbuf, 0);
    qs_cmd_draw(ctx->cmd, s_vert_count, 0);

    qs_cmd_end_rendering(ctx->cmd);
}

/* ================================================================
   Gizmo axis picking
   ================================================================ */

/* Distance from a ray to a 3D circle (ring). */
static float ray_circle_dist(const float ray_o[3], const float ray_d[3],
                              const float center[3], const float normal[3],
                              float radius)
{
    float diff[3]; v3_sub(center, ray_o, diff);
    float denom = v3_dot(ray_d, normal);
    float hit[3];

    if (qs_absf(denom) > 1e-7f) {
        float t = v3_dot(diff, normal) / denom;
        if (t < 0.0f) t = 0.0f;
        v3_scale(ray_d, t, hit); v3_add(ray_o, hit, hit);
    } else {
        /* Ray parallel to ring plane — project ray origin onto plane */
        float d = v3_dot(diff, normal);
        for (int i = 0; i < 3; i++) hit[i] = ray_o[i] + normal[i] * d;
    }

    /* Closest point on circle to hit */
    float to_hit[3]; v3_sub(hit, center, to_hit);
    float along_n = v3_dot(to_hit, normal);
    float in_plane[3];
    for (int i = 0; i < 3; i++) in_plane[i] = to_hit[i] - along_n * normal[i];
    float ipl = v3_len(in_plane);
    if (ipl < 1e-7f) return 1e30f;

    float circle_pt[3];
    for (int i = 0; i < 3; i++)
        circle_pt[i] = center[i] + in_plane[i] * (radius / ipl);

    /* Re-project onto ray to get true 3D distance */
    float to_cp[3]; v3_sub(circle_pt, ray_o, to_cp);
    float t_ray = v3_dot(to_cp, ray_d);
    if (t_ray < 0.0f) t_ray = 0.0f;
    float rp[3]; v3_scale(ray_d, t_ray, rp); v3_add(ray_o, rp, rp);
    float d2[3]; v3_sub(rp, circle_pt, d2);
    return v3_len(d2);
}

/* Pick the closest gizmo axis.  For rotate mode tests proximity to
   each ring circle.  For translate/scale tests proximity to the axis line. */
static int pick_gizmo_axis(const float pos[3], float giz_size,
                           const float ray_o[3], const float ray_d[3])
{
    int best = -1;
    float best_dist;

    if (s_mode == ED_GIZMO_ROTATE) {
        best_dist = giz_size * 0.18f;
        for (int i = 0; i < 3; i++) {
            float d = ray_circle_dist(ray_o, ray_d, pos, AXIS_DIR[i], giz_size);
            if (d < best_dist) { best_dist = d; best = i; }
        }
    } else {
        best_dist = giz_size * 0.12f;
        for (int i = 0; i < 3; i++) {
            float dist;
            float t2 = ray_ray_closest(ray_o, ray_d, pos, AXIS_DIR[i], &dist);
            if (t2 > 0.0f && t2 < giz_size * 1.1f && dist < best_dist) {
                best_dist = dist; best = i;
            }
        }
    }
    return best;
}

/* ================================================================
   Drag projection helpers
   ================================================================ */

/* Project the mouse ray onto the best plane for axis-aligned dragging.
   Returns the signed distance along the axis from the pivot.
   pivot is the entity position captured at drag start (fixed reference). */
static float project_on_axis_plane(const float ray_o[3], const float ray_d[3],
                                   const float pivot[3], int axis)
{
    float ax[3]; v3_copy(AXIS_DIR[axis], ax);
    float vd[3]; v3_sub(pivot, s_cam_pos, vd); v3_norm(vd, vd);

    /* Plane normal: perpendicular to axis, most facing camera.
       n = normalize(axis × (view × axis)) */
    float temp[3]; v3_cross(vd, ax, temp);
    float pn[3];   v3_cross(ax, temp, pn);
    float pnl = v3_len(pn);

    if (pnl < 1e-6f) {
        int alt = (axis + 1) % 3;
        v3_copy(AXIS_DIR[alt], pn);
    } else {
        v3_scale(pn, 1.0f / pnl, pn);
    }

    float denom = v3_dot(ray_d, pn);
    if (qs_absf(denom) < 1e-7f) {
        float dist;
        return ray_ray_closest(ray_o, ray_d, pivot, ax, &dist);
    }

    float diff[3]; v3_sub(pivot, ray_o, diff);
    float t = v3_dot(diff, pn) / denom;
    float hit[3]; v3_scale(ray_d, t, hit); v3_add(ray_o, hit, hit);
    float to_hit[3]; v3_sub(hit, pivot, to_hit);
    return v3_dot(to_hit, ax);
}

/* Compute angle on the rotation ring plane for rotation dragging. */
static float rotation_angle(const float ray_o[3], const float ray_d[3],
                             const float center[3], int axis)
{
    float denom = v3_dot(ray_d, AXIS_DIR[axis]);
    if (qs_absf(denom) < 1e-7f) return 0.0f;

    float diff[3]; v3_sub(center, ray_o, diff);
    float t = v3_dot(diff, AXIS_DIR[axis]) / denom;
    float hit[3]; v3_scale(ray_d, t, hit); v3_add(ray_o, hit, hit);
    float to_hit[3]; v3_sub(hit, center, to_hit);

    int a1 = (axis + 1) % 3, a2 = (axis + 2) % 3;
    return atan2f(to_hit[a2], to_hit[a1]);
}

/* ================================================================
   Public API
   ================================================================ */

EdGizmoMode ed_gizmo_mode(void)        { return s_mode; }
void ed_gizmo_set_mode(EdGizmoMode m)  { s_mode = m;    }

void ed_gizmo_init(Qs_Engine *engine)
{
    s_engine = engine;
    Qs_GpuContext *gpu = qs_engine_gpu(engine);

    Qs_GpuShader *vs = qs_gpu_compile_shader(gpu, GIZMO_VERT_SRC, QS_GPU_SHADER_VERTEX);
    Qs_GpuShader *fs = qs_gpu_compile_shader(gpu, GIZMO_FRAG_SRC, QS_GPU_SHADER_FRAGMENT);
    if (!vs || !fs) {
        QS_LOG_ERROR("Gizmo: shader compilation failed");
        if (vs) qs_gpu_destroy_shader(gpu, vs);
        if (fs) qs_gpu_destroy_shader(gpu, fs);
        return;
    }

    Qs_GpuPushConstantRange pc = {
        .stages = QS_GPU_SHADER_VERTEX | QS_GPU_SHADER_FRAGMENT,
        .offset = 0, .size = 64,
    };
    s_layout = qs_gpu_create_pipeline_layout(gpu, &(Qs_GpuPipelineLayoutDesc){
        .push_constants = &pc, .push_constant_count = 1,
    });

    Qs_GpuVertexAttribute attrs[2] = {
        { .location = 0, .format = QS_GPU_VERTEX_FORMAT_FLOAT3,
          .offset = offsetof(GizmoVert, pos) },
        { .location = 1, .format = QS_GPU_VERTEX_FORMAT_FLOAT4,
          .offset = offsetof(GizmoVert, col) },
    };
    Qs_GpuVertexBinding vb = {
        .binding = 0, .stride = sizeof(GizmoVert),
        .attributes = attrs, .attribute_count = 2,
    };

    s_pipeline = qs_gpu_create_graphics_pipeline(gpu, &(Qs_GpuGraphicsPipelineDesc){
        .layout               = s_layout,
        .vertex_shader        = vs,
        .fragment_shader      = fs,
        .vertex_bindings      = &vb,
        .vertex_binding_count = 1,
        .topology             = QS_GPU_TOPOLOGY_TRIANGLES,
        .cull_mode            = QS_GPU_CULL_NONE,
        .depth_test           = false,
        .depth_write          = false,
        .color_format         = QS_GPU_FORMAT_BGRA8_UNORM,
        .depth_format         = QS_GPU_FORMAT_DEPTH_AUTO,
    });

    qs_gpu_destroy_shader(gpu, vs);
    qs_gpu_destroy_shader(gpu, fs);

    if (!s_pipeline) {
        QS_LOG_ERROR("Gizmo: pipeline creation failed");
        qs_gpu_destroy_pipeline_layout(gpu, s_layout);
        s_layout = NULL;
        return;
    }

    s_vbuf = qs_gpu_create_buffer(gpu, &(Qs_GpuBufferDesc){
        .size   = GIZMO_MAX_VERTS * sizeof(GizmoVert),
        .usage  = QS_GPU_BUFFER_VERTEX,
        .memory = QS_GPU_MEMORY_HOST_VISIBLE,
    });

    s_vert_count  = 0;
    s_hover_axis  = -1;
    s_drag_axis   = -1;
}

void ed_gizmo_shutdown(Qs_Engine *engine)
{
    Qs_GpuContext *gpu = qs_engine_gpu(engine);
    if (s_vbuf)     { qs_gpu_destroy_buffer(gpu, s_vbuf);            s_vbuf     = NULL; }
    if (s_pipeline) { qs_gpu_destroy_pipeline(gpu, s_pipeline);      s_pipeline = NULL; }
    if (s_layout)   { qs_gpu_destroy_pipeline_layout(gpu, s_layout); s_layout   = NULL; }
    s_node   = NULL;
    s_engine = NULL;
}

void ed_gizmo_attach(Qs_Renderer *renderer)
{
    if (!renderer || !s_pipeline) return;
    s_node = qs_renderer_add_node(renderer, &(Qs_RenderNodeDesc){
        .name     = "editor_gizmo",
        .priority = 400,
        .execute  = gizmo_render,
    });
}

void ed_gizmo_update(void *editor, float dt)
{
    (void)dt;
    Editor *ed = (Editor *)editor;

    Qs_Renderer *renderer = editor_scene_renderer(ed);
    if (!renderer || !s_pipeline) { s_vert_count = 0; return; }

    Ca_Viewport *vp = editor_scene_viewport(ed);
    if (!vp) { s_vert_count = 0; return; }

    float vp_x, vp_y, vp_w, vp_h;
    ca_viewport_screen_rect(vp, &vp_x, &vp_y, &vp_w, &vp_h);
    if (vp_w <= 0 || vp_h <= 0) { s_vert_count = 0; return; }

    Qs_Camera *cam = qs_renderer_camera(renderer);
    if (!cam) { s_vert_count = 0; return; }

    v3_copy(cam->position, s_cam_pos);

    float view[16], proj[16];
    compute_view_proj(cam, (uint32_t)vp_w, (uint32_t)vp_h, view, proj);

    float mx, my;
    qs_input_mouse_pos(&mx, &my);
    bool in_viewport = (mx >= vp_x && mx < vp_x + vp_w &&
                        my >= vp_y && my < vp_y + vp_h);

    float ndc_x = 2.0f * (mx - vp_x) / vp_w - 1.0f;
    float ndc_y = 2.0f * (my - vp_y) / vp_h - 1.0f;

    float ray_o[3], ray_d[3];
    screen_to_ray(view, proj, ndc_x, ndc_y, cam->position, ray_o, ray_d);

    bool cam_active = qs_input_key_down(QS_KEY_LEFT_ALT)  ||
                      qs_input_key_down(QS_KEY_RIGHT_ALT) ||
                      qs_input_mouse_down(QS_MOUSE_RIGHT) ||
                      qs_input_mouse_down(QS_MOUSE_MIDDLE);

    Qs_Entity sel = editor_selected_entity(ed);
    Qs_Scene *scene = qs_scene_active();
    Qs_Transform *sel_tr = NULL;
    float gsize = 0.0f;

    if (sel != QS_ENTITY_INVALID && scene) {
        sel_tr = qs_entity_get(scene, sel, qs_transform_type());
        if (sel_tr) gsize = gizmo_scale(cam->position, sel_tr->position);
    }

    /* ---- Hover detection ---- */
    if (s_drag_axis < 0 && !cam_active && in_viewport && sel_tr)
        s_hover_axis = pick_gizmo_axis(sel_tr->position, gsize, ray_o, ray_d);
    else if (s_drag_axis < 0)
        s_hover_axis = -1;

    /* ---- Input handling ---- */
    if (in_viewport && !cam_active) {
        if (qs_input_mouse_pressed(QS_MOUSE_LEFT)) {
            /* Try gizmo handle first */
            if (sel_tr) {
                int axis = pick_gizmo_axis(sel_tr->position, gsize, ray_o, ray_d);
                if (axis >= 0) {
                    s_drag_axis  = axis;
                    s_hover_axis = axis;
                    v3_copy(sel_tr->position, s_drag_start_pos);
                    memcpy(s_drag_start_rot, sel_tr->rotation, 16);
                    s_drag_start_xform = *sel_tr;
                    s_drag_entity      = editor_selected_entity(ed);

                    if (s_mode == ED_GIZMO_ROTATE) {
                        s_drag_origin_val = rotation_angle(ray_o, ray_d,
                                                           s_drag_start_pos, axis);
                    } else {
                        s_drag_origin_val = project_on_axis_plane(ray_o, ray_d,
                                                                  s_drag_start_pos, axis);
                    }
                    s_drag_start_comp = (s_mode == ED_GIZMO_SCALE)
                                            ? sel_tr->scale[axis]
                                            : sel_tr->position[axis];
                    goto build;
                }
            }

            /* No gizmo hit — pixel-perfect GPU entity pick */
            float nx = (mx - vp_x) / vp_w;
            float ny = (my - vp_y) / vp_h;
            Qs_Entity hit = ed_pick_entity_at(s_engine, nx, ny);
            editor_set_selected_entity(ed, hit);
            s_drag_axis  = -1;
            s_hover_axis = -1;
        }

        /* Drag in progress */
        if (qs_input_mouse_down(QS_MOUSE_LEFT) && s_drag_axis >= 0 && sel_tr) {
            int axis = s_drag_axis;

            if (s_mode == ED_GIZMO_TRANSLATE) {
                float cur = project_on_axis_plane(ray_o, ray_d,
                                                  s_drag_start_pos, axis);
                sel_tr->position[axis] = s_drag_start_comp + (cur - s_drag_origin_val);

            } else if (s_mode == ED_GIZMO_ROTATE) {
                float cur = rotation_angle(ray_o, ray_d, s_drag_start_pos, axis);
                float delta = cur - s_drag_origin_val;
                /* Wrap to [-PI, PI] */
                while (delta >  QS_PI) delta -= 2.0f * QS_PI;
                while (delta < -QS_PI) delta += 2.0f * QS_PI;

                float half = delta * 0.5f;
                float sq = sinf(half), cq = cosf(half);
                float dq[4] = {0, 0, 0, cq};
                dq[axis] = sq;

                /* current = delta_q * start_rot */
                float rx = s_drag_start_rot[0], ry = s_drag_start_rot[1];
                float rz = s_drag_start_rot[2], rw = s_drag_start_rot[3];
                sel_tr->rotation[0] = dq[3]*rx + dq[0]*rw + dq[1]*rz - dq[2]*ry;
                sel_tr->rotation[1] = dq[3]*ry - dq[0]*rz + dq[1]*rw + dq[2]*rx;
                sel_tr->rotation[2] = dq[3]*rz + dq[0]*ry - dq[1]*rx + dq[2]*rw;
                sel_tr->rotation[3] = dq[3]*rw - dq[0]*rx - dq[1]*ry - dq[2]*rz;
                /* Normalize */
                float l = qs_safe_sqrtf(sel_tr->rotation[0]*sel_tr->rotation[0] +
                                      sel_tr->rotation[1]*sel_tr->rotation[1] +
                                      sel_tr->rotation[2]*sel_tr->rotation[2] +
                                      sel_tr->rotation[3]*sel_tr->rotation[3]);
                if (l > 1e-7f) {
                    float il = 1.0f / l;
                    sel_tr->rotation[0] *= il; sel_tr->rotation[1] *= il;
                    sel_tr->rotation[2] *= il; sel_tr->rotation[3] *= il;
                }

            } else { /* SCALE */
                float cur = project_on_axis_plane(ray_o, ray_d,
                                                  s_drag_start_pos, axis);
                float ns = s_drag_start_comp + (cur - s_drag_origin_val);
                if (ns < 0.01f) ns = 0.01f;
                sel_tr->scale[axis] = ns;
            }
        }

        if (qs_input_mouse_released(QS_MOUSE_LEFT)) {
            /* Push a single undo command for the entire drag. */
            if (s_drag_axis >= 0 && sel_tr &&
                s_drag_entity != QS_ENTITY_INVALID) {
                Qs_Scene *scene = qs_scene_active();
                if (memcmp(&s_drag_start_xform, sel_tr, sizeof(Qs_Transform)) != 0) {
                    ed_undo_push_transform(scene, s_drag_entity,
                                           &s_drag_start_xform, sel_tr);
                    editor_mark_dirty(ed);
                }
            }
            s_drag_axis = -1;
        }

    } else if (cam_active) {
        s_drag_axis = -1;
    }

    /* ---- Shortcut keys ---- */
    {
        EdGizmoMode prev = s_mode;
        if (qs_input_key_pressed(QS_KEY_1)) s_mode = ED_GIZMO_TRANSLATE;
        if (qs_input_key_pressed(QS_KEY_2)) s_mode = ED_GIZMO_ROTATE;
        if (qs_input_key_pressed(QS_KEY_3)) s_mode = ED_GIZMO_SCALE;
        if (s_mode != prev) ed_toolbar_sync_gizmo();
    }

build:
    /* ---- Build geometry ---- */
    s_vert_count = 0;
    if (sel_tr) {
        int hl = (s_drag_axis >= 0) ? s_drag_axis : s_hover_axis;
        float size = gizmo_scale(cam->position, sel_tr->position);
        switch (s_mode) {
        case ED_GIZMO_TRANSLATE: build_translate(sel_tr->position, size, hl); break;
        case ED_GIZMO_ROTATE:    build_rotate(sel_tr->position, size, hl);    break;
        case ED_GIZMO_SCALE:     build_scale(sel_tr->position, size, hl);     break;
        }
    }
}
