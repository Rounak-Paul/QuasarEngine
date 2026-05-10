/*
 * qs_render_graph.h  —  DAG render graph system.
 *
 * The render graph is a directed acyclic graph (DAG) of typed nodes.
 * Each node declares input ports (sinks) and output ports (sources).
 * Connections link an output port of one node to an input port of another.
 *
 * Resource kinds that flow between nodes:
 *   TEXTURE      — a GPU image with an associated view (Qs_RgTexture)
 *   BUFFER       — a GPU buffer (Qs_GpuBuffer*)
 *   PARAM_FLOAT  — a float scalar
 *   PARAM_UINT   — a uint32 scalar
 *   PARAM_VEC4   — four floats
 *
 * Fixed built-in nodes  (always present, registered by the PBR backend):
 *   shadow_csm     — cascaded shadow maps → outputs shadow_map_0/1/2, shadow_ubo
 *   forward_pbr    — PBR forward+ shading → outputs hdr_color
 *   tonemap        — ACES tonemap + gamma → outputs ldr_color
 *
 * Dynamic nodes (registered by plugins via QS_EXT_RENDER_GRAPH_NODE):
 *   Stage QS_RG_STAGE_PRE_GEOMETRY   — runs before PBR  (e.g. sky)
 *   Stage QS_RG_STAGE_POST_GEOMETRY  — runs after  PBR  (e.g. bloom, SSAO)
 *   Stage QS_RG_STAGE_POST_TONEMAP   — runs after tonemap, chained to swapchain
 *
 * Default connection conventions (applied by the PBR renderer):
 *   PRE_GEOMETRY  node: color_out  → PBR.sky_color
 *   POST_GEOMETRY node: PBR.hdr_color → color_in;  bloom_tex → tonemap.bloom_tex
 *   POST_TONEMAP  node: tonemap.ldr_color (or prev.color_out) → color_in; terminal → swapchain
 */

#ifndef QS_RENDER_GRAPH_H
#define QS_RENDER_GRAPH_H

#include "qs_gpu.h"
#include "qs_renderer.h"
#include <stdint.h>
#include <stdbool.h>

/* ================================================================
   RESOURCE KINDS
   ================================================================ */

typedef enum Qs_RgResourceKind {
    QS_RG_TEXTURE,       ///< { Qs_GpuImage*, Qs_GpuImageView* }
    QS_RG_BUFFER,        ///< Qs_GpuBuffer*
    QS_RG_PARAM_FLOAT,   ///< float
    QS_RG_PARAM_UINT,    ///< uint32_t
    QS_RG_PARAM_VEC4,    ///< float[4]
} Qs_RgResourceKind;

/// Texture resource: a GPU image together with its view.
typedef struct Qs_RgTexture {
    Qs_GpuImage     *image;
    Qs_GpuImageView *view;
} Qs_RgTexture;

/// Value that flows along a graph edge.
typedef union Qs_RgValue {
    Qs_RgTexture  texture;
    Qs_GpuBuffer *buffer;
    float         f32;
    uint32_t      u32;
    float         vec4[4];
} Qs_RgValue;

/* ================================================================
   PORT DECLARATION  (slot on a node)
   ================================================================ */

typedef struct Qs_RgPort {
    const char       *name;
    Qs_RgResourceKind kind;
    bool              optional; ///< Input ports only.  Unconnected optional inputs yield a zero value.
} Qs_RgPort;

/* ================================================================
   EXECUTE CONTEXT  (passed to every node callback each frame)
   ================================================================ */

typedef struct Qs_RgExecCtx {
    Qs_Engine      *engine;
    Qs_Renderer    *renderer;
    Qs_GpuContext  *gpu;
    Qs_GpuCmd      *cmd;
    uint32_t        width;
    uint32_t        height;
    float           dt;
    float           view[16];
    float           proj[16];

    /* Frame renderables + lights — same content as Qs_RenderContext */
    const Qs_Renderable *renderables;
    uint32_t             renderable_count;
    const Qs_LightGPU   *lights;
    uint32_t             light_count;

    /* Terminal node only: the swapchain output target.
       NULL for all non-terminal nodes.  The terminal node MUST render its
       final output into swapchain_view. */
    Qs_GpuImageView *swapchain_view;
    uint32_t         swapchain_width;
    uint32_t         swapchain_height;

    /* Port values — graph-managed; valid for the duration of the callback. */
    const Qs_RgValue *inputs;    ///< [type->input_count]
    Qs_RgValue       *outputs;   ///< [type->output_count]; node must write its outputs here
} Qs_RgExecCtx;

/* ================================================================
   NODE TYPE VTABLE
   ================================================================ */

/// Implement this struct to define a render graph node type.
/// Fixed engine nodes expose a `const Qs_RgNodeType` symbol.
/// Plugin nodes register a `Qs_RgNodeTypeExt` at QS_EXT_RENDER_GRAPH_NODE.
typedef struct Qs_RgNodeType {
    const char      *name;           ///< Unique type name, e.g. "sky", "bloom".

    const Qs_RgPort *inputs;
    uint32_t         input_count;
    const Qs_RgPort *outputs;
    uint32_t         output_count;

    /// Allocate node-private state.  GPU resources (pipelines, images) should
    /// be created here (or deferred to on_resize for viewport-sized resources).
    void *(*create)   (Qs_Engine *engine, Qs_GpuContext *gpu);

    /// Free all GPU resources and node-private state.
    void  (*destroy)  (void *state, Qs_GpuContext *gpu);

    /// Perform GPU work for one frame.  Read inputs via ctx->inputs[i] and
    /// write outputs via ctx->outputs[i].  The terminal node must render into
    /// ctx->swapchain_view when it is non-NULL.
    void  (*execute)  (void *state, const Qs_RgExecCtx *ctx);

    /// Recreate viewport-sized GPU resources (images, views, descriptor sets).
    /// Called before the first execute and whenever the viewport changes size.
    void  (*on_resize)(void *state, Qs_GpuContext *gpu, uint32_t w, uint32_t h);
} Qs_RgNodeType;

/* ================================================================
   OPAQUE HANDLES
   ================================================================ */

typedef struct Qs_RenderGraph Qs_RenderGraph;
typedef struct Qs_RgNode      Qs_RgNode;   ///< Opaque node instance in a graph.

/* ================================================================
   GRAPH API
   ================================================================ */

/// Create an empty render graph.
Qs_RenderGraph *qs_rg_create (Qs_Engine *engine, Qs_GpuContext *gpu);

/// Destroy the graph and all node instances it owns.
void            qs_rg_destroy(Qs_RenderGraph *rg);

/// Add a node of the given type.  The node's create() callback is called
/// immediately.  Returns the node handle (valid until qs_rg_destroy).
Qs_RgNode *qs_rg_add_node  (Qs_RenderGraph *rg, const Qs_RgNodeType *type);

/// Add a "source" node — zero inputs, one output of the given kind.
/// The current value is injected via qs_rg_source_set() before each execute.
Qs_RgNode *qs_rg_add_source(Qs_RenderGraph *rg, const char *name,
                             Qs_RgResourceKind kind);

/// Update the value emitted by a source node.
void       qs_rg_source_set(Qs_RgNode *source, Qs_RgValue value);

/// Connect src_node's output port [src_output] to dst_node's input port [dst_input].
/// Returns false if indices are out of range, kinds mismatch, or the input is
/// already connected.
bool qs_rg_connect(Qs_RenderGraph *rg,
                   Qs_RgNode *src, uint32_t src_output,
                   Qs_RgNode *dst, uint32_t dst_input);

/// Convenience wrapper: connect by port name instead of index.
bool qs_rg_connect_named(Qs_RenderGraph *rg,
                         Qs_RgNode *src, const char *src_out_name,
                         Qs_RgNode *dst, const char *dst_in_name);

/// Validate connections and compute the topological execution order.
/// Must be called once after all nodes and connections are established.
/// Returns false if the graph has cycles or missing required inputs.
bool qs_rg_compile(Qs_RenderGraph *rg);

/// Execute the compiled graph for one frame.
void qs_rg_execute(Qs_RenderGraph *rg, const Qs_RenderContext *ctx);

/// Notify the graph that the viewport was resized.  Calls on_resize on every
/// non-source node in compile order.
void qs_rg_on_resize(Qs_RenderGraph *rg, uint32_t w, uint32_t h);

/// Find a node in the graph by its node-type name.  Returns NULL if not found.
Qs_RgNode *qs_rg_find_node(Qs_RenderGraph *rg, const char *type_name);

/// Return the private implementation pointer (state) of a node, as created by
/// the node type's create() callback.  Useful for fixed nodes that need to
/// expose additional data (e.g. device max MSAA).
void *qs_rg_node_impl(Qs_RgNode *node);

/* ================================================================
   EXTENSION POINT: render_graph.node
   Plugins register Qs_RgNodeTypeExt here to contribute dynamic nodes.
   ================================================================ */

/// Extension point name for dynamic render graph nodes.
#define QS_EXT_RENDER_GRAPH_NODE "render_graph.node"

/// Stage hint that controls where the PBR renderer inserts the dynamic node
/// into the default pipeline.
typedef enum Qs_RgNodeStage {
    QS_RG_STAGE_PRE_GEOMETRY  = 0, ///< Before PBR forward shading (e.g. sky).
    QS_RG_STAGE_POST_GEOMETRY = 1, ///< After PBR, before tonemap (e.g. bloom, SSAO).
    QS_RG_STAGE_POST_TONEMAP  = 2, ///< After tonemap, chained to swapchain (e.g. vignette).
} Qs_RgNodeStage;

/// Interface for QS_EXT_RENDER_GRAPH_NODE extensions.
typedef struct Qs_RgNodeTypeExt {
    const Qs_RgNodeType *type;
    Qs_RgNodeStage       stage;
} Qs_RgNodeTypeExt;

/* ================================================================
   BUILT-IN FIXED NODE TYPE DECLARATIONS
   ================================================================ */

extern const Qs_RgNodeType qs_rg_shadow_node_type;
extern const Qs_RgNodeType qs_rg_pbr_node_type;
extern const Qs_RgNodeType qs_rg_tonemap_node_type;

#endif /* QS_RENDER_GRAPH_H */
