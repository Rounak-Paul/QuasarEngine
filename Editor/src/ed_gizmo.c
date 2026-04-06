#include "ed_gizmo.h"
#include "editor.h"
#include "qs_input.h"
#include "qs_renderer.h"
#include "qs_scene.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

/* ================================================================
   Math helpers (inline, no external deps)
   ================================================================ */

#ifndef QS_PI
#  define QS_PI 3.14159265358979323846f
#endif

static inline float gizmo_to_rad(float deg) { return deg * (QS_PI / 180.0f); }
static inline float gizmo_sqrtf(float v)    { return sqrtf(v < 0.0f ? 0.0f : v); }
static inline float gizmo_fabsf(float v)    { return v < 0.0f ? -v : v; }
static inline float gizmo_maxf(float a, float b) { return a > b ? a : b; }

static inline void v3_set(float out[3], float x, float y, float z)
{ out[0]=x; out[1]=y; out[2]=z; }
static inline void v3_copy(const float a[3], float b[3])
{ b[0]=a[0]; b[1]=a[1]; b[2]=a[2]; }
static inline void v3_sub(const float a[3], const float b[3], float out[3])
{ out[0]=a[0]-b[0]; out[1]=a[1]-b[1]; out[2]=a[2]-b[2]; }
static inline void v3_add(const float a[3], const float b[3], float out[3])
{ out[0]=a[0]+b[0]; out[1]=a[1]+b[1]; out[2]=a[2]+b[2]; }
static inline void v3_scale(const float v[3], float s, float out[3])
{ out[0]=v[0]*s; out[1]=v[1]*s; out[2]=v[2]*s; }
static inline float v3_dot(const float a[3], const float b[3])
{ return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static inline float v3_len(const float v[3])
{ return gizmo_sqrtf(v3_dot(v,v)); }
static inline void v3_norm(const float v[3], float out[3])
{
    float l=v3_len(v);
    if(l<1e-7f){out[0]=0;out[1]=0;out[2]=0;return;}
    float i=1.0f/l; out[0]=v[0]*i; out[1]=v[1]*i; out[2]=v[2]*i;
}
static inline void v3_cross(const float a[3], const float b[3], float out[3])
{
    out[0]=a[1]*b[2]-a[2]*b[1];
    out[1]=a[2]*b[0]-a[0]*b[2];
    out[2]=a[0]*b[1]-a[1]*b[0];
}

/* ---- mat4 (column-major) ---- */

static void m4_mul(const float a[16], const float b[16], float out[16])
{
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            out[c*4+r] = a[0*4+r]*b[c*4+0]+a[1*4+r]*b[c*4+1]
                        +a[2*4+r]*b[c*4+2]+a[3*4+r]*b[c*4+3];
}

static void m4_look_at(float out[16], const float eye[3],
                       const float center[3], const float up[3])
{
    float fx=center[0]-eye[0], fy=center[1]-eye[1], fz=center[2]-eye[2];
    float len=gizmo_sqrtf(fx*fx+fy*fy+fz*fz);
    if(len>1e-6f){fx/=len;fy/=len;fz/=len;}
    float sx=fy*up[2]-fz*up[1], sy=fz*up[0]-fx*up[2], sz=fx*up[1]-fy*up[0];
    len=gizmo_sqrtf(sx*sx+sy*sy+sz*sz);
    if(len>1e-6f){sx/=len;sy/=len;sz/=len;}
    float ux=sy*fz-sz*fy, uy=sz*fx-sx*fz, uz=sx*fy-sy*fx;
    memset(out,0,64);
    out[0]=sx; out[4]=sy; out[8] =sz; out[12]=-(sx*eye[0]+sy*eye[1]+sz*eye[2]);
    out[1]=ux; out[5]=uy; out[9] =uz; out[13]=-(ux*eye[0]+uy*eye[1]+uz*eye[2]);
    out[2]=-fx;out[6]=-fy;out[10]=-fz;out[14]= (fx*eye[0]+fy*eye[1]+fz*eye[2]);
    out[3]=0;  out[7]=0;  out[11]=0;  out[15]=1.0f;
}

static void m4_perspective(float out[16], float fov_rad, float aspect,
                           float near_p, float far_p)
{
    memset(out,0,64);
    float t=tanf(fov_rad*0.5f);
    out[0] = 1.0f/(aspect*t);
    out[5] = -1.0f/t;            /* Vulkan Y-flip */
    out[10]= -(far_p+near_p)/(far_p-near_p);
    out[11]= -1.0f;
    out[14]= -(2.0f*far_p*near_p)/(far_p-near_p);
}

static bool m4_inverse(const float m[16], float inv[16])
{
    float t[12];
    t[ 0]=m[10]*m[15]-m[14]*m[11]; t[ 1]=m[ 6]*m[15]-m[14]*m[ 7];
    t[ 2]=m[ 6]*m[11]-m[10]*m[ 7]; t[ 3]=m[ 2]*m[15]-m[14]*m[ 3];
    t[ 4]=m[ 2]*m[11]-m[10]*m[ 3]; t[ 5]=m[ 2]*m[ 7]-m[ 6]*m[ 3];
    t[ 6]=m[ 8]*m[13]-m[12]*m[ 9]; t[ 7]=m[ 4]*m[13]-m[12]*m[ 5];
    t[ 8]=m[ 4]*m[ 9]-m[ 8]*m[ 5]; t[ 9]=m[ 0]*m[13]-m[12]*m[ 1];
    t[10]=m[ 0]*m[ 9]-m[ 8]*m[ 1]; t[11]=m[ 0]*m[ 5]-m[ 4]*m[ 1];

    inv[ 0]= m[ 5]*t[0]-m[ 9]*t[1]+m[13]*t[2];
    inv[ 1]=-(m[ 1]*t[0]-m[ 9]*t[3]+m[13]*t[4]);
    inv[ 2]= m[ 1]*t[1]-m[ 5]*t[3]+m[13]*t[5];
    inv[ 3]=-(m[ 1]*t[2]-m[ 5]*t[4]+m[ 9]*t[5]);
    inv[ 4]=-(m[ 4]*t[0]-m[ 8]*t[1]+m[12]*t[2]);
    inv[ 5]= m[ 0]*t[0]-m[ 8]*t[3]+m[12]*t[4];
    inv[ 6]=-(m[ 0]*t[1]-m[ 4]*t[3]+m[12]*t[5]);
    inv[ 7]= m[ 0]*t[2]-m[ 4]*t[4]+m[ 8]*t[5];
    inv[ 8]= m[ 7]*t[6]-m[11]*t[7]+m[15]*t[8];
    inv[ 9]=-(m[ 3]*t[6]-m[11]*t[9]+m[15]*t[10]);
    inv[10]= m[ 3]*t[7]-m[ 7]*t[9]+m[15]*t[11];
    inv[11]=-(m[ 3]*t[8]-m[ 7]*t[10]+m[11]*t[11]);

    inv[12]=-(m[ 6]*t[6]-m[10]*t[7]+m[14]*t[8]);
    inv[13]= m[ 2]*t[6]-m[10]*t[9]+m[14]*t[10];
    inv[14]=-(m[ 2]*t[7]-m[ 6]*t[9]+m[14]*t[11]);
    inv[15]= m[ 2]*t[8]-m[ 6]*t[10]+m[10]*t[11];

    float det = m[0]*inv[0]+m[4]*inv[1]+m[8]*inv[2]+m[12]*inv[3];
    if (gizmo_fabsf(det) < 1e-12f) return false;
    float idet = 1.0f / det;
    for (int i = 0; i < 16; i++) inv[i] *= idet;
    return true;
}

static void m4_mul_v4(const float m[16], const float v[4], float out[4])
{
    out[0]=m[0]*v[0]+m[4]*v[1]+m[ 8]*v[2]+m[12]*v[3];
    out[1]=m[1]*v[0]+m[5]*v[1]+m[ 9]*v[2]+m[13]*v[3];
    out[2]=m[2]*v[0]+m[6]*v[1]+m[10]*v[2]+m[14]*v[3];
    out[3]=m[3]*v[0]+m[7]*v[1]+m[11]*v[2]+m[15]*v[3];
}

/* ================================================================
   Build view + projection from Qs_Camera
   ================================================================ */

static void compute_view_proj(const Qs_Camera *cam, uint32_t w, uint32_t h,
                              float view[16], float proj[16])
{
    m4_look_at(view, cam->position, cam->target, cam->up);
    float aspect = (h > 0) ? (float)w / (float)h : 1.0f;
    float fov = cam->fov_deg > 0.0f ? cam->fov_deg : 60.0f;
    float near_p = cam->near_plane != 0.0f ? cam->near_plane : 0.1f;
    float far_p  = cam->far_plane  != 0.0f ? cam->far_plane  : 1000.0f;
    m4_perspective(proj, gizmo_to_rad(fov), aspect, near_p, far_p);
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
    m4_mul(proj, view, vp);
    if (!m4_inverse(vp, ivp)) {
        v3_copy(cam_pos, ray_o);
        v3_set(ray_d, 0, 0, -1);
        return;
    }

    float near4[4] = { ndc_x, ndc_y, 0.0f, 1.0f };
    float far4[4]  = { ndc_x, ndc_y, 1.0f, 1.0f };
    float wn[4], wf[4];
    m4_mul_v4(ivp, near4, wn);
    m4_mul_v4(ivp, far4,  wf);

    if (gizmo_fabsf(wn[3]) > 1e-7f) { wn[0]/=wn[3]; wn[1]/=wn[3]; wn[2]/=wn[3]; }
    if (gizmo_fabsf(wf[3]) > 1e-7f) { wf[0]/=wf[3]; wf[1]/=wf[3]; wf[2]/=wf[3]; }

    v3_copy(cam_pos, ray_o);
    float d[3]; v3_sub(wf, wn, d); v3_norm(d, ray_d);
}

/* ================================================================
   Ray-sphere intersection
   ================================================================ */

static bool ray_sphere(const float o[3], const float d[3],
                       const float center[3], float radius,
                       float *out_t)
{
    float oc[3]; v3_sub(o, center, oc);
    float b = v3_dot(oc, d);
    float c = v3_dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    if (disc < 0.0f) return false;
    float sq = gizmo_sqrtf(disc);
    float t0 = -b - sq;
    float t1 = -b + sq;
    *out_t = t0 >= 0.0f ? t0 : t1;
    return *out_t >= 0.0f;
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
    if (gizmo_fabsf(denom) > 1e-7f) {
        t2 = (a * e - b * d) / denom;
    }

    float p1[3], p2[3], diff[3];
    float t1 = (gizmo_fabsf(denom) > 1e-7f) ? (b * e - c * d) / denom : 0.0f;
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

#define GIZMO_MAX_VERTS 1024

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

/* Drag state */
static int   s_drag_axis  = -1;   /* -1 = none, 0=X, 1=Y, 2=Z */
static float s_drag_origin_t;     /* axis parameter at drag start */
static float s_drag_start_val;    /* original transform value along axis */

/* Cached view/proj for render callback */
static float s_cached_vp[16];

/* ================================================================
   Axis definitions
   ================================================================ */

static const float AXIS_DIR[3][3] = {
    { 1, 0, 0 },   /* X — red   */
    { 0, 1, 0 },   /* Y — green */
    { 0, 0, 1 },   /* Z — blue  */
};

static const float AXIS_COL[3][4] = {
    { 1.0f, 0.32f, 0.44f, 1.0f },   /* X — red   (#ff5370) */
    { 0.50f, 1.0f, 0.50f, 1.0f },   /* Y — green (#80ff80) */
    { 0.36f, 0.61f, 1.0f, 1.0f },   /* Z — blue  (#5b9cff) */
};

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
   Geometry builders
   ================================================================ */

static void push_line(const float a[3], const float b[3], const float col[4])
{
    if (s_vert_count + 2 > GIZMO_MAX_VERTS) return;
    GizmoVert *va = &s_verts[s_vert_count++];
    GizmoVert *vb = &s_verts[s_vert_count++];
    v3_copy(a, va->pos);  memcpy(va->col, col, 16);
    v3_copy(b, vb->pos);  memcpy(vb->col, col, 16);
}

static float gizmo_scale(const float cam_pos[3], const float entity_pos[3])
{
    float d[3]; v3_sub(entity_pos, cam_pos, d);
    return gizmo_maxf(v3_len(d) * 0.12f, 0.1f);
}

static void build_translate(const float pos[3], float size)
{
    for (int i = 0; i < 3; i++) {
        float tip[3];
        v3_scale(AXIS_DIR[i], size, tip);
        v3_add(pos, tip, tip);

        /* Shaft */
        push_line(pos, tip, AXIS_COL[i]);

        /* Arrowhead — two lines diverging from tip */
        float perp1[3], perp2[3];
        int a1 = (i + 1) % 3, a2 = (i + 2) % 3;
        float head = size * 0.15f;

        float h1[3];
        v3_scale(AXIS_DIR[i], -head, h1);
        v3_add(tip, h1, h1);                /* step back from tip */

        float p1[3], p2[3], p3[3], p4[3];
        v3_scale(AXIS_DIR[a1],  head, p1);
        v3_scale(AXIS_DIR[a1], -head, p2);
        v3_scale(AXIS_DIR[a2],  head, p3);
        v3_scale(AXIS_DIR[a2], -head, p4);

        float ah1[3], ah2[3], ah3[3], ah4[3];
        v3_add(h1, p1, ah1); v3_add(h1, p2, ah2);
        v3_add(h1, p3, ah3); v3_add(h1, p4, ah4);

        push_line(tip, ah1, AXIS_COL[i]);
        push_line(tip, ah2, AXIS_COL[i]);
        push_line(tip, ah3, AXIS_COL[i]);
        push_line(tip, ah4, AXIS_COL[i]);
    }
}

static void build_rotate(const float pos[3], float size)
{
    const int SEGMENTS = 48;
    for (int axis = 0; axis < 3; axis++) {
        int a1 = (axis + 1) % 3;
        int a2 = (axis + 2) % 3;

        for (int s = 0; s < SEGMENTS; s++) {
            float t0 = (float)s       / (float)SEGMENTS * 2.0f * QS_PI;
            float t1 = (float)(s + 1) / (float)SEGMENTS * 2.0f * QS_PI;

            float p0[3], p1[3];
            v3_copy(pos, p0); v3_copy(pos, p1);
            p0[a1] += cosf(t0) * size;  p0[a2] += sinf(t0) * size;
            p1[a1] += cosf(t1) * size;  p1[a2] += sinf(t1) * size;

            push_line(p0, p1, AXIS_COL[axis]);
        }
    }
}

static void build_scale(const float pos[3], float size)
{
    float cube_s = size * 0.08f;

    for (int i = 0; i < 3; i++) {
        float tip[3];
        v3_scale(AXIS_DIR[i], size, tip);
        v3_add(pos, tip, tip);

        /* Shaft */
        push_line(pos, tip, AXIS_COL[i]);

        /* Small cube at tip: 12 edges of an axis-aligned box */
        float lo[3], hi[3];
        for (int j = 0; j < 3; j++) {
            lo[j] = tip[j] - cube_s;
            hi[j] = tip[j] + cube_s;
        }

        float c[8][3] = {
            {lo[0],lo[1],lo[2]},{hi[0],lo[1],lo[2]},
            {hi[0],hi[1],lo[2]},{lo[0],hi[1],lo[2]},
            {lo[0],lo[1],hi[2]},{hi[0],lo[1],hi[2]},
            {hi[0],hi[1],hi[2]},{lo[0],hi[1],hi[2]},
        };
        int edges[12][2] = {
            {0,1},{1,2},{2,3},{3,0},
            {4,5},{5,6},{6,7},{7,4},
            {0,4},{1,5},{2,6},{3,7},
        };
        for (int e = 0; e < 12; e++)
            push_line(c[edges[e][0]], c[edges[e][1]], AXIS_COL[i]);
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

    /* Upload vertices to GPU */
    Qs_GpuContext *gpu = qs_engine_gpu(s_engine);
    void *mapped = qs_gpu_map_buffer(gpu, s_vbuf);
    if (!mapped) return;
    memcpy(mapped, s_verts, s_vert_count * sizeof(GizmoVert));
    qs_gpu_unmap_buffer(gpu, s_vbuf);

    /* Begin rendering on top of swapchain (LOAD existing contents) */
    qs_cmd_begin_rendering(ctx->cmd, &(Qs_GpuRenderTarget){
        .color      = ctx->swapchain_view,
        .depth      = NULL,
        .load_color = true,
        .width      = ctx->swapchain_width,
        .height     = ctx->swapchain_height,
    });

    qs_cmd_set_viewport(ctx->cmd, ctx->swapchain_width, ctx->swapchain_height);
    qs_cmd_bind_pipeline(ctx->cmd, s_pipeline);

    /* Push MVP = proj * view */
    float mvp[16];
    m4_mul(ctx->proj, ctx->view, mvp);
    qs_cmd_push_constants(ctx->cmd, s_layout,
                          QS_GPU_SHADER_VERTEX | QS_GPU_SHADER_FRAGMENT,
                          0, 64, mvp);

    qs_cmd_bind_vertex_buffer(ctx->cmd, 0, s_vbuf, 0);
    qs_cmd_draw(ctx->cmd, s_vert_count, 0);

    qs_cmd_end_rendering(ctx->cmd);
}

/* ================================================================
   Entity picking — ray cast against bounding spheres
   ================================================================ */

static Qs_Entity pick_entity(const float ray_o[3], const float ray_d[3])
{
    Qs_Scene *scene = qs_scene_active();
    if (!scene) return QS_ENTITY_INVALID;

    float best_t = 1e30f;
    Qs_Entity best = QS_ENTITY_INVALID;

    for (Qs_Entity e = qs_scene_first(scene, qs_transform_type());
         e != QS_ENTITY_INVALID;
         e = qs_scene_next(scene, qs_transform_type(), e))
    {
        Qs_Transform *tr = qs_entity_get(scene, e, qs_transform_type());
        if (!tr) continue;

        /* Use a bounding sphere centred at position with radius from scale */
        float radius = gizmo_maxf(tr->scale[0],
                       gizmo_maxf(tr->scale[1], tr->scale[2])) * 0.5f;
        if (radius < 0.1f) radius = 0.5f;

        float t;
        if (ray_sphere(ray_o, ray_d, tr->position, radius, &t) && t < best_t) {
            best_t = t;
            best   = e;
        }
    }
    return best;
}

/* ================================================================
   Gizmo axis picking — find closest axis to mouse ray
   ================================================================ */

static int pick_gizmo_axis(const float entity_pos[3], float giz_size,
                           const float ray_o[3], const float ray_d[3])
{
    float threshold = giz_size * 0.12f;
    int best = -1;
    float best_dist = threshold;

    for (int i = 0; i < 3; i++) {
        float dist;
        float t2 = ray_ray_closest(ray_o, ray_d, entity_pos, AXIS_DIR[i], &dist);
        if (t2 > 0.0f && t2 < giz_size && dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}

/* ================================================================
   Gizmo drag interaction
   ================================================================ */

/* Project ray onto axis through entity position; return parameter along axis. */
static float project_ray_on_axis(const float ray_o[3], const float ray_d[3],
                                 const float entity_pos[3], int axis)
{
    float dist;
    return ray_ray_closest(ray_o, ray_d, entity_pos, AXIS_DIR[axis], &dist);
}

static void apply_translate_drag(Qs_Transform *tr, int axis,
                                 float current_t, float origin_t,
                                 float start_val)
{
    float delta = current_t - origin_t;
    tr->position[axis] = start_val + delta;
}

static void apply_rotate_drag(Qs_Transform *tr, int axis,
                              float current_t, float origin_t)
{
    float delta = (current_t - origin_t) * 2.0f;

    /* Build rotation quaternion around the selected axis */
    float half = delta * 0.5f;
    float s = sinf(half), c = cosf(half);
    float dq[4] = { 0, 0, 0, c };
    dq[axis] = s;

    /* Multiply: tr->rotation = dq * tr->rotation */
    float qx = tr->rotation[0], qy = tr->rotation[1];
    float qz = tr->rotation[2], qw = tr->rotation[3];
    tr->rotation[0] = dq[3]*qx + dq[0]*qw + dq[1]*qz - dq[2]*qy;
    tr->rotation[1] = dq[3]*qy - dq[0]*qz + dq[1]*qw + dq[2]*qx;
    tr->rotation[2] = dq[3]*qz + dq[0]*qy - dq[1]*qx + dq[2]*qw;
    tr->rotation[3] = dq[3]*qw - dq[0]*qx - dq[1]*qy - dq[2]*qz;

    /* Re-normalize */
    float l = gizmo_sqrtf(tr->rotation[0]*tr->rotation[0] +
                          tr->rotation[1]*tr->rotation[1] +
                          tr->rotation[2]*tr->rotation[2] +
                          tr->rotation[3]*tr->rotation[3]);
    if (l > 1e-7f) {
        float il = 1.0f / l;
        tr->rotation[0] *= il; tr->rotation[1] *= il;
        tr->rotation[2] *= il; tr->rotation[3] *= il;
    }
}

static void apply_scale_drag(Qs_Transform *tr, int axis,
                             float current_t, float origin_t,
                             float start_val)
{
    float delta = current_t - origin_t;
    float new_scale = start_val + delta;
    if (new_scale < 0.01f) new_scale = 0.01f;
    tr->scale[axis] = new_scale;
}

/* ================================================================
   Public API
   ================================================================ */

EdGizmoMode ed_gizmo_mode(void)     { return s_mode; }
void ed_gizmo_set_mode(EdGizmoMode m) { s_mode = m;  }

void ed_gizmo_init(Qs_Engine *engine)
{
    s_engine = engine;
    Qs_GpuContext *gpu = qs_engine_gpu(engine);

    /* Compile shaders */
    Qs_GpuShader *vs = qs_gpu_compile_shader(gpu, GIZMO_VERT_SRC, QS_GPU_SHADER_VERTEX);
    Qs_GpuShader *fs = qs_gpu_compile_shader(gpu, GIZMO_FRAG_SRC, QS_GPU_SHADER_FRAGMENT);
    if (!vs || !fs) {
        QS_LOG_ERROR("Gizmo: shader compilation failed");
        if (vs) qs_gpu_destroy_shader(gpu, vs);
        if (fs) qs_gpu_destroy_shader(gpu, fs);
        return;
    }

    /* Pipeline layout — push constant: mat4 MVP (64 bytes) */
    Qs_GpuPushConstantRange pc = {
        .stages = QS_GPU_SHADER_VERTEX | QS_GPU_SHADER_FRAGMENT,
        .offset = 0,
        .size   = 64,
    };
    s_layout = qs_gpu_create_pipeline_layout(gpu, &(Qs_GpuPipelineLayoutDesc){
        .push_constants      = &pc,
        .push_constant_count = 1,
    });

    /* Vertex binding: position(float3) + color(float4) */
    Qs_GpuVertexAttribute attrs[2] = {
        { .location = 0, .format = QS_GPU_VERTEX_FORMAT_FLOAT3,
          .offset = offsetof(GizmoVert, pos) },
        { .location = 1, .format = QS_GPU_VERTEX_FORMAT_FLOAT4,
          .offset = offsetof(GizmoVert, col) },
    };
    Qs_GpuVertexBinding vb = {
        .binding         = 0,
        .stride          = sizeof(GizmoVert),
        .attributes      = attrs,
        .attribute_count = 2,
    };

    s_pipeline = qs_gpu_create_graphics_pipeline(gpu, &(Qs_GpuGraphicsPipelineDesc){
        .layout              = s_layout,
        .vertex_shader       = vs,
        .fragment_shader     = fs,
        .vertex_bindings     = &vb,
        .vertex_binding_count = 1,
        .topology            = QS_GPU_TOPOLOGY_LINES,
        .cull_mode           = QS_GPU_CULL_NONE,
        .depth_test          = false,
        .depth_write         = false,
        .color_format        = QS_GPU_FORMAT_BGRA8_UNORM,
        .depth_format        = QS_GPU_FORMAT_DEPTH_AUTO,
    });

    qs_gpu_destroy_shader(gpu, vs);
    qs_gpu_destroy_shader(gpu, fs);

    if (!s_pipeline) {
        QS_LOG_ERROR("Gizmo: pipeline creation failed");
        return;
    }

    /* Create host-visible vertex buffer */
    s_vbuf = qs_gpu_create_buffer(gpu, &(Qs_GpuBufferDesc){
        .size   = GIZMO_MAX_VERTS * sizeof(GizmoVert),
        .usage  = QS_GPU_BUFFER_VERTEX,
        .memory = QS_GPU_MEMORY_HOST_VISIBLE,
    });

    s_vert_count = 0;
    s_drag_axis  = -1;
}

void ed_gizmo_shutdown(Qs_Engine *engine)
{
    Qs_GpuContext *gpu = qs_engine_gpu(engine);
    if (s_vbuf)     { qs_gpu_destroy_buffer(gpu, s_vbuf);           s_vbuf     = NULL; }
    if (s_pipeline) { qs_gpu_destroy_pipeline(gpu, s_pipeline);     s_pipeline = NULL; }
    if (s_layout)   { qs_gpu_destroy_pipeline_layout(gpu, s_layout);s_layout   = NULL; }
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

    /* Viewport rect in window coordinates */
    float vp_x, vp_y, vp_w, vp_h;
    ca_viewport_screen_rect(vp, &vp_x, &vp_y, &vp_w, &vp_h);
    if (vp_w <= 0 || vp_h <= 0) { s_vert_count = 0; return; }

    Qs_Camera *cam = qs_renderer_camera(renderer);
    if (!cam) { s_vert_count = 0; return; }

    /* Compute view/proj for ray casting */
    float view[16], proj[16];
    compute_view_proj(cam, (uint32_t)vp_w, (uint32_t)vp_h, view, proj);

    /* Mouse → NDC */
    float mx, my;
    qs_input_mouse_pos(&mx, &my);
    bool in_viewport = (mx >= vp_x && mx < vp_x + vp_w &&
                        my >= vp_y && my < vp_y + vp_h);

    float ndc_x = 2.0f * (mx - vp_x) / vp_w - 1.0f;
    float ndc_y = 2.0f * (my - vp_y) / vp_h - 1.0f;

    float ray_o[3], ray_d[3];
    screen_to_ray(view, proj, ndc_x, ndc_y, cam->position, ray_o, ray_d);

    /* Camera controls active? */
    bool alt_held = qs_input_key_down(QS_KEY_LEFT_ALT) ||
                    qs_input_key_down(QS_KEY_RIGHT_ALT);
    bool rmb_down = qs_input_mouse_down(QS_MOUSE_RIGHT);
    bool mmb_down = qs_input_mouse_down(QS_MOUSE_MIDDLE);
    bool cam_active = alt_held || rmb_down || mmb_down;

    /* ---- Input handling (only when cursor is in viewport) ---- */
    if (in_viewport && !cam_active) {
        Qs_Entity sel = editor_selected_entity(ed);
        Qs_Scene *scene = qs_scene_active();

        if (qs_input_mouse_pressed(QS_MOUSE_LEFT)) {
            /* Try gizmo handle first */
            if (sel != QS_ENTITY_INVALID && scene) {
                Qs_Transform *tr = qs_entity_get(scene, sel, qs_transform_type());
                if (tr) {
                    float gsize = gizmo_scale(cam->position, tr->position);
                    int axis = pick_gizmo_axis(tr->position, gsize, ray_o, ray_d);
                    if (axis >= 0) {
                        s_drag_axis = axis;
                        s_drag_origin_t = project_ray_on_axis(ray_o, ray_d,
                                                              tr->position, axis);
                        if (s_mode == ED_GIZMO_TRANSLATE)
                            s_drag_start_val = tr->position[axis];
                        else if (s_mode == ED_GIZMO_SCALE)
                            s_drag_start_val = tr->scale[axis];
                        else
                            s_drag_start_val = 0.0f;
                        goto build;
                    }
                }
            }

            /* No gizmo hit — pick entity */
            Qs_Entity hit = pick_entity(ray_o, ray_d);
            editor_set_selected_entity(ed, hit);
            s_drag_axis = -1;
        }

        /* Drag in progress */
        if (qs_input_mouse_down(QS_MOUSE_LEFT) && s_drag_axis >= 0 &&
            sel != QS_ENTITY_INVALID && scene)
        {
            Qs_Transform *tr = qs_entity_get(scene, sel, qs_transform_type());
            if (tr) {
                float current_t = project_ray_on_axis(ray_o, ray_d,
                                                      tr->position, s_drag_axis);
                switch (s_mode) {
                case ED_GIZMO_TRANSLATE:
                    apply_translate_drag(tr, s_drag_axis, current_t,
                                         s_drag_origin_t, s_drag_start_val);
                    break;
                case ED_GIZMO_ROTATE:
                    apply_rotate_drag(tr, s_drag_axis, current_t, s_drag_origin_t);
                    s_drag_origin_t = current_t;
                    break;
                case ED_GIZMO_SCALE:
                    apply_scale_drag(tr, s_drag_axis, current_t,
                                     s_drag_origin_t, s_drag_start_val);
                    break;
                }
            }
        }

        if (qs_input_mouse_released(QS_MOUSE_LEFT))
            s_drag_axis = -1;

    } else if (cam_active) {
        s_drag_axis = -1;
    }

    /* ---- Shortcut keys: W = translate, E = rotate, R = scale ---- */
    if (qs_input_key_pressed(QS_KEY_W)) s_mode = ED_GIZMO_TRANSLATE;
    if (qs_input_key_pressed(QS_KEY_E)) s_mode = ED_GIZMO_ROTATE;
    if (qs_input_key_pressed(QS_KEY_R)) s_mode = ED_GIZMO_SCALE;

build:
    /* ---- Build gizmo geometry ---- */
    s_vert_count = 0;
    {
        Qs_Entity sel = editor_selected_entity(ed);
        Qs_Scene *scene = qs_scene_active();
        if (sel != QS_ENTITY_INVALID && scene) {
            Qs_Transform *tr = qs_entity_get(scene, sel, qs_transform_type());
            if (tr) {
                float size = gizmo_scale(cam->position, tr->position);
                switch (s_mode) {
                case ED_GIZMO_TRANSLATE: build_translate(tr->position, size); break;
                case ED_GIZMO_ROTATE:    build_rotate(tr->position, size);    break;
                case ED_GIZMO_SCALE:     build_scale(tr->position, size);     break;
                }
            }
        }
    }
}
