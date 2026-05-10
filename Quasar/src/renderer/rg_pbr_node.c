/*
 * rg_pbr_node.c  —  PBR Forward+ render graph node.
 *
 * Ports
 *   Inputs:
 *     [0] sky_color    TEXTURE  (optional) — sky background rendered by SkyNode
 *     [1] shadow_map_0 TEXTURE  — CSM cascade 0
 *     [2] shadow_map_1 TEXTURE  — CSM cascade 1
 *     [3] shadow_map_2 TEXTURE  — CSM cascade 2
 *     [4] shadow_ubo   BUFFER   — ShadowUBO with cascade VP matrices
 *   Outputs:
 *     [0] hdr_color    TEXTURE  — forward-lit HDR colour (RGBA16F)
 */

#include "qs_render_graph.h"
#include "qs_renderer.h"
#include "qs_gpu.h"
#include "qs_math.h"
#include "qs_mesh.h"
#include "qs_material.h"
#include "qs_light.h"
#include "qs_memory.h"
#include "qs_log.h"

#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* ================================================================
   CONSTANTS
   ================================================================ */

#define PBR_MSAA_TIER_COUNT    4   /* tiers: 1× 2× 4× 8× */
#define PBR_DEFAULT_MSAA       4
#define PBR_DEBUG_SHOW_NORMALS 0x1u

/* ================================================================
   GPU TYPES  (push constants — must match shader layout)
   ================================================================ */

typedef struct {
    float base_color_factor[4];
    float metallic_factor;
    float roughness_factor;
    float normal_scale;
    float occlusion_strength;
    float emissive_factor[3];
    float alpha_cutoff;
} FwdMatPC; /* 48 bytes, placed at offset 64 */

typedef struct { float inv_w, inv_h; float _pad[2]; } SkyBgPC;

/* ================================================================
   GLSL SHADERS
   ================================================================ */

static const char *FULLSCREEN_VERT =
    "#version 450\n"
    "void main() {\n"
    "    vec2 pos=vec2((gl_VertexIndex==2)?3.0:-1.0,(gl_VertexIndex==1)?3.0:-1.0);\n"
    "    gl_Position=vec4(pos,0.0,1.0);\n"
    "}\n";

/* Sky background: blit the sky texture as background before geometry */
static const char *SKY_BG_FRAG =
    "#version 450\n"
    "layout(set=0,binding=0) uniform sampler2D u_sky;\n"
    "layout(push_constant) uniform PC { vec2 inv_size; vec2 _p; } pc;\n"
    "layout(location=0) out vec4 out_color;\n"
    "void main() {\n"
    "    vec2 uv = gl_FragCoord.xy * pc.inv_size;\n"
    "    out_color = texture(u_sky, uv);\n"
    "}\n";

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
    "    float bias=max(0.003*(1.0-NdotL),0.0003);\n"
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

/* ================================================================
   NODE STATE
   ================================================================ */

typedef struct {
    Qs_GpuContext *gpu;
    uint32_t       last_w, last_h;

    /* HDR render target (owned by this node) */
    Qs_GpuImage     *hdr_image;
    Qs_GpuImageView *hdr_view;

    /* MSAA transient resources */
    Qs_GpuImage     *msaa_color_image;
    Qs_GpuImageView *msaa_color_view;
    Qs_GpuImage     *msaa_depth_image;
    Qs_GpuImageView *msaa_depth_view;
    uint32_t         current_msaa_samples;
    uint32_t         dev_max_samples;

    /* Forward pass pipelines (one pair per MSAA tier) */
    Qs_GpuPipeline *forward_pipelines[PBR_MSAA_TIER_COUNT];
    Qs_GpuPipeline *forward_wf_pipelines[PBR_MSAA_TIER_COUNT];
    Qs_GpuPipelineLayout      *forward_layout;
    Qs_GpuDescriptorSetLayout *frame_set_layout;

    /* Sky background pass */
    Qs_GpuPipeline            *sky_bg_pipelines[PBR_MSAA_TIER_COUNT]; /* one per MSAA tier */
    Qs_GpuPipelineLayout      *sky_bg_layout;
    Qs_GpuDescriptorSetLayout *sky_bg_set_layout;
    Qs_GpuDescriptorPool      *sky_bg_pool;
    Qs_GpuDescriptorSet       *sky_bg_set;
    Qs_GpuSampler             *sky_bg_sampler;
    /* Track which sky image is currently bound to avoid redundant writes */
    Qs_GpuImageView           *sky_bound_view;

    /* Frame descriptor set (set=0: FrameUBO, LightsUBO, ShadowUBO, shadow maps) */
    Qs_GpuDescriptorPool *frame_pool;
    Qs_GpuDescriptorSet  *frame_desc_set;
    Qs_GpuSampler        *shadow_sampler;
    Qs_GpuSampler        *linear_sampler;

    bool frame_desc_written; /* true once static bindings have been written */
    uint32_t desired_msaa;   /* MSAA count requested by settings; 0 = use default */

    bool ok;
} PbrNodeState;

/* ================================================================
   HELPERS
   ================================================================ */

static int sample_tier(uint32_t s)
{
    switch (s) { case 2: return 1; case 4: return 2; case 8: return 3; default: return 0; }
}

static uint32_t effective_msaa(uint32_t want, uint32_t dev_max)
{
    static const uint32_t tiers[PBR_MSAA_TIER_COUNT] = {1,2,4,8};
    uint32_t r = 1;
    for (int i = 0; i < PBR_MSAA_TIER_COUNT; i++)
        if (tiers[i] <= dev_max && tiers[i] <= want) r = tiers[i];
    return r;
}

/* ================================================================
   NODE VTABLE
   ================================================================ */

static void *pbr_create(Qs_Engine *engine, Qs_GpuContext *gpu)
{
    (void)engine;
    PbrNodeState *s = qs_calloc(1, sizeof(PbrNodeState), QS_MEM_RENDER);
    if (!s) return NULL;
    s->gpu = gpu;
    s->dev_max_samples = qs_gpu_max_sample_count(gpu);

    /* --- Frame set layout (6 bindings) --- */
    {
        Qs_GpuDescriptorBinding b[6] = {
            {0,QS_GPU_DESCRIPTOR_UNIFORM_BUFFER,        1,QS_GPU_SHADER_VERTEX|QS_GPU_SHADER_FRAGMENT},
            {1,QS_GPU_DESCRIPTOR_UNIFORM_BUFFER,        1,QS_GPU_SHADER_FRAGMENT},
            {2,QS_GPU_DESCRIPTOR_UNIFORM_BUFFER,        1,QS_GPU_SHADER_VERTEX|QS_GPU_SHADER_FRAGMENT},
            {3,QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,1,QS_GPU_SHADER_FRAGMENT},
            {4,QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,1,QS_GPU_SHADER_FRAGMENT},
            {5,QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,1,QS_GPU_SHADER_FRAGMENT},
        };
        s->frame_set_layout = qs_gpu_create_descriptor_set_layout(gpu, b, 6);
        if (!s->frame_set_layout) { QS_LOG_ERROR("PBRNode: frame set layout failed"); return s; }
    }

    /* --- Forward pipeline layout --- */
    {
        Qs_GpuDescriptorSetLayout *mat_layout = qs_material_set_layout();
        if (!mat_layout) { QS_LOG_ERROR("PBRNode: material set layout unavailable"); return s; }
        Qs_GpuPushConstantRange pcs[2] = {{QS_GPU_SHADER_VERTEX,0,64},{QS_GPU_SHADER_FRAGMENT,64,48}};
        Qs_GpuDescriptorSetLayout *sets[] = {s->frame_set_layout, mat_layout};
        s->forward_layout = qs_gpu_create_pipeline_layout(gpu,
            &(Qs_GpuPipelineLayoutDesc){sets, 2, pcs, 2});
        if (!s->forward_layout) { QS_LOG_ERROR("PBRNode: pipeline layout failed"); return s; }
    }

    /* --- Forward pipelines (one per MSAA tier) --- */
    {
        Qs_GpuShader *vs = qs_gpu_compile_shader(gpu, FORWARD_VERT, QS_GPU_SHADER_VERTEX);
        Qs_GpuShader *fs = qs_gpu_compile_shader(gpu, FORWARD_FRAG, QS_GPU_SHADER_FRAGMENT);
        if (!vs || !fs) {
            if (vs) qs_gpu_destroy_shader(gpu,vs);
            if (fs) qs_gpu_destroy_shader(gpu,fs);
            QS_LOG_ERROR("PBRNode: forward shader compilation failed");
            return s;
        }
        Qs_GpuVertexAttribute attrs[4] = {
            {0,QS_GPU_VERTEX_FORMAT_FLOAT3,offsetof(Qs_Vertex,position)},
            {1,QS_GPU_VERTEX_FORMAT_FLOAT3,offsetof(Qs_Vertex,normal)},
            {2,QS_GPU_VERTEX_FORMAT_FLOAT4,offsetof(Qs_Vertex,tangent)},
            {3,QS_GPU_VERTEX_FORMAT_FLOAT2,offsetof(Qs_Vertex,uv)},
        };
        Qs_GpuVertexBinding vb = {0, sizeof(Qs_Vertex), attrs, 4};
        static const uint32_t tiers[PBR_MSAA_TIER_COUNT] = {1,2,4,8};
        for (int i = 0; i < PBR_MSAA_TIER_COUNT; i++) {
            uint32_t sc = tiers[i];
            if (sc > s->dev_max_samples) break;
            s->forward_pipelines[i] = qs_gpu_create_graphics_pipeline(gpu,
                &(Qs_GpuGraphicsPipelineDesc){
                    s->forward_layout,vs,fs,&vb,1,
                    QS_GPU_TOPOLOGY_TRIANGLES,QS_GPU_CULL_BACK,true,true,
                    QS_GPU_FORMAT_RGBA16_SFLOAT,QS_GPU_FORMAT_DEPTH_AUTO,
                    .wireframe=false,.sample_count=sc});
            s->forward_wf_pipelines[i] = qs_gpu_create_graphics_pipeline(gpu,
                &(Qs_GpuGraphicsPipelineDesc){
                    s->forward_layout,vs,fs,&vb,1,
                    QS_GPU_TOPOLOGY_TRIANGLES,QS_GPU_CULL_NONE,true,true,
                    QS_GPU_FORMAT_RGBA16_SFLOAT,QS_GPU_FORMAT_DEPTH_AUTO,
                    .wireframe=true,.sample_count=sc});
        }
        qs_gpu_destroy_shader(gpu,vs);
        qs_gpu_destroy_shader(gpu,fs);
        if (!s->forward_pipelines[0]) { QS_LOG_ERROR("PBRNode: forward pipeline failed"); return s; }
    }

    /* --- Sky background pass --- */
    {
        Qs_GpuDescriptorBinding b = {0,QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,1,QS_GPU_SHADER_FRAGMENT};
        s->sky_bg_set_layout = qs_gpu_create_descriptor_set_layout(gpu, &b, 1);
        if (!s->sky_bg_set_layout) { QS_LOG_ERROR("PBRNode: sky bg set layout failed"); return s; }
        Qs_GpuPushConstantRange pc = {QS_GPU_SHADER_FRAGMENT, 0, 16};
        Qs_GpuDescriptorSetLayout *sets[] = {s->sky_bg_set_layout};
        s->sky_bg_layout = qs_gpu_create_pipeline_layout(gpu,
            &(Qs_GpuPipelineLayoutDesc){sets,1,&pc,1});
        if (!s->sky_bg_layout) { QS_LOG_ERROR("PBRNode: sky bg layout failed"); return s; }
        Qs_GpuShader *fv = qs_gpu_compile_shader(gpu, FULLSCREEN_VERT, QS_GPU_SHADER_VERTEX);
        Qs_GpuShader *ff = qs_gpu_compile_shader(gpu, SKY_BG_FRAG,    QS_GPU_SHADER_FRAGMENT);
        if (!fv || !ff) {
            if (fv) qs_gpu_destroy_shader(gpu,fv);
            if (ff) qs_gpu_destroy_shader(gpu,ff);
            QS_LOG_ERROR("PBRNode: sky bg shader compilation failed");
            return s;
        }
        static const uint32_t sky_tiers[PBR_MSAA_TIER_COUNT] = {1,2,4,8};
        for (int i = 0; i < PBR_MSAA_TIER_COUNT; i++) {
            uint32_t sc = sky_tiers[i];
            if (sc > s->dev_max_samples) break;
            s->sky_bg_pipelines[i] = qs_gpu_create_graphics_pipeline(gpu,
                &(Qs_GpuGraphicsPipelineDesc){
                    s->sky_bg_layout,fv,ff,NULL,0,
                    QS_GPU_TOPOLOGY_TRIANGLES,QS_GPU_CULL_NONE,false,false,
                    QS_GPU_FORMAT_RGBA16_SFLOAT,QS_GPU_FORMAT_DEPTH_AUTO,
                    .sample_count=sc});
        }
        qs_gpu_destroy_shader(gpu,fv);
        qs_gpu_destroy_shader(gpu,ff);

        s->sky_bg_sampler = qs_gpu_create_sampler(gpu, &(Qs_GpuSamplerDesc){
            .min_filter=QS_GPU_FILTER_LINEAR,.mag_filter=QS_GPU_FILTER_LINEAR,
            .wrap_u=QS_GPU_WRAP_CLAMP_TO_EDGE,.wrap_v=QS_GPU_WRAP_CLAMP_TO_EDGE,.mip_levels=1});

        Qs_GpuDescriptorPoolSize ps = {QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER, 4};
        s->sky_bg_pool = qs_gpu_create_descriptor_pool(gpu,
            &(Qs_GpuDescriptorPoolDesc){&ps, 1, 2});
        if (s->sky_bg_pool && s->sky_bg_set_layout)
            s->sky_bg_set = qs_gpu_alloc_descriptor_set(gpu, s->sky_bg_pool, s->sky_bg_set_layout);
    }

    /* --- Samplers and frame descriptor pool --- */
    s->shadow_sampler = qs_gpu_create_sampler(gpu, &(Qs_GpuSamplerDesc){
        .min_filter=QS_GPU_FILTER_LINEAR,.mag_filter=QS_GPU_FILTER_LINEAR,
        .wrap_u=QS_GPU_WRAP_CLAMP_TO_EDGE,.wrap_v=QS_GPU_WRAP_CLAMP_TO_EDGE,.mip_levels=1});
    s->linear_sampler = qs_gpu_create_sampler(gpu, &(Qs_GpuSamplerDesc){
        .min_filter=QS_GPU_FILTER_LINEAR,.mag_filter=QS_GPU_FILTER_LINEAR,
        .wrap_u=QS_GPU_WRAP_CLAMP_TO_EDGE,.wrap_v=QS_GPU_WRAP_CLAMP_TO_EDGE,.mip_levels=1});

    {
        Qs_GpuDescriptorPoolSize sizes[] = {
            {QS_GPU_DESCRIPTOR_UNIFORM_BUFFER,         6},
            {QS_GPU_DESCRIPTOR_COMBINED_IMAGE_SAMPLER, 6},
        };
        s->frame_pool = qs_gpu_create_descriptor_pool(gpu,
            &(Qs_GpuDescriptorPoolDesc){sizes, 2, 4});
        if (!s->frame_pool) { QS_LOG_ERROR("PBRNode: frame pool failed"); return s; }
        s->frame_desc_set = qs_gpu_alloc_descriptor_set(gpu, s->frame_pool, s->frame_set_layout);
        if (!s->frame_desc_set) { QS_LOG_ERROR("PBRNode: frame desc set alloc failed"); return s; }
    }

    QS_LOG_INFO("PBRNode: created (device max MSAA %u×)", s->dev_max_samples);
    return s;
}

static void pbr_destroy(void *state, Qs_GpuContext *gpu)
{
    PbrNodeState *s = state;
    if (!s) return;

    if (s->frame_desc_set) qs_gpu_free_descriptor_set(gpu, s->frame_pool, s->frame_desc_set);
    if (s->frame_pool)     qs_gpu_destroy_descriptor_pool(gpu, s->frame_pool);
    if (s->shadow_sampler) qs_gpu_destroy_sampler(gpu, s->shadow_sampler);
    if (s->linear_sampler) qs_gpu_destroy_sampler(gpu, s->linear_sampler);

    if (s->sky_bg_set)       qs_gpu_free_descriptor_set(gpu, s->sky_bg_pool, s->sky_bg_set);
    if (s->sky_bg_pool)      qs_gpu_destroy_descriptor_pool(gpu, s->sky_bg_pool);
    for (int i = 0; i < PBR_MSAA_TIER_COUNT; i++)
        if (s->sky_bg_pipelines[i]) qs_gpu_destroy_pipeline(gpu, s->sky_bg_pipelines[i]);
    if (s->sky_bg_layout)    qs_gpu_destroy_pipeline_layout(gpu, s->sky_bg_layout);
    if (s->sky_bg_set_layout)qs_gpu_destroy_descriptor_set_layout(gpu, s->sky_bg_set_layout);
    if (s->sky_bg_sampler)   qs_gpu_destroy_sampler(gpu, s->sky_bg_sampler);

    for (int i = 0; i < PBR_MSAA_TIER_COUNT; i++) {
        if (s->forward_pipelines[i])    qs_gpu_destroy_pipeline(gpu, s->forward_pipelines[i]);
        if (s->forward_wf_pipelines[i]) qs_gpu_destroy_pipeline(gpu, s->forward_wf_pipelines[i]);
    }
    if (s->forward_layout)    qs_gpu_destroy_pipeline_layout(gpu, s->forward_layout);
    if (s->frame_set_layout)  qs_gpu_destroy_descriptor_set_layout(gpu, s->frame_set_layout);

    if (s->msaa_color_view)  qs_gpu_destroy_image_view(gpu, s->msaa_color_view);
    if (s->msaa_color_image) qs_gpu_destroy_image(gpu, s->msaa_color_image);
    if (s->msaa_depth_view)  qs_gpu_destroy_image_view(gpu, s->msaa_depth_view);
    if (s->msaa_depth_image) qs_gpu_destroy_image(gpu, s->msaa_depth_image);
    if (s->hdr_view)         qs_gpu_destroy_image_view(gpu, s->hdr_view);
    if (s->hdr_image)        qs_gpu_destroy_image(gpu, s->hdr_image);

    qs_free(s);
}

static void pbr_on_resize(void *state, Qs_GpuContext *gpu, uint32_t w, uint32_t h)
{
    PbrNodeState *s = state;
    if (!s || w == 0 || h == 0) return;
    s->last_w = w;
    s->last_h = h;

    /* Destroy old HDR image */
    if (s->hdr_view)  { qs_gpu_destroy_image_view(gpu, s->hdr_view);  s->hdr_view  = NULL; }
    if (s->hdr_image) { qs_gpu_destroy_image(gpu, s->hdr_image);      s->hdr_image = NULL; }

    /* Destroy old MSAA images */
    if (s->msaa_color_view)  { qs_gpu_destroy_image_view(gpu, s->msaa_color_view);  s->msaa_color_view  = NULL; }
    if (s->msaa_color_image) { qs_gpu_destroy_image(gpu, s->msaa_color_image);      s->msaa_color_image = NULL; }
    if (s->msaa_depth_view)  { qs_gpu_destroy_image_view(gpu, s->msaa_depth_view);  s->msaa_depth_view  = NULL; }
    if (s->msaa_depth_image) { qs_gpu_destroy_image(gpu, s->msaa_depth_image);      s->msaa_depth_image = NULL; }

    /* New HDR image */
    s->hdr_image = qs_gpu_create_image(gpu, &(Qs_GpuImageDesc){
        .width=w,.height=h,.mip_levels=1,
        .format=QS_GPU_FORMAT_RGBA16_SFLOAT,
        .usage=QS_GPU_IMAGE_COLOR_ATTACHMENT|QS_GPU_IMAGE_SAMPLED,
        .sample_count=1});
    if (!s->hdr_image) { QS_LOG_ERROR("PBRNode: HDR image creation failed"); return; }
    s->hdr_view = qs_gpu_create_image_view_for(gpu, s->hdr_image, QS_GPU_IMAGE_ASPECT_COLOR);
    if (!s->hdr_view)  { QS_LOG_ERROR("PBRNode: HDR view creation failed"); return; }

    /* MSAA resources */
    uint32_t want = (s->desired_msaa > 0) ? s->desired_msaa : (uint32_t)PBR_DEFAULT_MSAA;
    s->current_msaa_samples = effective_msaa(want, s->dev_max_samples);

    if (s->current_msaa_samples > 1) {
        s->msaa_color_image = qs_gpu_create_image(gpu, &(Qs_GpuImageDesc){
            .width=w,.height=h,.mip_levels=1,
            .format=QS_GPU_FORMAT_RGBA16_SFLOAT,
            .usage=QS_GPU_IMAGE_COLOR_ATTACHMENT,
            .sample_count=s->current_msaa_samples});
        s->msaa_depth_image = qs_gpu_create_image(gpu, &(Qs_GpuImageDesc){
            .width=w,.height=h,.mip_levels=1,
            .format=QS_GPU_FORMAT_DEPTH_AUTO,
            .usage=QS_GPU_IMAGE_DEPTH_ATTACHMENT,
            .sample_count=s->current_msaa_samples});
        if (s->msaa_color_image)
            s->msaa_color_view = qs_gpu_create_image_view_for(
                gpu, s->msaa_color_image, QS_GPU_IMAGE_ASPECT_COLOR);
        if (s->msaa_depth_image)
            s->msaa_depth_view = qs_gpu_create_image_view_for(
                gpu, s->msaa_depth_image, QS_GPU_IMAGE_ASPECT_DEPTH);

        if (!s->msaa_color_view || !s->msaa_depth_view) {
            QS_LOG_ERROR("PBRNode: MSAA resource creation failed — falling back to 1×");
            if (s->msaa_color_view)  { qs_gpu_destroy_image_view(gpu, s->msaa_color_view);  s->msaa_color_view  = NULL; }
            if (s->msaa_color_image) { qs_gpu_destroy_image(gpu, s->msaa_color_image);      s->msaa_color_image = NULL; }
            if (s->msaa_depth_view)  { qs_gpu_destroy_image_view(gpu, s->msaa_depth_view);  s->msaa_depth_view  = NULL; }
            if (s->msaa_depth_image) { qs_gpu_destroy_image(gpu, s->msaa_depth_image);      s->msaa_depth_image = NULL; }
            s->current_msaa_samples = 1;
        }
    }

    s->frame_desc_written = false; /* force re-write when we next have the shadow data */
    s->sky_bound_view     = NULL;
    s->ok                 = true;

    /* Report max MSAA to the engine renderer so the settings panel can show it */
    /* (This is called from pbr_renderer_on_resize which has the renderer handle) */

    QS_LOG_INFO("PBRNode: resized to %ux%u (MSAA %u×)", w, h, s->current_msaa_samples);
}

static void pbr_execute(void *state, const Qs_RgExecCtx *ctx)
{
    PbrNodeState *s = state;
    if (!s || !s->ok || !s->hdr_image || !s->hdr_view) return;

    /* --- Write frame descriptor set once we have shadow inputs --- */
    if (!s->frame_desc_written) {
        Qs_GpuBuffer *frame_ubo   = qs_renderer_get_frame_ubo(ctx->renderer);
        Qs_GpuBuffer *lights_ubo  = qs_renderer_get_lights_ubo(ctx->renderer);
        Qs_GpuBuffer *shadow_ubo  = ctx->inputs[4].buffer; /* shadow_ubo port */
        Qs_RgTexture  sm0         = ctx->inputs[1].texture;
        Qs_RgTexture  sm1         = ctx->inputs[2].texture;
        Qs_RgTexture  sm2         = ctx->inputs[3].texture;

        if (frame_ubo && lights_ubo && shadow_ubo && sm0.view && sm1.view && sm2.view) {
            qs_gpu_write_buffer_descriptor(ctx->gpu, s->frame_desc_set, 0, frame_ubo,  0, 0);
            qs_gpu_write_buffer_descriptor(ctx->gpu, s->frame_desc_set, 1, lights_ubo, 0, 0);
            qs_gpu_write_buffer_descriptor(ctx->gpu, s->frame_desc_set, 2, shadow_ubo, 0, 0);
            qs_gpu_write_image_descriptor(ctx->gpu, s->frame_desc_set, 3, s->shadow_sampler, sm0.view);
            qs_gpu_write_image_descriptor(ctx->gpu, s->frame_desc_set, 4, s->shadow_sampler, sm1.view);
            qs_gpu_write_image_descriptor(ctx->gpu, s->frame_desc_set, 5, s->shadow_sampler, sm2.view);
            s->frame_desc_written = true;
        }
    }
    if (!s->frame_desc_written) goto emit; /* not ready yet */

    /* --- Re-check MSAA sample count from post-process settings --- */
    {
        const Qs_PostProcessSettings *pp = qs_renderer_post_process(ctx->renderer);
        uint32_t want = pp ? pp->msaa_sample_count : (uint32_t)PBR_DEFAULT_MSAA;
        uint32_t eff  = effective_msaa(want, s->dev_max_samples);
        if (eff != s->current_msaa_samples && s->last_w > 0) {
            s->desired_msaa = want;
            pbr_on_resize(s, s->gpu, s->last_w, s->last_h);
        }
    }

    /* --- Transition HDR image to colour attachment --- */
    qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
        .image=s->hdr_image, .old_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
        .new_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .aspect=QS_GPU_IMAGE_ASPECT_COLOR, .base_mip=0, .mip_count=1});

    bool use_msaa = s->current_msaa_samples > 1
                 && s->msaa_color_image && s->msaa_color_view
                 && s->msaa_depth_image && s->msaa_depth_view;
    int tier_idx = sample_tier(s->current_msaa_samples);

    /* Sky is blitted as the first draw inside the geometry pass to avoid clear overwriting it. */
    Qs_RgTexture sky = ctx->inputs[0].texture;

    /* --- Forward geometry pass --- */
    Qs_GpuPipeline *fwd_pipe;
    bool wireframe = qs_renderer_wireframe(ctx->renderer);
    if (use_msaa) {
        qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
            .image=s->msaa_color_image,
            .old_layout=QS_GPU_IMAGE_LAYOUT_UNDEFINED,
            .new_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
            .aspect=QS_GPU_IMAGE_ASPECT_COLOR,.base_mip=0,.mip_count=1});
        qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
            .image=s->msaa_depth_image,
            .old_layout=QS_GPU_IMAGE_LAYOUT_UNDEFINED,
            .new_layout=QS_GPU_IMAGE_LAYOUT_DEPTH_ATTACHMENT,
            .aspect=QS_GPU_IMAGE_ASPECT_DEPTH,.base_mip=0,.mip_count=1});

        const float *cc = qs_renderer_clear_color(ctx->renderer);
        float clear[4] = {cc?cc[0]:0.f,cc?cc[1]:0.f,cc?cc[2]:0.f,cc?cc[3]:1.f};
        qs_cmd_begin_rendering(ctx->cmd, &(Qs_GpuRenderTarget){
            .color          = s->msaa_color_view,
            .depth          = s->msaa_depth_view,
            .resolve_target = s->hdr_view,
            .clear_color    = {clear[0],clear[1],clear[2],clear[3]},
            .clear_depth    = 1.0f,
            .width          = ctx->width,
            .height         = ctx->height});
        fwd_pipe = wireframe ? s->forward_wf_pipelines[tier_idx] : s->forward_pipelines[tier_idx];
    } else {
        const float *cc = qs_renderer_clear_color(ctx->renderer);
        float clear[4] = {cc?cc[0]:0.f,cc?cc[1]:0.f,cc?cc[2]:0.f,cc?cc[3]:1.f};
        qs_cmd_begin_rendering(ctx->cmd, &(Qs_GpuRenderTarget){
            .color      = s->hdr_view,
            .depth      = qs_renderer_depth_view(ctx->renderer),
            .clear_color = {clear[0],clear[1],clear[2],clear[3]},
            .clear_depth = 1.0f,
            .width       = ctx->width,
            .height      = ctx->height});
        fwd_pipe = wireframe ? s->forward_wf_pipelines[0] : s->forward_pipelines[0];
    }

    qs_cmd_set_viewport(ctx->cmd, ctx->width, ctx->height);

    /* --- Sky blit: first draw inside the pass so it acts as the background --- */
    if (sky.view && s->sky_bg_set && s->sky_bg_sampler) {
        int sky_tier = use_msaa ? tier_idx : 0;
        if (s->sky_bg_pipelines[sky_tier]) {
            if (sky.view != s->sky_bound_view) {
                qs_gpu_write_image_descriptor(ctx->gpu, s->sky_bg_set, 0,
                                              s->sky_bg_sampler, sky.view);
                s->sky_bound_view = sky.view;
            }
            qs_cmd_bind_pipeline(ctx->cmd, s->sky_bg_pipelines[sky_tier]);
            qs_cmd_bind_descriptor_set(ctx->cmd, s->sky_bg_layout, 0, s->sky_bg_set);
            SkyBgPC sp = {1.0f/(float)ctx->width, 1.0f/(float)ctx->height, {0,0}};
            qs_cmd_push_constants(ctx->cmd, s->sky_bg_layout,
                                  QS_GPU_SHADER_FRAGMENT, 0, 16, &sp);
            qs_cmd_draw(ctx->cmd, 3, 0);
        }
    }

    qs_cmd_bind_pipeline(ctx->cmd, fwd_pipe);
    qs_cmd_bind_descriptor_set(ctx->cmd, s->forward_layout, 0, s->frame_desc_set);

    for (uint32_t ri = 0; ri < ctx->renderable_count; ri++) {
        const Qs_Renderable *ren = &ctx->renderables[ri];
        if (!ren->material_set || !ren->vertex_buffer) continue;
        qs_cmd_push_constants(ctx->cmd, s->forward_layout, QS_GPU_SHADER_VERTEX, 0, 64, ren->transform);
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
            qs_cmd_push_constants(ctx->cmd, s->forward_layout,
                                  QS_GPU_SHADER_FRAGMENT, 64, sizeof(FwdMatPC), &mpc);
        }
        qs_cmd_bind_descriptor_set(ctx->cmd, s->forward_layout, 1, ren->material_set);
        qs_cmd_bind_vertex_buffer(ctx->cmd, 0, ren->vertex_buffer, 0);
        if (ren->index_buffer)
            qs_cmd_bind_index_buffer(ctx->cmd, ren->index_buffer, ren->index_16bit);
        if (ren->index_count > 0)
            qs_cmd_draw_indexed(ctx->cmd, ren->index_count, 0, 0);
        else
            qs_cmd_draw(ctx->cmd, ren->vertex_count, 0);
    }
    qs_cmd_end_rendering(ctx->cmd);

    /* Transition HDR back to shader-read for downstream nodes */
    qs_cmd_image_barrier(ctx->cmd, &(Qs_GpuImageBarrier){
        .image=s->hdr_image, .old_layout=QS_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT,
        .new_layout=QS_GPU_IMAGE_LAYOUT_SHADER_READ,
        .aspect=QS_GPU_IMAGE_ASPECT_COLOR, .base_mip=0, .mip_count=1});

emit:
    ctx->outputs[0].texture = (Qs_RgTexture){ s->hdr_image, s->hdr_view };
}

/* ================================================================
   PUBLIC ACCESSOR  (used by pbr_renderer.c to report max MSAA)
   ================================================================ */

uint32_t qs_rg_pbr_node_dev_max_samples(Qs_RgNode *node)
{
    if (!node) return 1;
    PbrNodeState *s = qs_rg_node_impl(node);
    return s ? s->dev_max_samples : 1u;
}

/* ================================================================
   PORT DECLARATIONS
   ================================================================ */

static const Qs_RgPort k_pbr_inputs[] = {
    { .name = "sky_color",    .kind = QS_RG_TEXTURE, .optional = true  },
    { .name = "shadow_map_0", .kind = QS_RG_TEXTURE, .optional = false },
    { .name = "shadow_map_1", .kind = QS_RG_TEXTURE, .optional = false },
    { .name = "shadow_map_2", .kind = QS_RG_TEXTURE, .optional = false },
    { .name = "shadow_ubo",   .kind = QS_RG_BUFFER,  .optional = false },
};

static const Qs_RgPort k_pbr_outputs[] = {
    { .name = "hdr_color", .kind = QS_RG_TEXTURE },
};

/* ================================================================
   PUBLIC NODE TYPE
   ================================================================ */

const Qs_RgNodeType qs_rg_pbr_node_type = {
    .name         = "forward_pbr",
    .inputs       = k_pbr_inputs,
    .input_count  = 5,
    .outputs      = k_pbr_outputs,
    .output_count = 1,
    .create       = pbr_create,
    .destroy      = pbr_destroy,
    .execute      = pbr_execute,
    .on_resize    = pbr_on_resize,
};
