/*
 * pbr_forward.c  --  Forward+ renderer passes for the PBR backend.
 *
 * Pass layout (priority order):
 *   Pass 0 (priority   0):  CSM shadow depth  (QS_CSM_CASCADES cascades)
 *   Pass 1 (priority 100):  Forward lit        (PBR GGX + CSM shadows)
 *   Pass 2 (priority 200):  Bloom              (Kawase downsample + tent up)
 *   Pass 3 (priority 300):  Composite          (ACES tonemap + vignette -> swapchain)
 *
 * Descriptor layout:
 *   set=0  binding 0  UNIFORM_BUFFER           FrameUBO   (engine-written)
 *   set=0  binding 1  UNIFORM_BUFFER           LightsUBO  (engine-written)
 *   set=0  binding 2  UNIFORM_BUFFER           ShadowUBO  (plugin-written, CSM data)
 *   set=0  binding 3-5 COMBINED_IMAGE_SAMPLER  shadow maps [3]
 *   set=1  material textures  (5 COMBINED_IMAGE_SAMPLER via qs_material_set_layout)
 *
 * Engine now owns:  depth buffer, frame_ubo, lights_ubo, HDR attachment,
 *                   bloom attachments, shadow map attachments.
 * Plugin owns:      pipelines, descriptor sets, shadow_ubo, CSM matrices.
 */

#include "qs_renderer.h"
#include "qs_math.h"
#include "qs_gpu.h"
#include "qs_material.h"
#include "qs_light.h"
#include "qs_log.h"
#include "pbr_internal.h"

#include <string.h>
#include <math.h>

/* ---- Post-process settings (mutable, read by composite pass each frame) ---- */
static PbrPostProcessSettings g_pp_settings = {
    .bloom_strength    = 0.04f,
    .vignette_strength = 0.35f,
};

PbrPostProcessSettings *pbr_post_process_settings(void) { return &g_pp_settings; }
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ================================================================
   GPU DATA STRUCTURES  (must match shader std140)
   ================================================================ */

typedef struct {
    float cascade_vp[QS_CSM_CASCADES][16];
    float cascade_splits[QS_CSM_CASCADES];
    float _pad;
} ShadowUBO;

/* Material params pushed at offset 64 (fragment stage), total push constant = 112 bytes */
typedef struct {
    float base_color_factor[4];  /* offset  0 rel (abs  64), 16 bytes */
    float metallic_factor;       /* offset 16 rel (abs  80) */
    float roughness_factor;      /* offset 20 rel (abs  84) */
    float normal_scale;          /* offset 24 rel (abs  88) */
    float occlusion_strength;    /* offset 28 rel (abs  92) */
    float emissive_factor[3];    /* offset 32 rel (abs  96), 12 bytes */
    float alpha_cutoff;          /* offset 44 rel (abs 108) */
} FwdMatPC;                      /* total: 48 bytes */

/* ================================================================
   GLSL SHADERS
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

static const char *SHADOW_FRAG = "#version 450\nvoid main() {}\n";

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
    "    float screen_width; float screen_height; uint debug_flags; float _pad;\n"
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
    "    float screen_width; float screen_height; uint debug_flags; float _pad;\n"
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
    "    float cascade_splits[3]; float _pad;\n"
    "} shadow_data;\n"
    "layout(set = 0, binding = 3) uniform sampler2D shadow_map_0;\n"
    "layout(set = 0, binding = 4) uniform sampler2D shadow_map_1;\n"
    "layout(set = 0, binding = 5) uniform sampler2D shadow_map_2;\n"
    "layout(set = 1, binding = 0) uniform sampler2D u_base_color;\n"
    "layout(set = 1, binding = 1) uniform sampler2D u_metallic_roughness;\n"
    "layout(set = 1, binding = 2) uniform sampler2D u_normal_map;\n"
    "layout(set = 1, binding = 3) uniform sampler2D u_occlusion;\n"
    "layout(set = 1, binding = 4) uniform sampler2D u_emissive;\n"
    "layout(push_constant) uniform MatPC {\n"
    "    layout(offset=64)  vec4  base_color_factor;\n"
    "    layout(offset=80)  float metallic_factor;\n"
    "    layout(offset=84)  float roughness_factor;\n"
    "    layout(offset=88)  float normal_scale;\n"
    "    layout(offset=92)  float occlusion_strength;\n"
    "    layout(offset=96)  vec3  emissive_factor;\n"
    "    layout(offset=108) float alpha_cutoff;\n"
    "} mat_pc;\n"
    "layout(location = 0) out vec4 out_color;\n"
    "const float PI = 3.14159265359;\n"
    "float D_GGX(float NdotH, float r) { float a2=(r*r)*(r*r); float d=NdotH*NdotH*(a2-1.0)+1.0; return a2/(PI*d*d); }\n"
    "float G_Smith(float NdotV, float NdotL, float r) {\n"
    "    float k=(r+1.0)*(r+1.0)/8.0;\n"
    "    return (NdotV/(NdotV*(1.0-k)+k))*(NdotL/(NdotL*(1.0-k)+k)); }\n"
    "vec3 F_Schlick(float c, vec3 F0) { return F0+(1.0-F0)*pow(clamp(1.0-c,0.0,1.0),5.0); }\n"
    "const vec2 POISSON[16]=vec2[](\n"
    "    vec2(-0.94201624,-0.39906216),vec2( 0.94558609,-0.76890725),\n"
    "    vec2(-0.09418410,-0.92938870),vec2( 0.34495938, 0.29387760),\n"
    "    vec2(-0.91588581, 0.45771432),vec2(-0.81544232,-0.87912464),\n"
    "    vec2(-0.38277543, 0.27676845),vec2( 0.97484398, 0.75648379),\n"
    "    vec2( 0.44323325,-0.97511554),vec2( 0.53742981,-0.47373420),\n"
    "    vec2(-0.26496911,-0.41893023),vec2( 0.79197514, 0.19090188),\n"
    "    vec2(-0.24188840, 0.99706507),vec2(-0.81409955, 0.91437590),\n"
    "    vec2( 0.19984126, 0.78641367),vec2( 0.14383161,-0.14100790));\n"
    "float shadow_pcf(sampler2D sm,vec4 lsp,float bias){\n"
    "    vec3 p=lsp.xyz/lsp.w; vec2 uv=p.xy*0.5+0.5;\n"
    "    if(p.z<0.0||p.z>1.0||any(lessThan(uv,vec2(0.0)))||any(greaterThan(uv,vec2(1.0)))) return 1.0;\n"
    "    float z=p.z-bias;\n"
    "    float spread=1.5/float(textureSize(sm,0).x);\n"
    "    float angle=fract(52.9829189*fract(dot(gl_FragCoord.xy,vec2(0.06711056,0.00583715))))*6.28318;\n"
    "    float ca=cos(angle); float sa=sin(angle);\n"
    "    float s=0.0;\n"
    "    for(int i=0;i<16;i++){\n"
    "        vec2 r=vec2(ca*POISSON[i].x-sa*POISSON[i].y,sa*POISSON[i].x+ca*POISSON[i].y);\n"
    "        s+=(texture(sm,uv+r*spread).r<z)?0.0:1.0;}\n"
    "    return s/16.0;}\n"
    "float compute_shadow(vec3 wpos,vec3 N,vec3 sun_dir){\n"
    "    float NdotL=max(dot(N,normalize(-sun_dir)),0.0);\n"
    "    float bias=max(0.001*(1.0-NdotL),0.0003);\n"
    "    float depth=-(frame.view*vec4(wpos,1.0)).z;\n"
    "    float split0=shadow_data.cascade_splits[0];\n"
    "    float split1=shadow_data.cascade_splits[1];\n"
    "    float s;\n"
    "    if(depth<split0){\n"
    "        s=shadow_pcf(shadow_map_0,shadow_data.cascade_vp[0]*vec4(wpos,1.0),bias);\n"
    "        float bt=split0*0.85; if(depth>bt){\n"
    "            float t=clamp((depth-bt)/(split0-bt),0.0,1.0);\n"
    "            s=mix(s,shadow_pcf(shadow_map_1,shadow_data.cascade_vp[1]*vec4(wpos,1.0),bias),t);}\n"
    "    }else if(depth<split1){\n"
    "        s=shadow_pcf(shadow_map_1,shadow_data.cascade_vp[1]*vec4(wpos,1.0),bias);\n"
    "        float bt=split0+(split1-split0)*0.85; if(depth>bt){\n"
    "            float t=clamp((depth-bt)/(split1-bt),0.0,1.0);\n"
    "            s=mix(s,shadow_pcf(shadow_map_2,shadow_data.cascade_vp[2]*vec4(wpos,1.0),bias),t);}\n"
    "    }else{\n"
    "        s=shadow_pcf(shadow_map_2,shadow_data.cascade_vp[2]*vec4(wpos,1.0),bias);}\n"
    "    return s;}\n"
    "void main() {\n"
    "    vec4 base=texture(u_base_color,v_uv)*mat_pc.base_color_factor;\n"
    "    vec2 mr=texture(u_metallic_roughness,v_uv).bg;\n"
    "    float metallic=mr.x*mat_pc.metallic_factor; float roughness=max(mr.y*mat_pc.roughness_factor,0.04);\n"
    "    float ao=mix(1.0,texture(u_occlusion,v_uv).r,mat_pc.occlusion_strength);\n"
    "    vec3 emissive=texture(u_emissive,v_uv).rgb*mat_pc.emissive_factor;\n"
    "    vec3 nmap=texture(u_normal_map,v_uv).rgb*2.0-1.0;\n"
    "    mat3 TBN=mat3(normalize(v_tangent),normalize(v_bitangent),normalize(v_normal));\n"
    "    vec3 N=normalize(TBN*nmap);\n"
    "    if((frame.debug_flags&1u)!=0u){out_color=vec4(N*0.5+0.5,1.0);return;}\n"
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
    "        float shad=(l.cast_shadows!=0u&&l.type==0u)?compute_shadow(v_world_pos,N,l.direction):1.0;\n"
    "        Lo+=(kd*base.rgb/PI+spec)*l.color*l.intensity*NdotL*att*shad;\n"
    "    }\n"
    "    vec3 ambient=0.03*base.rgb*ao;\n"
    "    out_color=vec4(ambient+Lo+emissive,base.a);\n"
    "}\n";

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

static const char *FULLSCREEN_VERT =
    "#version 450\n"
    "void main() {\n"
    "    vec2 pos=vec2((gl_VertexIndex==2)?3.0:-1.0,(gl_VertexIndex==1)?3.0:-1.0);\n"
    "    gl_Position=vec4(pos,0.0,1.0);\n"
    "}\n";

/* ================================================================
   MATH HELPERS (CSM computation)
   ================================================================ */

static void compute_csm(const Qs_Camera *cam,
                         const Qs_LightGPU *lights, uint32_t light_count,
                         float shadow_matrices[QS_CSM_CASCADES][16],
                         float shadow_splits[QS_CSM_CASCADES+1])
{
    /* Find the first directional light; fall back to a default sun direction. */
    float light_dir[3] = { 0.4f, -1.0f, 0.3f };
    for (uint32_t i = 0; i < light_count; i++) {
        if (lights[i].type == (uint32_t)QS_LIGHT_DIRECTIONAL) {
            light_dir[0] = lights[i].direction[0];
            light_dir[1] = lights[i].direction[1];
            light_dir[2] = lights[i].direction[2];
            break;
        }
    }
    float ld = sqrtf(light_dir[0]*light_dir[0]+light_dir[1]*light_dir[1]+light_dir[2]*light_dir[2]);
    if (ld > 1e-6f) { light_dir[0]/=ld; light_dir[1]/=ld; light_dir[2]/=ld; }

    float near_p = cam->near_plane > 0.0f ? cam->near_plane : 0.1f;
    float far_p  = cam->far_plane  > 0.0f ? cam->far_plane  : 500.0f;

    /* Practical Split Scheme (blend of logarithmic and uniform) */
    shadow_splits[0] = near_p;
    for (int i = 1; i <= QS_CSM_CASCADES; i++) {
        float fi = (float)i / (float)QS_CSM_CASCADES;
        float lg = near_p * powf(far_p / near_p, fi);
        float ln = near_p + (far_p - near_p) * fi;
        shadow_splits[i] = 0.75f * lg + 0.25f * ln;
    }

    /* Light-space orthonormal basis */
    float l_fwd[3] = { light_dir[0], light_dir[1], light_dir[2] };
    float l_up[3]  = { 0.0f, 1.0f, 0.0f };
    if (fabsf(l_fwd[1]) > 0.99f) { l_up[0] = 1.0f; l_up[1] = 0.0f; l_up[2] = 0.0f; }
    float l_right[3] = {
        l_fwd[1]*l_up[2]-l_fwd[2]*l_up[1],
        l_fwd[2]*l_up[0]-l_fwd[0]*l_up[2],
        l_fwd[0]*l_up[1]-l_fwd[1]*l_up[0]
    };
    float ll = sqrtf(l_right[0]*l_right[0]+l_right[1]*l_right[1]+l_right[2]*l_right[2]);
    if (ll > 1e-6f) { l_right[0]/=ll; l_right[1]/=ll; l_right[2]/=ll; }
    float l_up2[3] = {
        l_right[1]*l_fwd[2]-l_right[2]*l_fwd[1],
        l_right[2]*l_fwd[0]-l_right[0]*l_fwd[2],
        l_right[0]*l_fwd[1]-l_right[1]*l_fwd[0]
    };

    for (int c = 0; c < QS_CSM_CASCADES; c++) {
        float near_c = shadow_splits[c], far_c = shadow_splits[c+1];

        /* Shadow map XY radius: proportional to cascade depth slice.
           Anchored on the camera's orbit target so the tightest coverage always
           wraps the visible scene even when the camera is looking steeply downward
           (placing the view-ray midpoint underground). */
        float radius = (far_c - near_c) * 0.6f + 2.0f;

        float cx = cam->target[0];
        float cy = cam->target[1];
        float cz = cam->target[2];

        /* Snap center to texel grid in light space (eliminates shadow shimmer) */
        float texel = 2.0f * radius / (float)QS_SHADOW_MAP_SIZE;
        float lc_r  = cx*l_right[0] + cy*l_right[1] + cz*l_right[2];
        float lc_u  = cx*l_up2[0]   + cy*l_up2[1]   + cz*l_up2[2];
        float lc_d  = cx*l_fwd[0]   + cy*l_fwd[1]   + cz*l_fwd[2];
        lc_r = roundf(lc_r / texel) * texel;
        lc_u = roundf(lc_u / texel) * texel;
        cx = lc_r*l_right[0] + lc_u*l_up2[0] + lc_d*l_fwd[0];
        cy = lc_r*l_right[1] + lc_u*l_up2[1] + lc_d*l_fwd[1];
        cz = lc_r*l_right[2] + lc_u*l_up2[2] + lc_d*l_fwd[2];

        /* Pull light eye back so scene geometry sits well within the depth range. */
        float pull = radius;
        float lx = cx - light_dir[0]*pull;
        float ly = cy - light_dir[1]*pull;
        float lz = cz - light_dir[2]*pull;

        /* Standard GL right-handed view matrix: Z row = -l_fwd.
           Objects land at view_z = -pull (negative), consistent with ortho below. */
        float lv[16]; memset(lv, 0, 64);
        lv[0]= l_right[0]; lv[4]= l_right[1]; lv[8]= l_right[2];  lv[12]=-(l_right[0]*lx+l_right[1]*ly+l_right[2]*lz);
        lv[1]= l_up2[0];   lv[5]= l_up2[1];   lv[9]= l_up2[2];    lv[13]=-(l_up2[0]*lx+l_up2[1]*ly+l_up2[2]*lz);
        lv[2]=-l_fwd[0];   lv[6]=-l_fwd[1];   lv[10]=-l_fwd[2];   lv[14]= (l_fwd[0]*lx+l_fwd[1]*ly+l_fwd[2]*lz);
        lv[3]=0; lv[7]=0; lv[11]=0; lv[15]=1.0f;
        float lp[16];
        /* Symmetric Z range [-5r, +5r] keeps scene geometry (view_z = -pull = -r)
           centred in the depth range at NDC z = 0.2, identical to the original. */
        qs_m4_ortho_lrtbnf(lp, -radius, radius, -radius, radius, -radius*5.0f, radius*5.0f);
        qs_m4_mul(lp, lv, shadow_matrices[c]);
    }
}

/* ================================================================
   SHARED PASS RESOURCE CREATION / DESTRUCTION
   ================================================================ */

static bool create_samplers(Qs_GpuContext *gpu, PbrPassResources *ps)
{
    ps->linear_sampler = qs_gpu_create_sampler(gpu, &(Qs_GpuSamplerDesc){
        .min_filter=QS_GPU_FILTER_LINEAR,.mag_filter=QS_GPU_FILTER_LINEAR,
        .wrap_u=QS_GPU_WRAP_CLAMP_TO_EDGE,.wrap_v=QS_GPU_WRAP_CLAMP_TO_EDGE,.mip_levels=1});
    ps->point_sampler  = qs_gpu_create_sampler(gpu, &(Qs_GpuSamplerDesc){
        .min_filter=QS_GPU_FILTER_NEAREST,.mag_filter=QS_GPU_FILTER_NEAREST,
        .wrap_u=QS_GPU_WRAP_CLAMP_TO_EDGE,.wrap_v=QS_GPU_WRAP_CLAMP_TO_EDGE,.mip_levels=1});
    ps->shadow_sampler = qs_gpu_create_sampler(gpu, &(Qs_GpuSamplerDesc){
        .min_filter=QS_GPU_FILTER_LINEAR,.mag_filter=QS_GPU_FILTER_LINEAR,
        .wrap_u=QS_GPU_WRAP_CLAMP_TO_EDGE,.wrap_v=QS_GPU_WRAP_CLAMP_TO_EDGE,.mip_levels=1});
    return ps->linear_sampler && ps->point_sampler && ps->shadow_sampler;
}

static bool create_frame_set_layout(Qs_GpuContext *gpu, PbrPassResources *ps)
{
    Qs_GpuDescriptorBinding b[6] = {
        {0,QS_GPU_DESCRIPTOR_UNIFORM_BUFFER,        1,QS_GPU_SHADER_VERTEX|QS_GPU_SHADER_FRAGMENT},
        {1,QS_GPU_DESCRIPTOR_UNIFORM_BUFFER,        1,QS_GPU_SHADER_FRAGMENT},
        {2,QS_GPU_DESCRIPTOR_UNIFORM_BUFFER,        1,QS_GPU_SHADER_VERTEX|QS_GPU_SHADER_FRAGMENT},
        {3,QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,1,QS_GPU_SHADER_FRAGMENT},
        {4,QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,1,QS_GPU_SHADER_FRAGMENT},
        {5,QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,1,QS_GPU_SHADER_FRAGMENT},
    };
    ps->frame_set_layout = qs_gpu_create_descriptor_set_layout(gpu, b, 6);
    return ps->frame_set_layout != NULL;
}

static bool create_single_sampler_layout(Qs_GpuContext *gpu,
                                          Qs_GpuDescriptorSetLayout **out, uint32_t count)
{
    Qs_GpuDescriptorBinding b[2] = {
        {0,QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,1,QS_GPU_SHADER_FRAGMENT},
        {1,QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,1,QS_GPU_SHADER_FRAGMENT},
    };
    *out = qs_gpu_create_descriptor_set_layout(gpu, b, count);
    return *out != NULL;
}

static bool create_shadow_pipeline(Qs_GpuContext *gpu, PbrPassResources *ps)
{
    Qs_GpuShader *vs=qs_gpu_compile_shader(gpu,SHADOW_VERT,QS_GPU_SHADER_VERTEX);
    Qs_GpuShader *fs=qs_gpu_compile_shader(gpu,SHADOW_FRAG,QS_GPU_SHADER_FRAGMENT);
    if(!vs||!fs){if(vs)qs_gpu_destroy_shader(gpu,vs);if(fs)qs_gpu_destroy_shader(gpu,fs);return false;}
    Qs_GpuPushConstantRange pc={QS_GPU_SHADER_VERTEX,0,80};
    Qs_GpuDescriptorSetLayout *sets[]={ps->frame_set_layout};
    ps->shadow_layout=qs_gpu_create_pipeline_layout(gpu,&(Qs_GpuPipelineLayoutDesc){sets,1,&pc,1});
    Qs_GpuVertexAttribute attr={0,QS_GPU_VERTEX_FORMAT_FLOAT3,offsetof(Qs_Vertex,position)};
    Qs_GpuVertexBinding vb={0,sizeof(Qs_Vertex),&attr,1};
    ps->shadow_pipeline=qs_gpu_create_graphics_pipeline(gpu,&(Qs_GpuGraphicsPipelineDesc){
        ps->shadow_layout,vs,fs,&vb,1,
        QS_GPU_TOPOLOGY_TRIANGLES,QS_GPU_CULL_FRONT,true,true,
        QS_GPU_FORMAT_NONE,QS_GPU_FORMAT_D32_SFLOAT});
    qs_gpu_destroy_shader(gpu,vs);qs_gpu_destroy_shader(gpu,fs);
    return ps->shadow_pipeline!=NULL;
}

static bool create_forward_pipeline(Qs_GpuContext *gpu, PbrPassResources *ps)
{
    Qs_GpuShader *vs=qs_gpu_compile_shader(gpu,FORWARD_VERT,QS_GPU_SHADER_VERTEX);
    Qs_GpuShader *fs=qs_gpu_compile_shader(gpu,FORWARD_FRAG,QS_GPU_SHADER_FRAGMENT);
    if(!vs||!fs){if(vs)qs_gpu_destroy_shader(gpu,vs);if(fs)qs_gpu_destroy_shader(gpu,fs);return false;}
    Qs_GpuDescriptorSetLayout *mat_layout=qs_material_set_layout();
    if(!mat_layout){qs_gpu_destroy_shader(gpu,vs);qs_gpu_destroy_shader(gpu,fs);
        QS_LOG_ERROR("PBR Renderer: material set layout unavailable");return false;}
    Qs_GpuPushConstantRange pcs[2]={{QS_GPU_SHADER_VERTEX,0,64},{QS_GPU_SHADER_FRAGMENT,64,48}};
    Qs_GpuDescriptorSetLayout *sets[]={ps->frame_set_layout,mat_layout};
    ps->forward_layout=qs_gpu_create_pipeline_layout(gpu,&(Qs_GpuPipelineLayoutDesc){sets,2,pcs,2});
    Qs_GpuVertexAttribute attrs[4]={
        {0,QS_GPU_VERTEX_FORMAT_FLOAT3,offsetof(Qs_Vertex,position)},
        {1,QS_GPU_VERTEX_FORMAT_FLOAT3,offsetof(Qs_Vertex,normal)},
        {2,QS_GPU_VERTEX_FORMAT_FLOAT4,offsetof(Qs_Vertex,tangent)},
        {3,QS_GPU_VERTEX_FORMAT_FLOAT2,offsetof(Qs_Vertex,uv)},
    };
    Qs_GpuVertexBinding vb={0,sizeof(Qs_Vertex),attrs,4};
    ps->forward_pipeline=qs_gpu_create_graphics_pipeline(gpu,&(Qs_GpuGraphicsPipelineDesc){
        ps->forward_layout,vs,fs,&vb,1,
        QS_GPU_TOPOLOGY_TRIANGLES,QS_GPU_CULL_BACK,true,true,
        QS_GPU_FORMAT_RGBA16_SFLOAT,QS_GPU_FORMAT_DEPTH_AUTO,.wireframe=false});
    ps->forward_wireframe_pipeline=qs_gpu_create_graphics_pipeline(gpu,&(Qs_GpuGraphicsPipelineDesc){
        ps->forward_layout,vs,fs,&vb,1,
        QS_GPU_TOPOLOGY_TRIANGLES,QS_GPU_CULL_NONE,true,true,
        QS_GPU_FORMAT_RGBA16_SFLOAT,QS_GPU_FORMAT_DEPTH_AUTO,.wireframe=true});
    qs_gpu_destroy_shader(gpu,vs);qs_gpu_destroy_shader(gpu,fs);
    return ps->forward_pipeline!=NULL && ps->forward_wireframe_pipeline!=NULL;
}

static bool create_bloom_pipelines(Qs_GpuContext *gpu, PbrPassResources *ps)
{
    if(!create_single_sampler_layout(gpu,&ps->bloom_set_layout,1)) return false;
    Qs_GpuPushConstantRange pc={QS_GPU_SHADER_FRAGMENT,0,16};
    Qs_GpuDescriptorSetLayout *sets[]={ps->bloom_set_layout};
    ps->bloom_layout=qs_gpu_create_pipeline_layout(gpu,&(Qs_GpuPipelineLayoutDesc){sets,1,&pc,1});
    if(!ps->bloom_layout) return false;
    Qs_GpuShader *fv=qs_gpu_compile_shader(gpu,FULLSCREEN_VERT,QS_GPU_SHADER_VERTEX);
    Qs_GpuShader *fd=qs_gpu_compile_shader(gpu,BLOOM_DOWN_FRAG,QS_GPU_SHADER_FRAGMENT);
    Qs_GpuShader *fu=qs_gpu_compile_shader(gpu,BLOOM_UP_FRAG,  QS_GPU_SHADER_FRAGMENT);
    if(!fv||!fd||!fu){if(fv)qs_gpu_destroy_shader(gpu,fv);if(fd)qs_gpu_destroy_shader(gpu,fd);if(fu)qs_gpu_destroy_shader(gpu,fu);return false;}
    Qs_GpuGraphicsPipelineDesc pd={ps->bloom_layout,fv,fd,NULL,0,
        QS_GPU_TOPOLOGY_TRIANGLES,QS_GPU_CULL_NONE,false,false,QS_GPU_FORMAT_RGBA16_SFLOAT,QS_GPU_FORMAT_DEPTH_AUTO};
    ps->bloom_down_pipeline=qs_gpu_create_graphics_pipeline(gpu,&pd);
    pd.fragment_shader=fu;
    ps->bloom_up_pipeline=qs_gpu_create_graphics_pipeline(gpu,&pd);
    qs_gpu_destroy_shader(gpu,fv);qs_gpu_destroy_shader(gpu,fd);qs_gpu_destroy_shader(gpu,fu);
    return ps->bloom_down_pipeline&&ps->bloom_up_pipeline;
}

static bool create_composite_pipeline(Qs_GpuContext *gpu, PbrPassResources *ps,
                                        Qs_GpuImageFormat swapchain_fmt)
{
    if(!create_single_sampler_layout(gpu,&ps->composite_set_layout,2)) return false;
    Qs_GpuPushConstantRange pc={QS_GPU_SHADER_FRAGMENT,0,16};
    Qs_GpuDescriptorSetLayout *sets[]={ps->composite_set_layout};
    ps->composite_layout=qs_gpu_create_pipeline_layout(gpu,&(Qs_GpuPipelineLayoutDesc){sets,1,&pc,1});
    if(!ps->composite_layout) return false;
    Qs_GpuShader *fv=qs_gpu_compile_shader(gpu,FULLSCREEN_VERT,QS_GPU_SHADER_VERTEX);
    Qs_GpuShader *fc=qs_gpu_compile_shader(gpu,COMPOSITE_FRAG, QS_GPU_SHADER_FRAGMENT);
    if(!fv||!fc){if(fv)qs_gpu_destroy_shader(gpu,fv);if(fc)qs_gpu_destroy_shader(gpu,fc);return false;}
    ps->composite_pipeline=qs_gpu_create_graphics_pipeline(gpu,&(Qs_GpuGraphicsPipelineDesc){
        ps->composite_layout,fv,fc,NULL,0,
        QS_GPU_TOPOLOGY_TRIANGLES,QS_GPU_CULL_NONE,false,false,swapchain_fmt,QS_GPU_FORMAT_DEPTH_AUTO});
    qs_gpu_destroy_shader(gpu,fv);qs_gpu_destroy_shader(gpu,fc);
    return ps->composite_pipeline!=NULL;
}

static bool pbr_pass_resources_init(Qs_GpuContext *gpu, PbrPassResources *ps)
{
    if (ps->ok) return true;
    if (!create_samplers(gpu,ps))                               { QS_LOG_ERROR("PBR Renderer: samplers failed");          goto fail; }
    if (!create_frame_set_layout(gpu,ps))                       { QS_LOG_ERROR("PBR Renderer: frame set layout failed");  goto fail; }
    if (!create_shadow_pipeline(gpu,ps))                        { QS_LOG_ERROR("PBR Renderer: shadow pipeline failed");   goto fail; }
    if (!create_forward_pipeline(gpu,ps))                       { QS_LOG_ERROR("PBR Renderer: forward pipeline failed");  goto fail; }
    if (!create_bloom_pipelines(gpu,ps))                        { QS_LOG_ERROR("PBR Renderer: bloom pipelines failed");   goto fail; }
    if (!create_composite_pipeline(gpu,ps,QS_GPU_FORMAT_BGRA8_UNORM))
                                                                { QS_LOG_ERROR("PBR Renderer: composite pipeline failed");goto fail; }
    ps->ok = true;
    QS_LOG_INFO("PBR Renderer: shared pass resources ready");
    return true;
fail:
    pbr_pass_resources_shutdown(gpu, ps);
    return false;
}

void pbr_pass_resources_shutdown(Qs_GpuContext *gpu, PbrPassResources *ps)
{
    qs_gpu_destroy_pipeline(gpu, ps->shadow_pipeline);
    qs_gpu_destroy_pipeline_layout(gpu, ps->shadow_layout);
    qs_gpu_destroy_pipeline(gpu, ps->forward_pipeline);
    qs_gpu_destroy_pipeline(gpu, ps->forward_wireframe_pipeline);
    qs_gpu_destroy_pipeline_layout(gpu, ps->forward_layout);
    qs_gpu_destroy_descriptor_set_layout(gpu, ps->frame_set_layout);
    qs_gpu_destroy_pipeline(gpu, ps->bloom_down_pipeline);
    qs_gpu_destroy_pipeline(gpu, ps->bloom_up_pipeline);
    qs_gpu_destroy_pipeline_layout(gpu, ps->bloom_layout);
    qs_gpu_destroy_descriptor_set_layout(gpu, ps->bloom_set_layout);
    qs_gpu_destroy_pipeline(gpu, ps->composite_pipeline);
    qs_gpu_destroy_pipeline_layout(gpu, ps->composite_layout);
    qs_gpu_destroy_descriptor_set_layout(gpu, ps->composite_set_layout);
    qs_gpu_destroy_sampler(gpu, ps->linear_sampler);
    qs_gpu_destroy_sampler(gpu, ps->point_sampler);
    qs_gpu_destroy_sampler(gpu, ps->shadow_sampler);
    memset(ps, 0, sizeof(*ps));
}

/* ================================================================
   DESCRIPTOR POOL + SET ALLOCATION
   ================================================================ */

static bool fwd_alloc_descriptors(PbrRenderer *r, Qs_GpuContext *gpu, PbrPassResources *ps)
{
    Qs_GpuDescriptorPoolSize sizes[] = {
        {QS_GPU_DESCRIPTOR_UNIFORM_BUFFER,         12},
        {QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER, 12},
    };
    r->desc_pool = qs_gpu_create_descriptor_pool(gpu,
                   &(Qs_GpuDescriptorPoolDesc){sizes,2,8});
    if (!r->desc_pool) return false;

    r->frame_desc_set     = qs_gpu_alloc_descriptor_set(gpu, r->desc_pool, ps->frame_set_layout);
    r->composite_desc_set = qs_gpu_alloc_descriptor_set(gpu, r->desc_pool, ps->composite_set_layout);
    r->bloom_desc_sets[0] = qs_gpu_alloc_descriptor_set(gpu, r->desc_pool, ps->bloom_set_layout);
    r->bloom_desc_sets[1] = qs_gpu_alloc_descriptor_set(gpu, r->desc_pool, ps->bloom_set_layout);
    return r->frame_desc_set && r->composite_desc_set &&
           r->bloom_desc_sets[0] && r->bloom_desc_sets[1];
}

/* ================================================================
   RENDER NODE CALLBACKS
   ================================================================ */

/* Pass 0: CSM shadow maps */
static void shadow_pass_execute(const Qs_RenderContext *ctx, void *user_data)
{
    PbrRenderer      *r  = user_data;
    PbrPassResources *ps = pbr_renderer_pass_resources();
    if (!ps || !ps->ok || !r->ok) return;
    if (ctx->renderable_count == 0) return;

    /* Update plugin-owned ShadowUBO (CSM matrices) */
    Qs_Camera *cam = qs_renderer_camera(ctx->renderer);
    compute_csm(cam, ctx->lights, ctx->light_count, r->shadow_matrices, r->shadow_splits);

    ShadowUBO *subo = qs_gpu_map_buffer(r->gpu, r->shadow_ubo);
    if (subo) {
        for (int c=0; c<QS_CSM_CASCADES; c++) {
            memcpy(subo->cascade_vp[c], r->shadow_matrices[c], 64);
            subo->cascade_splits[c] = r->shadow_splits[c+1];
        }
        qs_gpu_unmap_buffer(r->gpu, r->shadow_ubo);
    }

    for (int cascade=0; cascade<QS_CSM_CASCADES; cascade++) {
        Qs_GpuImageView *sv  = qs_attachment_view(r->shadow_att[cascade]);
        Qs_GpuImage     *img = qs_attachment_image(r->shadow_att[cascade]);
        if (!sv || !img) continue;

        qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
            .image=img,.old_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
            .new_layout=QS_GPU_IMAGE_LAYOUT_DEPTH_ATTACHMENT,
            .aspect=QS_GPU_IMAGE_ASPECT_DEPTH,.base_mip=0,.mip_count=1});

        qs_cmd_begin_rendering(ctx->cmd, &(Qs_GpuRenderTarget){
            .color=NULL,.depth=sv,.clear_depth=1.0f,
            .width=QS_SHADOW_MAP_SIZE,.height=QS_SHADOW_MAP_SIZE});
        qs_cmd_set_viewport(ctx->cmd, QS_SHADOW_MAP_SIZE, QS_SHADOW_MAP_SIZE);
        qs_cmd_bind_pipeline(ctx->cmd, ps->shadow_pipeline);
        qs_cmd_bind_descriptor_set(ctx->cmd, ps->shadow_layout, 0, r->frame_desc_set);

        for (uint32_t ri=0; ri<ctx->renderable_count; ri++) {
            const Qs_Renderable *ren = &ctx->renderables[ri];
            if (!ren->cast_shadows || !ren->vertex_buffer) continue;
            typedef struct { float model[16]; int32_t cascade_idx; int32_t _p[3]; } ShadowPC;
            ShadowPC spc; memcpy(spc.model, ren->transform, 64);
            spc.cascade_idx=cascade; spc._p[0]=spc._p[1]=spc._p[2]=0;
            qs_cmd_push_constants(ctx->cmd, ps->shadow_layout,
                                  QS_GPU_SHADER_VERTEX, 0, sizeof(ShadowPC), &spc);
            qs_cmd_bind_vertex_buffer(ctx->cmd, 0, ren->vertex_buffer, 0);
            if (ren->index_buffer)
                qs_cmd_bind_index_buffer(ctx->cmd, ren->index_buffer, ren->index_16bit);
            if (ren->index_count > 0)
                qs_cmd_draw_indexed(ctx->cmd, ren->index_count, 0, 0);
            else
                qs_cmd_draw(ctx->cmd, ren->vertex_count, 0);
        }
        qs_cmd_end_rendering(ctx->cmd);

        qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
            .image=img,.old_layout=QS_GPU_IMAGE_LAYOUT_DEPTH_ATTACHMENT,
            .new_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
            .aspect=QS_GPU_IMAGE_ASPECT_DEPTH,.base_mip=0,.mip_count=1});
    }
}

/* Pass 1: Forward lit (HDR target) */
static void forward_pass_execute(const Qs_RenderContext *ctx, void *user_data)
{
    PbrRenderer      *r  = user_data;
    PbrPassResources *ps = pbr_renderer_pass_resources();
    if (!ps || !ps->ok || !r->ok) return;

    Qs_GpuImageView *hdr_view  = qs_attachment_view(r->hdr_att);
    Qs_GpuImage     *hdr_img   = qs_attachment_image(r->hdr_att);
    Qs_GpuImageView *depth_view = qs_renderer_depth_view(ctx->renderer);
    if (!hdr_view || !hdr_img) return;

    const float *cc = qs_renderer_clear_color(ctx->renderer);
    float clear[4] = { cc ? cc[0]:0.0f, cc ? cc[1]:0.0f,
                        cc ? cc[2]:0.0f, cc ? cc[3]:1.0f };

    qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
        .image=hdr_img,.old_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
        .new_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .aspect=QS_GPU_IMAGE_ASPECT_COLOR,.base_mip=0,.mip_count=1});

    qs_cmd_begin_rendering(ctx->cmd, &(Qs_GpuRenderTarget){
        .color=hdr_view,.depth=depth_view,
        .clear_color={clear[0],clear[1],clear[2],clear[3]},
        .clear_depth=1.0f,.width=ctx->width,.height=ctx->height});
    qs_cmd_set_viewport(ctx->cmd, ctx->width, ctx->height);
    Qs_GpuPipeline *fwd_pipeline = qs_renderer_wireframe(ctx->renderer)
        ? ps->forward_wireframe_pipeline : ps->forward_pipeline;
    qs_cmd_bind_pipeline(ctx->cmd, fwd_pipeline);
    qs_cmd_bind_descriptor_set(ctx->cmd, ps->forward_layout, 0, r->frame_desc_set);

    for (uint32_t ri=0; ri<ctx->renderable_count; ri++) {
        const Qs_Renderable *ren = &ctx->renderables[ri];
        if (!ren->material_set || !ren->vertex_buffer) continue;
        qs_cmd_push_constants(ctx->cmd, ps->forward_layout,
                              QS_GPU_SHADER_VERTEX, 0, 64, ren->transform);
        {
            FwdMatPC mpc;
            const Qs_PBRParams *p = &ren->material_params;
            memcpy(mpc.base_color_factor, p->base_color_factor, sizeof(mpc.base_color_factor));
            mpc.metallic_factor    = p->metallic_factor;
            mpc.roughness_factor   = p->roughness_factor;
            mpc.normal_scale       = p->normal_scale;
            mpc.occlusion_strength = p->occlusion_strength;
            memcpy(mpc.emissive_factor, p->emissive_factor, sizeof(mpc.emissive_factor));
            mpc.alpha_cutoff       = p->alpha_cutoff;
            qs_cmd_push_constants(ctx->cmd, ps->forward_layout,
                                  QS_GPU_SHADER_FRAGMENT, 64, sizeof(FwdMatPC), &mpc);
        }
        qs_cmd_bind_descriptor_set(ctx->cmd, ps->forward_layout, 1, ren->material_set);
        qs_cmd_bind_vertex_buffer(ctx->cmd, 0, ren->vertex_buffer, 0);
        if (ren->index_buffer)
            qs_cmd_bind_index_buffer(ctx->cmd, ren->index_buffer, ren->index_16bit);
        if (ren->index_count > 0)
            qs_cmd_draw_indexed(ctx->cmd, ren->index_count, 0, 0);
        else
            qs_cmd_draw(ctx->cmd, ren->vertex_count, 0);
    }
    qs_cmd_end_rendering(ctx->cmd);

    qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
        .image=hdr_img,.old_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .new_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
        .aspect=QS_GPU_IMAGE_ASPECT_COLOR,.base_mip=0,.mip_count=1});
}

/* Pass 2: Bloom */
static void bloom_pass_execute(const Qs_RenderContext *ctx, void *user_data)
{
    PbrRenderer      *r  = user_data;
    PbrPassResources *ps = pbr_renderer_pass_resources();
    if (!ps || !ps->ok || !r->ok) return;

    uint32_t bw = (ctx->width+1)/2, bh = (ctx->height+1)/2;
    typedef struct { float inv_w, inv_h, _p[2]; } BloomPC;

    Qs_GpuImage *bimg0 = qs_attachment_image(r->bloom_att[0]);
    Qs_GpuImage *bimg1 = qs_attachment_image(r->bloom_att[1]);
    if (!bimg0 || !bimg1) return;

    /* Downsample: HDR -> bloom[0] */
    qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
        .image=bimg0,.old_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
        .new_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .aspect=QS_GPU_IMAGE_ASPECT_COLOR,.base_mip=0,.mip_count=1});
    qs_cmd_begin_rendering(ctx->cmd, &(Qs_GpuRenderTarget){
        .color=qs_attachment_view(r->bloom_att[0]),.depth=NULL,
        .clear_color={0,0,0,0},.width=bw,.height=bh});
    qs_cmd_set_viewport(ctx->cmd,bw,bh);
    qs_cmd_bind_pipeline(ctx->cmd,ps->bloom_down_pipeline);
    qs_cmd_bind_descriptor_set(ctx->cmd,ps->bloom_layout,0,r->bloom_desc_sets[0]);
    /* inv_src_size maps destination pixel position to source UV:
       use 1/bw, 1/bh so gl_FragCoord.xy * inv_src_size covers [0,1]
       across the full-res HDR source. */
    BloomPC bpc={1.0f/(float)bw,1.0f/(float)bh,{0,0}};
    qs_cmd_push_constants(ctx->cmd,ps->bloom_layout,QS_GPU_SHADER_FRAGMENT,0,16,&bpc);
    qs_cmd_draw(ctx->cmd,3,0);
    qs_cmd_end_rendering(ctx->cmd);
    qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
        .image=bimg0,.old_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .new_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
        .aspect=QS_GPU_IMAGE_ASPECT_COLOR,.base_mip=0,.mip_count=1});

    /* Upsample: bloom[0] -> bloom[1] */
    qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
        .image=bimg1,.old_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
        .new_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .aspect=QS_GPU_IMAGE_ASPECT_COLOR,.base_mip=0,.mip_count=1});
    qs_cmd_begin_rendering(ctx->cmd, &(Qs_GpuRenderTarget){
        .color=qs_attachment_view(r->bloom_att[1]),.depth=NULL,
        .clear_color={0,0,0,0},.width=bw,.height=bh});
    qs_cmd_set_viewport(ctx->cmd,bw,bh);
    qs_cmd_bind_pipeline(ctx->cmd,ps->bloom_up_pipeline);
    qs_cmd_bind_descriptor_set(ctx->cmd,ps->bloom_layout,0,r->bloom_desc_sets[1]);
    BloomPC upc={1.0f/(float)bw,1.0f/(float)bh,{0,0}};
    qs_cmd_push_constants(ctx->cmd,ps->bloom_layout,QS_GPU_SHADER_FRAGMENT,0,16,&upc);
    qs_cmd_draw(ctx->cmd,3,0);
    qs_cmd_end_rendering(ctx->cmd);
    qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
        .image=bimg1,.old_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .new_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
        .aspect=QS_GPU_IMAGE_ASPECT_COLOR,.base_mip=0,.mip_count=1});
}

/* Pass 3: Composite (ACES tonemap + vignette -> swapchain) */
static void composite_pass_execute(const Qs_RenderContext *ctx, void *user_data)
{
    PbrRenderer      *r  = user_data;
    PbrPassResources *ps = pbr_renderer_pass_resources();
    if (!ps || !ps->ok || !r->ok) return;
    if (!ctx->swapchain_view || ctx->swapchain_width==0 || ctx->swapchain_height==0) return;

    qs_cmd_begin_rendering(ctx->cmd, &(Qs_GpuRenderTarget){
        .color=ctx->swapchain_view,.depth=NULL,
        .clear_color={0,0,0,0},
        .width=ctx->swapchain_width,.height=ctx->swapchain_height});
    qs_cmd_set_viewport(ctx->cmd, ctx->swapchain_width, ctx->swapchain_height);
    qs_cmd_bind_pipeline(ctx->cmd, ps->composite_pipeline);
    qs_cmd_bind_descriptor_set(ctx->cmd, ps->composite_layout, 0, r->composite_desc_set);
    typedef struct { float inv_w, inv_h, bloom_str, vignette_str; } CompositePC;
    CompositePC cpc={1.0f/(float)ctx->swapchain_width,1.0f/(float)ctx->swapchain_height,
                     g_pp_settings.bloom_strength, g_pp_settings.vignette_strength};
    qs_cmd_push_constants(ctx->cmd,ps->composite_layout,QS_GPU_SHADER_FRAGMENT,0,16,&cpc);
    qs_cmd_draw(ctx->cmd,3,0);
    qs_cmd_end_rendering(ctx->cmd);
}

/* ================================================================
   PUBLIC ATTACH / DETACH / RESIZE
   ================================================================ */

void pbr_forward_attach(Qs_Engine *engine, PbrRenderer *r, Qs_Renderer *handle)
{
    if (!r || !handle) return;
    r->engine          = engine;
    r->engine_renderer = handle;

    Qs_GpuContext   *gpu = r->gpu;
    PbrPassResources *ps  = pbr_renderer_pass_resources();
    if (!pbr_pass_resources_init(gpu, ps)) {
        QS_LOG_ERROR("PBR Renderer: pass resources init failed"); return;
    }

    /* --- Declare engine-managed attachments --- */

    r->hdr_att = qs_renderer_add_attachment(handle, &(Qs_RenderAttachmentDesc){
        .name="hdr",.format=QS_GPU_FORMAT_RGBA16_SFLOAT,.usage=QS_ATTACHMENT_COLOR,
        .width_scale=1.0f,.height_scale=1.0f});

    char sname[32];
    for (int i=0; i<QS_CSM_CASCADES; i++) {
        snprintf(sname, sizeof(sname), "shadow_%d", i);
        r->shadow_att[i] = qs_renderer_add_attachment(handle, &(Qs_RenderAttachmentDesc){
            .name=sname,.format=QS_GPU_FORMAT_D32_SFLOAT,.usage=QS_ATTACHMENT_DEPTH,
            .fixed_width=QS_SHADOW_MAP_SIZE,.fixed_height=QS_SHADOW_MAP_SIZE});
    }
    for (int i=0; i<2; i++) {
        char bname[32]; snprintf(bname, sizeof(bname), "bloom_%d", i);
        r->bloom_att[i] = qs_renderer_add_attachment(handle, &(Qs_RenderAttachmentDesc){
            .name=bname,.format=QS_GPU_FORMAT_RGBA16_SFLOAT,.usage=QS_ATTACHMENT_COLOR,
            .width_scale=0.5f,.height_scale=0.5f});
    }

    /* --- Create shadow sample views (from fixed-size shadow images) --- */
    for (int i=0; i<QS_CSM_CASCADES; i++) {
        if (!r->shadow_att[i]) continue;
        r->shadow_sample_views[i] = qs_gpu_create_image_view_for(
            gpu, qs_attachment_image(r->shadow_att[i]), QS_GPU_IMAGE_ASPECT_DEPTH);
    }

    /* --- Plugin-owned shadow UBO --- */
    r->shadow_ubo = qs_gpu_create_buffer(gpu, &(Qs_GpuBufferDesc){
        .size=sizeof(ShadowUBO),.usage=QS_GPU_BUFFER_UNIFORM,
        .memory=QS_GPU_MEMORY_HOST_VISIBLE});
    if (!r->shadow_ubo) {
        QS_LOG_ERROR("PBR Renderer: shadow UBO creation failed");
        pbr_forward_detach(r); return;
    }

    /* --- Allocate descriptor pool and sets --- */
    if (!fwd_alloc_descriptors(r, gpu, ps)) {
        QS_LOG_ERROR("PBR Renderer: descriptor alloc failed");
        pbr_forward_detach(r); return;
    }

    /* --- Write static descriptors (UBO bindings and shadow map samplers).
           These never change after creation so we write them once here. --- */
    Qs_GpuBuffer *frame_ubo  = qs_renderer_get_frame_ubo(handle);
    Qs_GpuBuffer *lights_ubo = qs_renderer_get_lights_ubo(handle);
    qs_gpu_write_buffer_descriptor(gpu, r->frame_desc_set, 0, frame_ubo,  0, 0);
    qs_gpu_write_buffer_descriptor(gpu, r->frame_desc_set, 1, lights_ubo, 0, 0);
    qs_gpu_write_buffer_descriptor(gpu, r->frame_desc_set, 2, r->shadow_ubo, 0, 0);
    for (int i=0; i<QS_CSM_CASCADES; i++) {
        if (r->shadow_sample_views[i])
            qs_gpu_write_image_descriptor(gpu, r->frame_desc_set, 3+(uint32_t)i,
                                           ps->shadow_sampler, r->shadow_sample_views[i]);
    }
    /* composite + bloom descriptors are written in pbr_forward_on_resize */

    /* --- Add render nodes via engine API --- */
    r->shadow_node = qs_renderer_add_node(handle, &(Qs_RenderNodeDesc){
        .name="shadow_csm",.priority=0,.execute=shadow_pass_execute,.user_data=r});
    r->forward_node = qs_renderer_add_node(handle, &(Qs_RenderNodeDesc){
        .name="forward_pbr",.priority=100,.execute=forward_pass_execute,.user_data=r});
    r->bloom_node = qs_renderer_add_node(handle, &(Qs_RenderNodeDesc){
        .name="bloom",.priority=200,.execute=bloom_pass_execute,.user_data=r});
    r->composite_node = qs_renderer_add_node(handle, &(Qs_RenderNodeDesc){
        .name="composite",.priority=300,.execute=composite_pass_execute,.user_data=r});

    /* r->ok stays false until pbr_forward_on_resize is called */
    QS_LOG_INFO("PBR Renderer: Forward+ renderer attached to '%s'", r->name);
}

void pbr_forward_detach(PbrRenderer *r)
{
    if (!r) return;
    Qs_GpuContext *gpu    = r->gpu;
    Qs_Renderer   *handle = r->engine_renderer;

    /* Remove render nodes (engine removes from its sorted list) */
    if (handle) {
        if (r->shadow_node)    qs_renderer_remove_node(handle, r->shadow_node);
        if (r->forward_node)   qs_renderer_remove_node(handle, r->forward_node);
        if (r->bloom_node)     qs_renderer_remove_node(handle, r->bloom_node);
        if (r->composite_node) qs_renderer_remove_node(handle, r->composite_node);
    }

    /* Destroy plugin-owned shadow sample views */
    for (int i=0; i<QS_CSM_CASCADES; i++) {
        if (r->shadow_sample_views[i]) {
            qs_gpu_destroy_image_view(gpu, r->shadow_sample_views[i]);
            r->shadow_sample_views[i] = NULL;
        }
    }

    /* Destroy plugin-owned UBO and descriptor pool */
    if (r->shadow_ubo) { qs_gpu_destroy_buffer(gpu, r->shadow_ubo); r->shadow_ubo = NULL; }
    if (r->desc_pool)  { qs_gpu_destroy_descriptor_pool(gpu, r->desc_pool); r->desc_pool = NULL; }

    r->frame_desc_set = r->composite_desc_set = NULL;
    r->bloom_desc_sets[0] = r->bloom_desc_sets[1] = NULL;
    r->shadow_node = r->forward_node = r->bloom_node = r->composite_node = NULL;
    r->hdr_att = NULL;
    for (int i=0;i<QS_CSM_CASCADES;i++) r->shadow_att[i] = NULL;
    for (int i=0;i<2;i++) r->bloom_att[i] = NULL;
    r->ok = false;

    QS_LOG_INFO("PBR Renderer: detached from '%s'", r->name);

    /* Shared pass resources are cleaned up in pbr_render_shutdown. */
}

void pbr_forward_on_resize(PbrRenderer *r, uint32_t w, uint32_t h)
{
    (void)w; (void)h;
    PbrPassResources *ps = pbr_renderer_pass_resources();
    if (!r || !ps || !ps->ok) return;

    Qs_GpuContext *gpu = r->gpu;

    /* Get current attachment views (engine just resized them) */
    Qs_GpuImageView *hdr_view    = qs_attachment_view(r->hdr_att);
    Qs_GpuImageView *bloom0_view = qs_attachment_view(r->bloom_att[0]);
    Qs_GpuImageView *bloom1_view = qs_attachment_view(r->bloom_att[1]);

    if (!hdr_view || !bloom0_view || !bloom1_view) {
        QS_LOG_ERROR("PBR Renderer: on_resize — attachment views unavailable");
        r->ok = false;
        return;
    }

    /* Re-write composite descriptor set */
    qs_gpu_write_image_descriptor(gpu, r->composite_desc_set, 0,
                                   ps->linear_sampler, hdr_view);
    qs_gpu_write_image_descriptor(gpu, r->composite_desc_set, 1,
                                   ps->linear_sampler, bloom1_view);

    /* Re-write bloom descriptor sets */
    qs_gpu_write_image_descriptor(gpu, r->bloom_desc_sets[0], 0,
                                   ps->linear_sampler, hdr_view);
    qs_gpu_write_image_descriptor(gpu, r->bloom_desc_sets[1], 0,
                                   ps->linear_sampler, bloom0_view);

    r->ok = true;
    QS_LOG_INFO("PBR Renderer: on_resize %ux%u — descriptors updated", w, h);
}
