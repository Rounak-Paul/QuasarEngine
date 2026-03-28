/*
 * vk_forward.c  --  Forward+ renderer with:
 *   Pass 0 (priority   0): CSM shadow depth pass  (QS_CSM_CASCADES cascades)
 *   Pass 1 (priority 100): Forward lit pass       (PBR GGX + CSM)
 *   Pass 2 (priority 200): Bloom                  (downsample + upsample)
 *   Pass 3 (priority 300): Composite              (ACES tonemap + vignette -> swapchain)
 *
 * Descriptor layout:
 *   set=0  binding 0  UNIFORM_BUFFER   FrameUBO
 *   set=0  binding 1  UNIFORM_BUFFER   LightUBO
 *   set=0  binding 2  UNIFORM_BUFFER   ShadowUBO
 *   set=0  binding 3-5 COMBINED_IMAGE_SAMPLER shadow maps [3]
 *   set=1  material textures (5 COMBINED_IMAGE_SAMPLER via qs_material_set_layout)
 *
 * Push constants:
 *   Shadow pass  : mat4 model (64 bytes) + int cascade_idx (4 bytes) = 68 bytes
 *   Forward pass : mat4 model (64 bytes)
 *   Bloom/composite: vec2 inv_size + 2 floats (16 bytes)
 */

#include "qs_renderer.h"
#include "qs_gpu.h"
#include "qs_material.h"
#include "qs_mesh.h"
#include "qs_light.h"
#include "qs_log.h"
#include "vk_renderer_internal.h"

#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ================================================================
   GPU DATA STRUCTURES  (must match shader std140)
   ================================================================ */

#define QS_MAX_LIGHTS_GPU 128

typedef struct {
    float view[16];
    float proj[16];
    float inv_view_proj[16];
    float cam_pos[3];
    float time;
    float screen_width;
    float screen_height;
    float _pad[2];
} FrameUBO;  /* 208 bytes */

typedef struct {
    float    position[3];
    float    range;
    float    direction[3];
    float    intensity;
    float    color[3];
    float    inner_cone_cos;
    float    outer_cone_cos;
    uint32_t type;
    uint32_t cast_shadows;
    uint32_t _pad;
} LightGPUEntry;  /* 64 bytes - matches Qs_LightGPU */

typedef struct {
    LightGPUEntry lights[QS_MAX_LIGHTS_GPU];
    uint32_t      count;
    uint32_t      _pad[3];
} LightUBO;

typedef struct {
    float cascade_vp[QS_CSM_CASCADES][16];
    float cascade_splits[QS_CSM_CASCADES];
    float _pad;
} ShadowUBO;

/* ================================================================
   GLSL -- shadow depth vertex / fragment shaders
   ================================================================ */

static const char *SHADOW_VERT =
    "#version 450\n"
    "layout(location = 0) in vec3 a_position;\n"
    "layout(push_constant) uniform PC { mat4 model; int cascade_idx; int _p[3]; } pc;\n"
    "layout(set = 0, binding = 2) uniform ShadowUBO {\n"
    "    mat4  cascade_vp[3];\n"
    "    float cascade_splits[3];\n"
    "    float _pad;\n"
    "} shadow;\n"
    "void main() {\n"
    "    gl_Position = shadow.cascade_vp[pc.cascade_idx] * pc.model * vec4(a_position, 1.0);\n"
    "}\n";

static const char *SHADOW_FRAG =
    "#version 450\n"
    "void main() {}\n";

/* ================================================================
   GLSL -- forward lit vertex shader
   ================================================================ */

static const char *FORWARD_VERT =
    "#version 450\n"
    "layout(location = 0) in vec3 a_position;\n"
    "layout(location = 1) in vec3 a_normal;\n"
    "layout(location = 2) in vec4 a_tangent;\n"
    "layout(location = 3) in vec2 a_uv;\n"
    "layout(push_constant) uniform PC { mat4 model; } pc;\n"
    "layout(set = 0, binding = 0) uniform FrameUBO {\n"
    "    mat4  view; mat4  proj; mat4  inv_view_proj;\n"
    "    vec3  cam_pos; float time;\n"
    "    float screen_width; float screen_height; vec2 _pad;\n"
    "} frame;\n"
    "layout(location = 0) out vec3 v_world_pos;\n"
    "layout(location = 1) out vec3 v_normal;\n"
    "layout(location = 2) out vec3 v_tangent;\n"
    "layout(location = 3) out vec3 v_bitangent;\n"
    "layout(location = 4) out vec2 v_uv;\n"
    "void main() {\n"
    "    vec4 world = pc.model * vec4(a_position, 1.0);\n"
    "    v_world_pos = world.xyz;\n"
    "    mat3 N = transpose(inverse(mat3(pc.model)));\n"
    "    v_normal    = normalize(N * a_normal);\n"
    "    v_tangent   = normalize(N * a_tangent.xyz);\n"
    "    v_bitangent = cross(v_normal, v_tangent) * a_tangent.w;\n"
    "    v_uv = a_uv;\n"
    "    gl_Position = frame.proj * frame.view * world;\n"
    "}\n";

/* ================================================================
   GLSL -- forward lit fragment shader (PBR GGX + CSM)
   ================================================================ */

static const char *FORWARD_FRAG =
    "#version 450\n"
    "layout(location = 0) in vec3 v_world_pos;\n"
    "layout(location = 1) in vec3 v_normal;\n"
    "layout(location = 2) in vec3 v_tangent;\n"
    "layout(location = 3) in vec3 v_bitangent;\n"
    "layout(location = 4) in vec2 v_uv;\n"
    "layout(set = 0, binding = 0) uniform FrameUBO {\n"
    "    mat4  view; mat4  proj; mat4  inv_view_proj;\n"
    "    vec3  cam_pos; float time;\n"
    "    float screen_width; float screen_height; vec2 _pad;\n"
    "} frame;\n"
    "struct LightEntry {\n"
    "    vec3  position;  float range;\n"
    "    vec3  direction; float intensity;\n"
    "    vec3  color;     float inner_cone_cos;\n"
    "    float outer_cone_cos; uint type; uint cast_shadows; uint _pad;\n"
    "};\n"
    "layout(set = 0, binding = 1) uniform LightUBO {\n"
    "    LightEntry lights[128]; uint count; uint _pad[3];\n"
    "} light_data;\n"
    "layout(set = 0, binding = 2) uniform ShadowUBO {\n"
    "    mat4  cascade_vp[3];\n"
    "    float cascade_splits[3];\n"
    "    float _pad;\n"
    "} shadow_data;\n"
    "layout(set = 0, binding = 3) uniform sampler2D shadow_map_0;\n"
    "layout(set = 0, binding = 4) uniform sampler2D shadow_map_1;\n"
    "layout(set = 0, binding = 5) uniform sampler2D shadow_map_2;\n"
    "layout(set = 1, binding = 0) uniform sampler2D u_base_color;\n"
    "layout(set = 1, binding = 1) uniform sampler2D u_metallic_roughness;\n"
    "layout(set = 1, binding = 2) uniform sampler2D u_normal_map;\n"
    "layout(set = 1, binding = 3) uniform sampler2D u_occlusion;\n"
    "layout(set = 1, binding = 4) uniform sampler2D u_emissive;\n"
    "layout(location = 0) out vec4 out_color;\n"
    "const float PI = 3.14159265359;\n"
    "float D_GGX(float NdotH, float r) { float a2=(r*r)*(r*r); float d=NdotH*NdotH*(a2-1.0)+1.0; return a2/(PI*d*d); }\n"
    "float G_Smith(float NdotV, float NdotL, float r) {\n"
    "    float k=(r+1.0)*(r+1.0)/8.0;\n"
    "    return (NdotV/(NdotV*(1.0-k)+k))*(NdotL/(NdotL*(1.0-k)+k)); }\n"
    "vec3 F_Schlick(float c, vec3 F0) { return F0+(1.0-F0)*pow(clamp(1.0-c,0.0,1.0),5.0); }\n"
    "float shadow_pcf(sampler2D sm, vec4 lsp) {\n"
    "    vec3 p=lsp.xyz/lsp.w; p=p*0.5+0.5;\n"
    "    if(p.z>1.0||any(lessThan(p.xy,vec2(0)))||any(greaterThan(p.xy,vec2(1)))) return 1.0;\n"
    "    float s=0.0; vec2 t=1.0/vec2(textureSize(sm,0)); float d=p.z-0.002;\n"
    "    for(int x=-1;x<=1;x++) for(int y=-1;y<=1;y++) s+=(texture(sm,p.xy+vec2(x,y)*t).r<d)?0.0:1.0;\n"
    "    return s/9.0; }\n"
    "float compute_shadow(vec3 wpos) {\n"
    "    float depth=-( frame.view*vec4(wpos,1.0)).z;\n"
    "    if(depth<shadow_data.cascade_splits[0]) return shadow_pcf(shadow_map_0,shadow_data.cascade_vp[0]*vec4(wpos,1.0));\n"
    "    else if(depth<shadow_data.cascade_splits[1]) return shadow_pcf(shadow_map_1,shadow_data.cascade_vp[1]*vec4(wpos,1.0));\n"
    "    else return shadow_pcf(shadow_map_2,shadow_data.cascade_vp[2]*vec4(wpos,1.0)); }\n"
    "void main() {\n"
    "    vec4 base=texture(u_base_color,v_uv);\n"
    "    vec2 mr=texture(u_metallic_roughness,v_uv).bg;\n"
    "    float metallic=mr.x; float roughness=max(mr.y,0.04);\n"
    "    float ao=texture(u_occlusion,v_uv).r;\n"
    "    vec3 emissive=texture(u_emissive,v_uv).rgb;\n"
    "    vec3 nmap=texture(u_normal_map,v_uv).rgb*2.0-1.0;\n"
    "    mat3 TBN=mat3(normalize(v_tangent),normalize(v_bitangent),normalize(v_normal));\n"
    "    vec3 N=normalize(TBN*nmap);\n"
    "    vec3 V=normalize(frame.cam_pos-v_world_pos);\n"
    "    vec3 F0=mix(vec3(0.04),base.rgb,metallic);\n"
    "    vec3 Lo=vec3(0);\n"
    "    for(uint i=0u;i<light_data.count;i++) {\n"
    "        LightEntry l=light_data.lights[i];\n"
    "        vec3 L; float att=1.0;\n"
    "        if(l.type==0u) { L=normalize(-l.direction); }\n"
    "        else { vec3 dv=l.position-v_world_pos; float dist=length(dv); L=dv/dist;\n"
    "               att=1.0/(1.0+dist*dist);\n"
    "               if(l.range>0.0) att*=clamp(1.0-dist/l.range,0.0,1.0);\n"
    "               if(l.type==2u){ float ct=dot(-L,normalize(l.direction));\n"
    "                   float sa=clamp((ct-l.outer_cone_cos)/(l.inner_cone_cos-l.outer_cone_cos),0.0,1.0);\n"
    "                   att*=sa*sa; } }\n"
    "        float NdotL=max(dot(N,L),0.0); if(NdotL<=0.0) continue;\n"
    "        vec3 H=normalize(V+L);\n"
    "        float NdotV=max(dot(N,V),0.001),NdotH=max(dot(N,H),0.001),VdotH=max(dot(V,H),0.001);\n"
    "        float D=D_GGX(NdotH,roughness); float G=G_Smith(NdotV,NdotL,roughness);\n"
    "        vec3 F=F_Schlick(VdotH,F0);\n"
    "        vec3 spec=(D*G*F)/(4.0*NdotV*NdotL);\n"
    "        vec3 kd=(vec3(1.0)-F)*(1.0-metallic);\n"
    "        float shad=(l.cast_shadows!=0u&&l.type==0u)?compute_shadow(v_world_pos):1.0;\n"
    "        Lo+=(kd*base.rgb/PI+spec)*l.color*l.intensity*NdotL*att*shad;\n"
    "    }\n"
    "    vec3 ambient=0.03*base.rgb*ao;\n"
    "    out_color=vec4(ambient+Lo+emissive,base.a);\n"
    "}\n";

/* ================================================================
   GLSL -- bloom downsample (Kawase)
   ================================================================ */

static const char *BLOOM_DOWN_FRAG =
    "#version 450\n"
    "layout(set=0,binding=0) uniform sampler2D u_src;\n"
    "layout(push_constant) uniform PC { vec2 inv_src_size; vec2 _pad; } pc;\n"
    "layout(location=0) out vec4 out_color;\n"
    "void main() {\n"
    "    vec2 uv=gl_FragCoord.xy*pc.inv_src_size;\n"
    "    vec2 h=pc.inv_src_size*0.5;\n"
    "    vec3 s=texture(u_src,uv).rgb\n"
    "          +texture(u_src,uv+vec2( h.x, h.y)).rgb\n"
    "          +texture(u_src,uv+vec2(-h.x, h.y)).rgb\n"
    "          +texture(u_src,uv+vec2( h.x,-h.y)).rgb\n"
    "          +texture(u_src,uv+vec2(-h.x,-h.y)).rgb;\n"
    "    out_color=vec4(s/5.0,1.0);\n"
    "}\n";

/* ================================================================
   GLSL -- bloom upsample (tent filter)
   ================================================================ */

static const char *BLOOM_UP_FRAG =
    "#version 450\n"
    "layout(set=0,binding=0) uniform sampler2D u_src;\n"
    "layout(push_constant) uniform PC { vec2 inv_src_size; vec2 _pad; } pc;\n"
    "layout(location=0) out vec4 out_color;\n"
    "void main() {\n"
    "    vec2 uv=gl_FragCoord.xy*pc.inv_src_size;\n"
    "    vec2 d=pc.inv_src_size;\n"
    "    vec3 s=4.0*texture(u_src,uv).rgb\n"
    "          +2.0*(texture(u_src,uv+vec2(d.x,0)).rgb+texture(u_src,uv+vec2(-d.x,0)).rgb\n"
    "               +texture(u_src,uv+vec2(0,d.y)).rgb+texture(u_src,uv+vec2(0,-d.y)).rgb)\n"
    "          +texture(u_src,uv+vec2( d.x, d.y)).rgb+texture(u_src,uv+vec2(-d.x, d.y)).rgb\n"
    "          +texture(u_src,uv+vec2( d.x,-d.y)).rgb+texture(u_src,uv+vec2(-d.x,-d.y)).rgb;\n"
    "    out_color=vec4(s/16.0,1.0);\n"
    "}\n";

/* ================================================================
   GLSL -- composite (ACES tonemap + vignette)
   ================================================================ */

static const char *COMPOSITE_FRAG =
    "#version 450\n"
    "layout(set=0,binding=0) uniform sampler2D u_hdr;\n"
    "layout(set=0,binding=1) uniform sampler2D u_bloom;\n"
    "layout(push_constant) uniform PC { vec2 inv_size; float bloom_str; float vignette_str; } pc;\n"
    "layout(location=0) out vec4 out_color;\n"
    "vec3 aces(vec3 x) { return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14),0.0,1.0); }\n"
    "void main() {\n"
    "    vec2 uv=gl_FragCoord.xy*pc.inv_size;\n"
    "    vec3 color=texture(u_hdr,uv).rgb + texture(u_bloom,uv).rgb*pc.bloom_str;\n"
    "    color=aces(color);\n"
    "    color=pow(color,vec3(1.0/2.2));\n"
    "    vec2 vu=uv*(1.0-uv.yx);\n"
    "    color*=pow(vu.x*vu.y*15.0,pc.vignette_str);\n"
    "    out_color=vec4(color,1.0);\n"
    "}\n";

/* ================================================================
   GLSL -- fullscreen big-triangle vertex shader
   ================================================================ */

static const char *FULLSCREEN_VERT =
    "#version 450\n"
    "void main() {\n"
    "    vec2 pos=vec2((gl_VertexIndex==2)?3.0:-1.0,(gl_VertexIndex==1)?3.0:-1.0);\n"
    "    gl_Position=vec4(pos,0.0,1.0);\n"
    "}\n";

/* ================================================================
   FORWARD PASS STATE  (one per VkRenderer)
   ================================================================ */

typedef struct FwdRenderer {
    VkRenderer *r;
    Qs_Engine  *engine;

    /* Offscreen resources */
    Qs_GpuImage     *hdr_image;
    Qs_GpuImageView *hdr_view;
    Qs_GpuImage     *shadow_images[QS_CSM_CASCADES];
    Qs_GpuImageView *shadow_views[QS_CSM_CASCADES];
    Qs_GpuImageView *shadow_sample_views[QS_CSM_CASCADES];
    Qs_GpuImage     *bloom_images[2];
    Qs_GpuImageView *bloom_views[2];

    /* UBOs */
    Qs_GpuBuffer    *frame_ubo;
    Qs_GpuBuffer    *light_ubo;
    Qs_GpuBuffer    *shadow_ubo;

    /* Descriptor pool + sets */
    Qs_GpuDescriptorPool *desc_pool;
    Qs_GpuDescriptorSet  *frame_desc_set;
    Qs_GpuDescriptorSet  *composite_desc_set;
    Qs_GpuDescriptorSet  *bloom_desc_sets[2];

    /* Render nodes */
    Qs_RenderNode *shadow_node;
    Qs_RenderNode *forward_node;
    Qs_RenderNode *bloom_node;
    Qs_RenderNode *composite_node;

    Qs_Material   *default_material;

    uint32_t last_w, last_h;
    bool ok;
} FwdRenderer;

#define MAX_FWD_RENDERERS 32
static FwdRenderer g_fwd_pool[MAX_FWD_RENDERERS];

static FwdRenderer *fwd_find(VkRenderer *r)
{
    for (int i = 0; i < MAX_FWD_RENDERERS; i++)
        if (g_fwd_pool[i].r == r) return &g_fwd_pool[i];
    return NULL;
}

static FwdRenderer *fwd_alloc(VkRenderer *r)
{
    for (int i = 0; i < MAX_FWD_RENDERERS; i++) {
        if (!g_fwd_pool[i].r) {
            memset(&g_fwd_pool[i], 0, sizeof(FwdRenderer));
            g_fwd_pool[i].r = r;
            return &g_fwd_pool[i];
        }
    }
    return NULL;
}

/* ================================================================
   MATH HELPERS
   ================================================================ */

static void fwd_mat4_identity(float m[16])
{
    memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void fwd_mat4_mul(float out[16], const float a[16], const float b[16])
{
    float tmp[16];
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) s += a[r+k*4]*b[k+c*4];
            tmp[r+c*4] = s;
        }
    memcpy(out, tmp, 64);
}

static void fwd_mat4_ortho(float out[16],
    float l, float r, float b, float t, float n, float f)
{
    memset(out, 0, 64);
    out[0]  =  2.0f/(r-l); out[5]  =  2.0f/(t-b); out[10] = -2.0f/(f-n);
    out[12] = -(r+l)/(r-l); out[13] = -(t+b)/(t-b); out[14] = -(f+n)/(f-n);
    out[15] =  1.0f;
}

/* Compute CSM light-space VP matrices and split planes */
static void compute_csm(const Qs_Camera *cam,
                         float shadow_matrices[QS_CSM_CASCADES][16],
                         float shadow_splits[QS_CSM_CASCADES+1])
{
    /* Fixed sun direction -- will be driven by directional lights in future */
    float light_dir[3] = { 0.4f, -1.0f, 0.3f };
    float ld = sqrtf(light_dir[0]*light_dir[0]+light_dir[1]*light_dir[1]+light_dir[2]*light_dir[2]);
    if (ld > 1e-6f) { light_dir[0]/=ld; light_dir[1]/=ld; light_dir[2]/=ld; }

    float near_p = cam->near_plane > 0.0f ? cam->near_plane : 0.1f;
    float far_p  = cam->far_plane  > 0.0f ? cam->far_plane  : 500.0f;

    shadow_splits[0] = near_p;
    for (int i = 1; i <= QS_CSM_CASCADES; i++) {
        float fi  = (float)i / (float)QS_CSM_CASCADES;
        float log = near_p * powf(far_p/near_p, fi);
        float lin = near_p + (far_p-near_p)*fi;
        shadow_splits[i] = 0.75f*log + 0.25f*lin;
    }

    for (int c = 0; c < QS_CSM_CASCADES; c++) {
        float cn = shadow_splits[c];
        float cf = shadow_splits[c+1];
        float radius = (cf - cn) * 0.6f + 2.0f;

        /* Light eye position above scene centre */
        float cx = cam->target[0], cy = cam->target[1], cz = cam->target[2];
        float lx = cx - light_dir[0]*radius;
        float ly = cy - light_dir[1]*radius;
        float lz = cz - light_dir[2]*radius;

        /* Build light-space view matrix */
        float fd[3] = { -light_dir[0], -light_dir[1], -light_dir[2] };
        float up[3] = { 0, 1, 0 };
        float right[3];
        right[0] = fd[1]*up[2]-fd[2]*up[1];
        right[1] = fd[2]*up[0]-fd[0]*up[2];
        right[2] = fd[0]*up[1]-fd[1]*up[0];
        float rl = sqrtf(right[0]*right[0]+right[1]*right[1]+right[2]*right[2]);
        if (rl > 1e-6f) { right[0]/=rl; right[1]/=rl; right[2]/=rl; }
        float up2[3];
        up2[0]=fd[1]*right[2]-fd[2]*right[1];
        up2[1]=fd[2]*right[0]-fd[0]*right[2];
        up2[2]=fd[0]*right[1]-fd[1]*right[0];

        float lv[16]; memset(lv,0,64);
        lv[0]=right[0]; lv[4]=right[1]; lv[8]=right[2];  lv[12]=-(right[0]*lx+right[1]*ly+right[2]*lz);
        lv[1]=up2[0];   lv[5]=up2[1];   lv[9]=up2[2];    lv[13]=-(up2[0]*lx+up2[1]*ly+up2[2]*lz);
        lv[2]=fd[0];    lv[6]=fd[1];    lv[10]=fd[2];    lv[14]=-(fd[0]*lx+fd[1]*ly+fd[2]*lz);
        lv[3]=0; lv[7]=0; lv[11]=0; lv[15]=1.0f;

        float lp[16];
        float r2 = radius*1.2f;
        fwd_mat4_ortho(lp, -r2, r2, -r2, r2, -radius*5.0f, radius*5.0f);
        fwd_mat4_mul(shadow_matrices[c], lp, lv);
    }
}

/* ================================================================
   OFFSCREEN RESOURCE MANAGEMENT
   ================================================================ */

static void fwd_destroy_offscreen(FwdRenderer *f, Qs_GpuContext *gpu)
{
    for (int i = 0; i < 2; i++) {
        if (f->bloom_views[i])  { qs_gpu_destroy_image_view(gpu, f->bloom_views[i]);  f->bloom_views[i]  = NULL; }
        if (f->bloom_images[i]) { qs_gpu_destroy_image(gpu, f->bloom_images[i]);      f->bloom_images[i] = NULL; }
    }
    if (f->hdr_view)  { qs_gpu_destroy_image_view(gpu, f->hdr_view);  f->hdr_view  = NULL; }
    if (f->hdr_image) { qs_gpu_destroy_image(gpu, f->hdr_image);      f->hdr_image = NULL; }
    for (int i = 0; i < QS_CSM_CASCADES; i++) {
        if (f->shadow_sample_views[i]) { qs_gpu_destroy_image_view(gpu, f->shadow_sample_views[i]); f->shadow_sample_views[i]=NULL; }
        if (f->shadow_views[i])        { qs_gpu_destroy_image_view(gpu, f->shadow_views[i]);        f->shadow_views[i]=NULL; }
        if (f->shadow_images[i])       { qs_gpu_destroy_image(gpu, f->shadow_images[i]);            f->shadow_images[i]=NULL; }
    }
}

static bool fwd_create_offscreen(FwdRenderer *f, Qs_GpuContext *gpu,
                                  uint32_t w, uint32_t h)
{
    f->hdr_image = qs_gpu_create_image(gpu, &(Qs_GpuImageDesc){
        .width=w, .height=h, .mip_levels=1,
        .format=QS_GPU_FORMAT_RGBA16_SFLOAT,
        .usage=QS_GPU_IMAGE_COLOR_ATTACHMENT|QS_GPU_IMAGE_SAMPLED });
    if (!f->hdr_image) return false;
    f->hdr_view = qs_gpu_create_image_view_for(gpu, f->hdr_image,
                                                QS_GPU_IMAGE_ASPECT_COLOR);
    if (!f->hdr_view) return false;

    {
        Qs_GpuCmd *cmd = qs_gpu_begin_transfer(gpu);
        qs_cmd_image_barrier(cmd, &(Qs_GpuImageBarrier){
            .image=f->hdr_image,
            .old_layout=QS_GPU_IMAGE_LAYOUT_UNDEFINED,
            .new_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
            .aspect=QS_GPU_IMAGE_ASPECT_COLOR, .base_mip=0, .mip_count=1 });
        qs_gpu_end_transfer(gpu, cmd);
    }

    for (int i = 0; i < QS_CSM_CASCADES; i++) {
        f->shadow_images[i] = qs_gpu_create_image(gpu, &(Qs_GpuImageDesc){
            .width=QS_SHADOW_MAP_SIZE, .height=QS_SHADOW_MAP_SIZE, .mip_levels=1,
            .format=QS_GPU_FORMAT_D32_SFLOAT,
            .usage=QS_GPU_IMAGE_DEPTH_ATTACHMENT|QS_GPU_IMAGE_SAMPLED });
        if (!f->shadow_images[i]) return false;
        f->shadow_views[i] = qs_gpu_create_image_view_for(gpu, f->shadow_images[i],
                                                            QS_GPU_IMAGE_ASPECT_DEPTH);
        if (!f->shadow_views[i]) return false;
        f->shadow_sample_views[i] = qs_gpu_create_image_view_for(gpu, f->shadow_images[i],
                                                                   QS_GPU_IMAGE_ASPECT_DEPTH);
        if (!f->shadow_sample_views[i]) return false;
        {
            Qs_GpuCmd *cmd = qs_gpu_begin_transfer(gpu);
            qs_cmd_image_barrier(cmd, &(Qs_GpuImageBarrier){
                .image=f->shadow_images[i],
                .old_layout=QS_GPU_IMAGE_LAYOUT_UNDEFINED,
                .new_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
                .aspect=QS_GPU_IMAGE_ASPECT_DEPTH, .base_mip=0, .mip_count=1 });
            qs_gpu_end_transfer(gpu, cmd);
        }
    }

    uint32_t bw=(w+1)/2, bh=(h+1)/2;
    for (int i = 0; i < 2; i++) {
        f->bloom_images[i] = qs_gpu_create_image(gpu, &(Qs_GpuImageDesc){
            .width=bw, .height=bh, .mip_levels=1,
            .format=QS_GPU_FORMAT_RGBA16_SFLOAT,
            .usage=QS_GPU_IMAGE_COLOR_ATTACHMENT|QS_GPU_IMAGE_SAMPLED });
        if (!f->bloom_images[i]) return false;
        f->bloom_views[i] = qs_gpu_create_image_view_for(gpu, f->bloom_images[i],
                                                           QS_GPU_IMAGE_ASPECT_COLOR);
        if (!f->bloom_views[i]) return false;
        {
            Qs_GpuCmd *cmd = qs_gpu_begin_transfer(gpu);
            qs_cmd_image_barrier(cmd, &(Qs_GpuImageBarrier){
                .image=f->bloom_images[i],
                .old_layout=QS_GPU_IMAGE_LAYOUT_UNDEFINED,
                .new_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
                .aspect=QS_GPU_IMAGE_ASPECT_COLOR, .base_mip=0, .mip_count=1 });
            qs_gpu_end_transfer(gpu, cmd);
        }
    }

    f->last_w = w; f->last_h = h;
    return true;
}

/* ================================================================
   DESCRIPTOR UPDATES
   ================================================================ */

static void fwd_write_frame_descriptors(FwdRenderer *f, Qs_GpuContext *gpu,
                                         VkPassResources *ps)
{
    qs_gpu_write_buffer_descriptor(gpu, f->frame_desc_set, 0, f->frame_ubo,  0, 0);
    qs_gpu_write_buffer_descriptor(gpu, f->frame_desc_set, 1, f->light_ubo,  0, 0);
    qs_gpu_write_buffer_descriptor(gpu, f->frame_desc_set, 2, f->shadow_ubo, 0, 0);
    for (int i = 0; i < QS_CSM_CASCADES; i++)
        qs_gpu_write_image_descriptor(gpu, f->frame_desc_set, 3+(uint32_t)i,
                                       ps->shadow_sampler, f->shadow_sample_views[i]);
}

static void fwd_write_composite_descriptors(FwdRenderer *f, Qs_GpuContext *gpu,
                                              VkPassResources *ps)
{
    qs_gpu_write_image_descriptor(gpu, f->composite_desc_set, 0,
                                   ps->linear_sampler, f->hdr_view);
    qs_gpu_write_image_descriptor(gpu, f->composite_desc_set, 1,
                                   ps->linear_sampler, f->bloom_views[1]);
}

static void fwd_write_bloom_descriptors(FwdRenderer *f, Qs_GpuContext *gpu,
                                          VkPassResources *ps)
{
    qs_gpu_write_image_descriptor(gpu, f->bloom_desc_sets[0], 0,
                                   ps->linear_sampler, f->hdr_view);
    qs_gpu_write_image_descriptor(gpu, f->bloom_desc_sets[1], 0,
                                   ps->linear_sampler, f->bloom_views[0]);
}

/* ================================================================
   SHARED PASS RESOURCE CREATION
   ================================================================ */

static bool create_samplers(Qs_GpuContext *gpu, VkPassResources *ps)
{
    ps->linear_sampler = qs_gpu_create_sampler(gpu, &(Qs_GpuSamplerDesc){
        .min_filter=QS_GPU_FILTER_LINEAR, .mag_filter=QS_GPU_FILTER_LINEAR,
        .wrap_u=QS_GPU_WRAP_CLAMP_TO_EDGE, .wrap_v=QS_GPU_WRAP_CLAMP_TO_EDGE,
        .mip_levels=1 });
    ps->point_sampler = qs_gpu_create_sampler(gpu, &(Qs_GpuSamplerDesc){
        .min_filter=QS_GPU_FILTER_NEAREST, .mag_filter=QS_GPU_FILTER_NEAREST,
        .wrap_u=QS_GPU_WRAP_CLAMP_TO_EDGE, .wrap_v=QS_GPU_WRAP_CLAMP_TO_EDGE,
        .mip_levels=1 });
    ps->shadow_sampler = qs_gpu_create_sampler(gpu, &(Qs_GpuSamplerDesc){
        .min_filter=QS_GPU_FILTER_LINEAR, .mag_filter=QS_GPU_FILTER_LINEAR,
        .wrap_u=QS_GPU_WRAP_CLAMP_TO_EDGE, .wrap_v=QS_GPU_WRAP_CLAMP_TO_EDGE,
        .mip_levels=1 });
    return ps->linear_sampler && ps->point_sampler && ps->shadow_sampler;
}

static bool create_frame_set_layout(Qs_GpuContext *gpu, VkPassResources *ps)
{
    Qs_GpuDescriptorBinding b[6] = {
        {0, QS_GPU_DESCRIPTOR_UNIFORM_BUFFER,        1, QS_GPU_SHADER_VERTEX|QS_GPU_SHADER_FRAGMENT},
        {1, QS_GPU_DESCRIPTOR_UNIFORM_BUFFER,        1, QS_GPU_SHADER_FRAGMENT},
        {2, QS_GPU_DESCRIPTOR_UNIFORM_BUFFER,        1, QS_GPU_SHADER_VERTEX|QS_GPU_SHADER_FRAGMENT},
        {3, QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,1, QS_GPU_SHADER_FRAGMENT},
        {4, QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,1, QS_GPU_SHADER_FRAGMENT},
        {5, QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,1, QS_GPU_SHADER_FRAGMENT},
    };
    ps->frame_set_layout = qs_gpu_create_descriptor_set_layout(gpu, b, 6);
    return ps->frame_set_layout != NULL;
}

static bool create_single_sampler_layout(Qs_GpuContext *gpu,
                                          Qs_GpuDescriptorSetLayout **out,
                                          uint32_t count)
{
    Qs_GpuDescriptorBinding b[2] = {
        {0, QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,1, QS_GPU_SHADER_FRAGMENT},
        {1, QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,1, QS_GPU_SHADER_FRAGMENT},
    };
    *out = qs_gpu_create_descriptor_set_layout(gpu, b, count);
    return *out != NULL;
}

static bool create_shadow_pipeline(Qs_GpuContext *gpu, VkPassResources *ps)
{
    Qs_GpuShader *vs = qs_gpu_compile_shader(gpu, SHADOW_VERT, QS_GPU_SHADER_VERTEX);
    Qs_GpuShader *fs = qs_gpu_compile_shader(gpu, SHADOW_FRAG, QS_GPU_SHADER_FRAGMENT);
    if (!vs || !fs) { if(vs)qs_gpu_destroy_shader(gpu,vs); if(fs)qs_gpu_destroy_shader(gpu,fs); return false; }

    Qs_GpuPushConstantRange pc = {QS_GPU_SHADER_VERTEX, 0, 80}; /* mat4(64)+int+pad(16) = 80 */
    Qs_GpuDescriptorSetLayout *sets[] = { ps->frame_set_layout };
    Qs_GpuPipelineLayoutDesc ld = { sets, 1, &pc, 1 };
    ps->shadow_layout = qs_gpu_create_pipeline_layout(gpu, &ld);

    Qs_GpuVertexAttribute attr = {0, QS_GPU_VERTEX_FORMAT_FLOAT3, offsetof(Qs_Vertex, position)};
    Qs_GpuVertexBinding vb = {0, sizeof(Qs_Vertex), &attr, 1};
    Qs_GpuGraphicsPipelineDesc pd = {
        ps->shadow_layout, vs, fs, &vb, 1,
        QS_GPU_TOPOLOGY_TRIANGLES, QS_GPU_CULL_FRONT,
        true, true,
        QS_GPU_FORMAT_NONE, QS_GPU_FORMAT_D32_SFLOAT
    };
    ps->shadow_pipeline = qs_gpu_create_graphics_pipeline(gpu, &pd);
    qs_gpu_destroy_shader(gpu, vs); qs_gpu_destroy_shader(gpu, fs);
    return ps->shadow_pipeline != NULL;
}

static bool create_forward_pipeline(Qs_GpuContext *gpu, VkPassResources *ps)
{
    Qs_GpuShader *vs = qs_gpu_compile_shader(gpu, FORWARD_VERT, QS_GPU_SHADER_VERTEX);
    Qs_GpuShader *fs = qs_gpu_compile_shader(gpu, FORWARD_FRAG, QS_GPU_SHADER_FRAGMENT);
    if (!vs || !fs) { if(vs)qs_gpu_destroy_shader(gpu,vs); if(fs)qs_gpu_destroy_shader(gpu,fs); return false; }

    Qs_GpuDescriptorSetLayout *mat_layout = qs_material_set_layout();
    if (!mat_layout) {
        qs_gpu_destroy_shader(gpu,vs); qs_gpu_destroy_shader(gpu,fs);
        QS_LOG_ERROR("VkForward: material set layout unavailable"); return false;
    }

    Qs_GpuPushConstantRange pc = {QS_GPU_SHADER_VERTEX, 0, 64};
    Qs_GpuDescriptorSetLayout *sets[] = { ps->frame_set_layout, mat_layout };
    Qs_GpuPipelineLayoutDesc ld = { sets, 2, &pc, 1 };
    ps->forward_layout = qs_gpu_create_pipeline_layout(gpu, &ld);

    Qs_GpuVertexAttribute attrs[4] = {
        {0, QS_GPU_VERTEX_FORMAT_FLOAT3, offsetof(Qs_Vertex, position)},
        {1, QS_GPU_VERTEX_FORMAT_FLOAT3, offsetof(Qs_Vertex, normal)},
        {2, QS_GPU_VERTEX_FORMAT_FLOAT4, offsetof(Qs_Vertex, tangent)},
        {3, QS_GPU_VERTEX_FORMAT_FLOAT2, offsetof(Qs_Vertex, uv)},
    };
    Qs_GpuVertexBinding vb = {0, sizeof(Qs_Vertex), attrs, 4};
    Qs_GpuGraphicsPipelineDesc pd = {
        ps->forward_layout, vs, fs, &vb, 1,
        QS_GPU_TOPOLOGY_TRIANGLES, QS_GPU_CULL_BACK,
        true, true,
        QS_GPU_FORMAT_RGBA16_SFLOAT, QS_GPU_FORMAT_DEPTH_AUTO
    };
    ps->forward_pipeline = qs_gpu_create_graphics_pipeline(gpu, &pd);
    qs_gpu_destroy_shader(gpu, vs); qs_gpu_destroy_shader(gpu, fs);
    return ps->forward_pipeline != NULL;
}

static bool create_bloom_pipelines(Qs_GpuContext *gpu, VkPassResources *ps)
{
    if (!create_single_sampler_layout(gpu, &ps->bloom_set_layout, 1)) return false;

    Qs_GpuPushConstantRange pc = {QS_GPU_SHADER_FRAGMENT, 0, 16};
    Qs_GpuDescriptorSetLayout *sets[] = { ps->bloom_set_layout };
    Qs_GpuPipelineLayoutDesc ld = { sets, 1, &pc, 1 };
    ps->bloom_layout = qs_gpu_create_pipeline_layout(gpu, &ld);
    if (!ps->bloom_layout) return false;

    Qs_GpuShader *fv = qs_gpu_compile_shader(gpu, FULLSCREEN_VERT, QS_GPU_SHADER_VERTEX);
    Qs_GpuShader *fd = qs_gpu_compile_shader(gpu, BLOOM_DOWN_FRAG, QS_GPU_SHADER_FRAGMENT);
    Qs_GpuShader *fu = qs_gpu_compile_shader(gpu, BLOOM_UP_FRAG,   QS_GPU_SHADER_FRAGMENT);
    if (!fv||!fd||!fu) {
        if(fv)qs_gpu_destroy_shader(gpu,fv);
        if(fd)qs_gpu_destroy_shader(gpu,fd);
        if(fu)qs_gpu_destroy_shader(gpu,fu);
        return false;
    }

    Qs_GpuGraphicsPipelineDesc pd = {
        ps->bloom_layout, fv, fd, NULL, 0,
        QS_GPU_TOPOLOGY_TRIANGLES, QS_GPU_CULL_NONE,
        false, false,
        QS_GPU_FORMAT_RGBA16_SFLOAT, QS_GPU_FORMAT_DEPTH_AUTO
    };
    ps->bloom_down_pipeline = qs_gpu_create_graphics_pipeline(gpu, &pd);
    pd.fragment_shader = fu;
    ps->bloom_up_pipeline = qs_gpu_create_graphics_pipeline(gpu, &pd);
    qs_gpu_destroy_shader(gpu, fv); qs_gpu_destroy_shader(gpu, fd); qs_gpu_destroy_shader(gpu, fu);
    return ps->bloom_down_pipeline && ps->bloom_up_pipeline;
}

static bool create_composite_pipeline(Qs_GpuContext *gpu, VkPassResources *ps,
                                       Qs_GpuImageFormat swapchain_fmt)
{
    if (!create_single_sampler_layout(gpu, &ps->composite_set_layout, 2)) return false;

    Qs_GpuPushConstantRange pc = {QS_GPU_SHADER_FRAGMENT, 0, 16};
    Qs_GpuDescriptorSetLayout *sets[] = { ps->composite_set_layout };
    Qs_GpuPipelineLayoutDesc ld = { sets, 1, &pc, 1 };
    ps->composite_layout = qs_gpu_create_pipeline_layout(gpu, &ld);
    if (!ps->composite_layout) return false;

    Qs_GpuShader *fv = qs_gpu_compile_shader(gpu, FULLSCREEN_VERT, QS_GPU_SHADER_VERTEX);
    Qs_GpuShader *fc = qs_gpu_compile_shader(gpu, COMPOSITE_FRAG,  QS_GPU_SHADER_FRAGMENT);
    if (!fv||!fc) { if(fv)qs_gpu_destroy_shader(gpu,fv); if(fc)qs_gpu_destroy_shader(gpu,fc); return false; }

    Qs_GpuGraphicsPipelineDesc pd = {
        ps->composite_layout, fv, fc, NULL, 0,
        QS_GPU_TOPOLOGY_TRIANGLES, QS_GPU_CULL_NONE,
        false, false,
        swapchain_fmt, QS_GPU_FORMAT_DEPTH_AUTO
    };
    ps->composite_pipeline = qs_gpu_create_graphics_pipeline(gpu, &pd);
    qs_gpu_destroy_shader(gpu, fv); qs_gpu_destroy_shader(gpu, fc);
    return ps->composite_pipeline != NULL;
}

static bool vk_pass_resources_init(Qs_GpuContext *gpu, VkPassResources *ps)
{
    if (ps->ok) return true;
    if (!create_samplers(gpu, ps))               { QS_LOG_ERROR("VkForward: samplers failed");           return false; }
    if (!create_frame_set_layout(gpu, ps))       { QS_LOG_ERROR("VkForward: frame set layout failed");   return false; }
    if (!create_shadow_pipeline(gpu, ps))        { QS_LOG_ERROR("VkForward: shadow pipeline failed");    return false; }
    if (!create_forward_pipeline(gpu, ps))       { QS_LOG_ERROR("VkForward: forward pipeline failed");   return false; }
    if (!create_bloom_pipelines(gpu, ps))        { QS_LOG_ERROR("VkForward: bloom pipelines failed");    return false; }
    if (!create_composite_pipeline(gpu, ps, QS_GPU_FORMAT_BGRA8_UNORM))
                                                 { QS_LOG_ERROR("VkForward: composite pipeline failed"); return false; }
    ps->ok = true;
    QS_LOG_INFO("VkForward: shared pass resources ready");
    return true;
}

static void vk_pass_resources_shutdown(Qs_GpuContext *gpu, VkPassResources *ps)
{
    if (!ps->ok) return;
#define DP(x) if(ps->x){qs_gpu_destroy_pipeline(gpu,ps->x);ps->x=NULL;}
#define DL(x) if(ps->x){qs_gpu_destroy_pipeline_layout(gpu,ps->x);ps->x=NULL;}
#define DD(x) if(ps->x){qs_gpu_destroy_descriptor_set_layout(gpu,ps->x);ps->x=NULL;}
#define DS(x) if(ps->x){qs_gpu_destroy_sampler(gpu,ps->x);ps->x=NULL;}
    DP(shadow_pipeline)   DL(shadow_layout)
    DP(forward_pipeline)  DL(forward_layout)   DD(frame_set_layout)
    DP(bloom_down_pipeline) DP(bloom_up_pipeline) DL(bloom_layout) DD(bloom_set_layout)
    DP(composite_pipeline) DL(composite_layout) DD(composite_set_layout)
    DS(linear_sampler) DS(point_sampler) DS(shadow_sampler)
#undef DP
#undef DL
#undef DD
#undef DS
    ps->ok = false;
}

/* ================================================================
   DESCRIPTOR POOL + SET ALLOCATION
   ================================================================ */

static bool fwd_alloc_descriptors(FwdRenderer *f, Qs_GpuContext *gpu, VkPassResources *ps)
{
    Qs_GpuDescriptorPoolSize sizes[] = {
        {QS_GPU_DESCRIPTOR_UNIFORM_BUFFER,         12},
        {QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER, 12},
    };
    Qs_GpuDescriptorPoolDesc pd = { sizes, 2, 8 };
    f->desc_pool = qs_gpu_create_descriptor_pool(gpu, &pd);
    if (!f->desc_pool) return false;

    f->frame_desc_set     = qs_gpu_alloc_descriptor_set(gpu, f->desc_pool, ps->frame_set_layout);
    f->composite_desc_set = qs_gpu_alloc_descriptor_set(gpu, f->desc_pool, ps->composite_set_layout);
    f->bloom_desc_sets[0] = qs_gpu_alloc_descriptor_set(gpu, f->desc_pool, ps->bloom_set_layout);
    f->bloom_desc_sets[1] = qs_gpu_alloc_descriptor_set(gpu, f->desc_pool, ps->bloom_set_layout);
    return f->frame_desc_set && f->composite_desc_set &&
           f->bloom_desc_sets[0] && f->bloom_desc_sets[1];
}

static bool fwd_create_ubos(FwdRenderer *f, Qs_GpuContext *gpu)
{
    Qs_GpuBufferDesc bd = {0, QS_GPU_BUFFER_UNIFORM, QS_GPU_MEMORY_HOST_VISIBLE};
    bd.size = sizeof(FrameUBO);  f->frame_ubo  = qs_gpu_create_buffer(gpu, &bd);
    bd.size = sizeof(LightUBO);  f->light_ubo  = qs_gpu_create_buffer(gpu, &bd);
    bd.size = sizeof(ShadowUBO); f->shadow_ubo = qs_gpu_create_buffer(gpu, &bd);
    return f->frame_ubo && f->light_ubo && f->shadow_ubo;
}

/* ================================================================
   RENDER NODE CALLBACKS
   ================================================================ */

/* Pass 0: CSM shadow maps */
static void shadow_pass_execute(const Qs_RenderContext *ctx, void *user_data)
{
    FwdRenderer     *f  = user_data;
    VkPassResources *ps = vk_renderer_pass_resources();
    if (!ps || !ps->ok || !f->ok) return;

    uint32_t nr = 0;
    const Qs_Renderable *rends = qs_renderer_renderables(ctx->renderer, &nr);
    if (nr == 0) return;

    /* Update ShadowUBO CSM matrices */
    Qs_Camera *cam = qs_renderer_camera(ctx->renderer);
    compute_csm(cam, f->r->shadow_matrices, f->r->shadow_splits);

    ShadowUBO *subo = qs_gpu_map_buffer(f->r->gpu, f->shadow_ubo);
    if (subo) {
        for (int c = 0; c < QS_CSM_CASCADES; c++) {
            memcpy(subo->cascade_vp[c], f->r->shadow_matrices[c], 64);
            subo->cascade_splits[c] = f->r->shadow_splits[c+1];
        }
        qs_gpu_unmap_buffer(f->r->gpu, f->shadow_ubo);
    }

    for (int cascade = 0; cascade < QS_CSM_CASCADES; cascade++) {
        qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
            .image=f->shadow_images[cascade],
            .old_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
            .new_layout=QS_GPU_IMAGE_LAYOUT_DEPTH_ATTACHMENT,
            .aspect=QS_GPU_IMAGE_ASPECT_DEPTH, .base_mip=0, .mip_count=1 });

        qs_cmd_begin_rendering(ctx->cmd, &(Qs_GpuRenderTarget){
            .color=NULL, .depth=f->shadow_views[cascade],
            .clear_depth=1.0f,
            .width=QS_SHADOW_MAP_SIZE, .height=QS_SHADOW_MAP_SIZE });
        qs_cmd_set_viewport(ctx->cmd, QS_SHADOW_MAP_SIZE, QS_SHADOW_MAP_SIZE);
        qs_cmd_bind_pipeline(ctx->cmd, ps->shadow_pipeline);
        qs_cmd_bind_descriptor_set(ctx->cmd, ps->shadow_layout, 0, f->frame_desc_set);

        for (uint32_t ri = 0; ri < nr; ri++) {
            const Qs_Renderable *ren = &rends[ri];
            if (!ren->cast_shadows || !ren->mesh) continue;
            typedef struct { float model[16]; int32_t cascade_idx; int32_t _p[3]; } ShadowPC;
            ShadowPC spc; memcpy(spc.model, ren->transform, 64);
            spc.cascade_idx = cascade; spc._p[0]=spc._p[1]=spc._p[2]=0;
            qs_cmd_push_constants(ctx->cmd, ps->shadow_layout,
                                  QS_GPU_SHADER_VERTEX, 0, sizeof(ShadowPC), &spc);
            qs_mesh_bind(ren->mesh, ctx->cmd);
            qs_mesh_draw(ren->mesh, ctx->cmd);
        }
        qs_cmd_end_rendering(ctx->cmd);

        qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
            .image=f->shadow_images[cascade],
            .old_layout=QS_GPU_IMAGE_LAYOUT_DEPTH_ATTACHMENT,
            .new_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
            .aspect=QS_GPU_IMAGE_ASPECT_DEPTH, .base_mip=0, .mip_count=1 });
    }
}

/* Pass 1: Forward lit (HDR target) */
static void forward_pass_execute(const Qs_RenderContext *ctx, void *user_data)
{
    FwdRenderer     *f  = user_data;
    VkPassResources *ps = vk_renderer_pass_resources();
    if (!ps || !ps->ok || !f->ok) return;

    /* If offscreen images are not sized for this frame, skip rendering.
       vk_forward_resize (called from on_resize) will recreate them safely
       outside the frame-recording callback where vkDeviceWaitIdle is allowed. */
    if (ctx->width != f->last_w || ctx->height != f->last_h) return;

    /* Update FrameUBO */
    FrameUBO *fubo = qs_gpu_map_buffer(f->r->gpu, f->frame_ubo);
    if (fubo) {
        memcpy(fubo->view, ctx->view, 64);
        memcpy(fubo->proj, ctx->proj, 64);
        fwd_mat4_identity(fubo->inv_view_proj);
        Qs_Camera *cam = qs_renderer_camera(ctx->renderer);
        fubo->cam_pos[0] = cam->position[0];
        fubo->cam_pos[1] = cam->position[1];
        fubo->cam_pos[2] = cam->position[2];
        fubo->time = ctx->dt;
        fubo->screen_width  = (float)ctx->width;
        fubo->screen_height = (float)ctx->height;
        qs_gpu_unmap_buffer(f->r->gpu, f->frame_ubo);
    }

    /* Update LightUBO */
    uint32_t nl = 0;
    const Qs_LightGPU *lights = qs_renderer_lights(ctx->renderer, &nl);
    LightUBO *lubo = qs_gpu_map_buffer(f->r->gpu, f->light_ubo);
    if (lubo) {
        lubo->count = nl < QS_MAX_LIGHTS_GPU ? nl : QS_MAX_LIGHTS_GPU;
        if (lights && lubo->count > 0)
            memcpy(lubo->lights, lights, lubo->count * sizeof(LightGPUEntry));
        qs_gpu_unmap_buffer(f->r->gpu, f->light_ubo);
    }

    /* Render into HDR */
    uint32_t nr = 0;
    const Qs_Renderable *rends = qs_renderer_renderables(ctx->renderer, &nr);

    qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
        .image=f->hdr_image,
        .old_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
        .new_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .aspect=QS_GPU_IMAGE_ASPECT_COLOR, .base_mip=0, .mip_count=1 });

    qs_cmd_begin_rendering(ctx->cmd, &(Qs_GpuRenderTarget){
        .color=f->hdr_view, .depth=f->r->depth_view,
        .clear_color={f->r->clear_color[0], f->r->clear_color[1],
                      f->r->clear_color[2], f->r->clear_color[3]},
        .clear_depth=1.0f, .width=ctx->width, .height=ctx->height });
    qs_cmd_set_viewport(ctx->cmd, ctx->width, ctx->height);
    qs_cmd_bind_pipeline(ctx->cmd, ps->forward_pipeline);
    qs_cmd_bind_descriptor_set(ctx->cmd, ps->forward_layout, 0, f->frame_desc_set);

    for (uint32_t ri = 0; ri < nr; ri++) {
        const Qs_Renderable *ren = &rends[ri];
        if (!ren->mesh) continue;
        Qs_GpuDescriptorSet *mat_ds = ren->material
            ? qs_material_descriptor_set(ren->material) : NULL;
        if (!mat_ds)
            mat_ds = f->default_material
                ? qs_material_descriptor_set(f->default_material) : NULL;
        if (!mat_ds) continue;
        qs_cmd_push_constants(ctx->cmd, ps->forward_layout,
                              QS_GPU_SHADER_VERTEX, 0, 64, ren->transform);
        qs_cmd_bind_descriptor_set(ctx->cmd, ps->forward_layout, 1, mat_ds);
        qs_mesh_bind(ren->mesh, ctx->cmd);
        qs_mesh_draw(ren->mesh, ctx->cmd);
    }
    qs_cmd_end_rendering(ctx->cmd);

    qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
        .image=f->hdr_image,
        .old_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .new_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
        .aspect=QS_GPU_IMAGE_ASPECT_COLOR, .base_mip=0, .mip_count=1 });
}

/* Pass 2: Bloom */
static void bloom_pass_execute(const Qs_RenderContext *ctx, void *user_data)
{
    FwdRenderer     *f  = user_data;
    VkPassResources *ps = vk_renderer_pass_resources();
    if (!ps || !ps->ok || !f->ok) return;

    uint32_t bw = (ctx->width+1)/2, bh = (ctx->height+1)/2;

    typedef struct { float inv_w, inv_h, _p[2]; } BloomPC;

    /* Downsample: HDR -> bloom[0] */
    qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
        .image=f->bloom_images[0], .old_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
        .new_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .aspect=QS_GPU_IMAGE_ASPECT_COLOR, .base_mip=0, .mip_count=1 });
    qs_cmd_begin_rendering(ctx->cmd, &(Qs_GpuRenderTarget){
        .color=f->bloom_views[0], .depth=NULL, .clear_color={0,0,0,0},
        .width=bw, .height=bh });
    qs_cmd_set_viewport(ctx->cmd, bw, bh);
    qs_cmd_bind_pipeline(ctx->cmd, ps->bloom_down_pipeline);
    qs_cmd_bind_descriptor_set(ctx->cmd, ps->bloom_layout, 0, f->bloom_desc_sets[0]);
    BloomPC bpc = {1.0f/(float)ctx->width, 1.0f/(float)ctx->height, {0,0}};
    qs_cmd_push_constants(ctx->cmd, ps->bloom_layout, QS_GPU_SHADER_FRAGMENT, 0, 16, &bpc);
    qs_cmd_draw(ctx->cmd, 3, 0);
    qs_cmd_end_rendering(ctx->cmd);
    qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
        .image=f->bloom_images[0], .old_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .new_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
        .aspect=QS_GPU_IMAGE_ASPECT_COLOR, .base_mip=0, .mip_count=1 });

    /* Upsample: bloom[0] -> bloom[1] */
    qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
        .image=f->bloom_images[1], .old_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
        .new_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .aspect=QS_GPU_IMAGE_ASPECT_COLOR, .base_mip=0, .mip_count=1 });
    qs_cmd_begin_rendering(ctx->cmd, &(Qs_GpuRenderTarget){
        .color=f->bloom_views[1], .depth=NULL, .clear_color={0,0,0,0},
        .width=bw, .height=bh });
    qs_cmd_set_viewport(ctx->cmd, bw, bh);
    qs_cmd_bind_pipeline(ctx->cmd, ps->bloom_up_pipeline);
    qs_cmd_bind_descriptor_set(ctx->cmd, ps->bloom_layout, 0, f->bloom_desc_sets[1]);
    BloomPC upc = {1.0f/(float)bw, 1.0f/(float)bh, {0,0}};
    qs_cmd_push_constants(ctx->cmd, ps->bloom_layout, QS_GPU_SHADER_FRAGMENT, 0, 16, &upc);
    qs_cmd_draw(ctx->cmd, 3, 0);
    qs_cmd_end_rendering(ctx->cmd);
    qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
        .image=f->bloom_images[1], .old_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .new_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
        .aspect=QS_GPU_IMAGE_ASPECT_COLOR, .base_mip=0, .mip_count=1 });
}

/* Pass 3: Composite (ACES tonemap + vignette -> swapchain) */
static void composite_pass_execute(const Qs_RenderContext *ctx, void *user_data)
{
    FwdRenderer     *f  = user_data;
    VkPassResources *ps = vk_renderer_pass_resources();
    if (!ps || !ps->ok || !f->ok) return;

    Qs_GpuImageView *sc_view = vk_renderer_swapchain_view(f->r);
    uint32_t sc_w = vk_renderer_swapchain_width(f->r);
    uint32_t sc_h = vk_renderer_swapchain_height(f->r);
    if (!sc_view || sc_w == 0 || sc_h == 0) return;

    qs_cmd_begin_rendering(ctx->cmd, &(Qs_GpuRenderTarget){
        .color=sc_view, .depth=NULL,
        .clear_color={0,0,0,0}, .width=sc_w, .height=sc_h });
    qs_cmd_set_viewport(ctx->cmd, sc_w, sc_h);
    qs_cmd_bind_pipeline(ctx->cmd, ps->composite_pipeline);
    qs_cmd_bind_descriptor_set(ctx->cmd, ps->composite_layout, 0, f->composite_desc_set);
    typedef struct { float inv_w, inv_h, bloom_str, vignette_str; } CompositePC;
    CompositePC cpc = {1.0f/(float)sc_w, 1.0f/(float)sc_h, 0.04f, 0.35f};
    qs_cmd_push_constants(ctx->cmd, ps->composite_layout,
                          QS_GPU_SHADER_FRAGMENT, 0, 16, &cpc);
    qs_cmd_draw(ctx->cmd, 3, 0);
    qs_cmd_end_rendering(ctx->cmd);
}

/* ================================================================
   PUBLIC API: attach / detach / resize
   ================================================================ */

/* Called from on_resize (outside frame-recording callback) — safe to call
   vkDeviceWaitIdle and submit transfer commands here. */
void vk_forward_resize(VkRenderer *r, uint32_t w, uint32_t h)
{
    FwdRenderer *f = fwd_find(r);
    if (!f || w == 0 || h == 0) return;

    Qs_GpuContext *gpu = f->r->gpu;
    VkPassResources *ps = vk_renderer_pass_resources();
    if (!ps || !ps->ok) return;

    /* Destroy old offscreen images (calls vkDeviceWaitIdle — safe here). */
    fwd_destroy_offscreen(f, gpu);

    /* Recreate at the new size. */
    if (!fwd_create_offscreen(f, gpu, w, h)) {
        QS_LOG_ERROR("VkForward: offscreen resize failed (%ux%u)", w, h);
        f->ok = false;
        return;
    }

    /* Re-write all descriptors that reference the new image views. */
    fwd_write_frame_descriptors(f, gpu, ps);
    fwd_write_composite_descriptors(f, gpu, ps);
    fwd_write_bloom_descriptors(f, gpu, ps);

    /* Sync back-pointers in VkRenderer. */
    r->hdr_image  = f->hdr_image;
    r->hdr_view   = f->hdr_view;
    for (int i = 0; i < QS_CSM_CASCADES; i++) {
        r->shadow_images[i]       = f->shadow_images[i];
        r->shadow_views[i]        = f->shadow_views[i];
        r->shadow_sample_views[i] = f->shadow_sample_views[i];
    }
    for (int i = 0; i < 2; i++) {
        r->bloom_images[i] = f->bloom_images[i];
        r->bloom_views[i]  = f->bloom_views[i];
    }

    f->ok = true;
    QS_LOG_INFO("VkForward: resized to %ux%u", w, h);
}

void vk_forward_attach(Qs_Engine *engine, VkRenderer *renderer)
{
    if (!renderer) return;
    FwdRenderer *f = fwd_alloc(renderer);
    if (!f) { QS_LOG_ERROR("VkForward: renderer pool full"); return; }
    f->engine = engine;

    Qs_GpuContext *gpu = qs_engine_gpu(engine);
    VkPassResources *ps = vk_renderer_pass_resources();
    if (!vk_pass_resources_init(gpu, ps)) { f->r = NULL; return; }

    if (!fwd_create_ubos(f, gpu)) {
        QS_LOG_ERROR("VkForward: UBO creation failed"); f->r = NULL; return; }
    if (!fwd_alloc_descriptors(f, gpu, ps)) {
        QS_LOG_ERROR("VkForward: descriptor alloc failed"); f->r = NULL; return; }
    /* Initial 1x1 offscreen — resized on first real frame */
    if (!fwd_create_offscreen(f, gpu, 1, 1)) {
        QS_LOG_ERROR("VkForward: offscreen creation failed"); f->r = NULL; return; }

    fwd_write_frame_descriptors(f, gpu, ps);
    fwd_write_composite_descriptors(f, gpu, ps);
    fwd_write_bloom_descriptors(f, gpu, ps);

    /* Sync pointers to VkRenderer */
    renderer->hdr_image  = f->hdr_image;
    renderer->hdr_view   = f->hdr_view;
    renderer->frame_ubo  = f->frame_ubo;
    renderer->light_ubo  = f->light_ubo;
    renderer->shadow_ubo = f->shadow_ubo;
    renderer->desc_pool  = f->desc_pool;
    renderer->frame_desc_set     = f->frame_desc_set;
    renderer->composite_desc_set = f->composite_desc_set;
    renderer->bloom_desc_sets[0] = f->bloom_desc_sets[0];
    renderer->bloom_desc_sets[1] = f->bloom_desc_sets[1];
    for (int i = 0; i < QS_CSM_CASCADES; i++) {
        renderer->shadow_images[i]       = f->shadow_images[i];
        renderer->shadow_views[i]        = f->shadow_views[i];
        renderer->shadow_sample_views[i] = f->shadow_sample_views[i];
    }
    for (int i = 0; i < 2; i++) {
        renderer->bloom_images[i] = f->bloom_images[i];
        renderer->bloom_views[i]  = f->bloom_views[i];
    }

    f->default_material = qs_material_create(engine, &(Qs_MaterialDesc){
        .name = "_fwd_default",
        .base_color_factor    = { 0.8f, 0.8f, 0.8f, 1.0f },
        .metallic_factor      = 0.0f,
        .roughness_factor     = 0.8f,
    });
    if (!f->default_material) QS_LOG_WARN("VkForward: default material failed");
    /* f->ok stays false until vk_forward_resize() is called from on_resize.
       This prevents render passes from running before the offscreen images are
       properly sized (avoids vkDeviceWaitIdle inside the frame recording callback). */

    f->shadow_node = vk_renderer_add_node_impl(renderer, &(Qs_RenderNodeDesc){
        .name="shadow_csm", .priority=0, .execute=shadow_pass_execute, .user_data=f });
    f->forward_node = vk_renderer_add_node_impl(renderer, &(Qs_RenderNodeDesc){
        .name="forward_pbr", .priority=100, .execute=forward_pass_execute, .user_data=f });
    f->bloom_node = vk_renderer_add_node_impl(renderer, &(Qs_RenderNodeDesc){
        .name="bloom", .priority=200, .execute=bloom_pass_execute, .user_data=f });
    f->composite_node = vk_renderer_add_node_impl(renderer, &(Qs_RenderNodeDesc){
        .name="composite", .priority=300, .execute=composite_pass_execute, .user_data=f });

    QS_LOG_INFO("VkForward: Forward+ renderer attached");
}

void vk_forward_detach(VkRenderer *renderer)
{
    FwdRenderer *f = fwd_find(renderer);
    if (!f) return;
    Qs_GpuContext *gpu = qs_engine_gpu(f->engine);

    if (f->shadow_node)    vk_renderer_remove_node_impl(renderer, f->shadow_node);
    if (f->forward_node)   vk_renderer_remove_node_impl(renderer, f->forward_node);
    if (f->bloom_node)     vk_renderer_remove_node_impl(renderer, f->bloom_node);
    if (f->composite_node) vk_renderer_remove_node_impl(renderer, f->composite_node);

    if (f->default_material) qs_material_destroy(f->default_material);

    fwd_destroy_offscreen(f, gpu);
    if (f->frame_ubo)  qs_gpu_destroy_buffer(gpu, f->frame_ubo);
    if (f->light_ubo)  qs_gpu_destroy_buffer(gpu, f->light_ubo);
    if (f->shadow_ubo) qs_gpu_destroy_buffer(gpu, f->shadow_ubo);
    if (f->desc_pool)  qs_gpu_destroy_descriptor_pool(gpu, f->desc_pool);

    renderer->hdr_image  = NULL; renderer->hdr_view   = NULL;
    renderer->frame_ubo  = NULL; renderer->light_ubo  = NULL;
    renderer->shadow_ubo = NULL; renderer->desc_pool  = NULL;
    renderer->frame_desc_set = NULL; renderer->composite_desc_set = NULL;

    memset(f, 0, sizeof(FwdRenderer));
    QS_LOG_INFO("VkForward: detached");

    /* Shut down shared resources if no renderers remain */
    bool any = false;
    for (int i = 0; i < MAX_FWD_RENDERERS; i++)
        if (g_fwd_pool[i].r) { any = true; break; }
    if (!any) {
        VkPassResources *ps = vk_renderer_pass_resources();
        if (ps) vk_pass_resources_shutdown(gpu, ps);
    }
}
