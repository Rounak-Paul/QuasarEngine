/*
 * qs_render_graph.c  —  DAG render graph implementation.
 *
 * Nodes are stored in a fixed-size pool.  After qs_rg_compile() the pool is
 * topologically sorted with Kahn's algorithm so that every producer executes
 * before its consumers.  The last node in the sorted order is the "terminal"
 * and receives the swapchain view in its execute context.
 */

#include "qs_render_graph.h"
#include "qs_memory.h"
#include "qs_log.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ================================================================
   INTERNAL LIMITS
   ================================================================ */

#define QS_RG_MAX_NODES  64u
#define QS_RG_MAX_SLOTS  16u
#define QS_RG_NULL_IDX   UINT32_MAX

/* ================================================================
   INTERNAL NODE STATE
   ================================================================ */

typedef struct {
    /* Type vtable.  NULL for source nodes. */
    const Qs_RgNodeType *type;
    void                *impl;

    bool              is_source;
    Qs_RgResourceKind source_kind;
    char              source_name[32];
    Qs_RgValue        source_val;

    /* Per-slot input wiring: index into rg->nodes[] + slot within that node.
       QS_RG_NULL_IDX means unconnected. */
    uint32_t input_from_node[QS_RG_MAX_SLOTS];
    uint32_t input_from_slot[QS_RG_MAX_SLOTS];

    /* Output values written during execute; read by downstream nodes. */
    Qs_RgValue outputs[QS_RG_MAX_SLOTS];

    uint32_t self_idx;
} RgNodeState;

/* Qs_RgNode is the public handle type; internally it IS RgNodeState. */
struct Qs_RgNode { char _opaque; };

static inline RgNodeState *as_state(Qs_RgNode *n)
{
    return (RgNodeState *)(void *)n;
}
static inline Qs_RgNode *as_handle(RgNodeState *s)
{
    return (Qs_RgNode *)(void *)s;
}

/* ================================================================
   GRAPH STRUCT
   ================================================================ */

struct Qs_RenderGraph {
    Qs_Engine     *engine;
    Qs_GpuContext *gpu;

    RgNodeState  nodes[QS_RG_MAX_NODES];
    uint32_t     node_count;

    /* Compiled execution plan */
    uint32_t exec_order[QS_RG_MAX_NODES]; /* topo-sorted node indices */
    uint32_t exec_count;
    uint32_t terminal_node; /* index of last node — receives swapchain view */
    bool     compiled;
};

/* ================================================================
   LIFECYCLE
   ================================================================ */

Qs_RenderGraph *qs_rg_create(Qs_Engine *engine, Qs_GpuContext *gpu)
{
    Qs_RenderGraph *rg = qs_calloc(1, sizeof(Qs_RenderGraph), QS_MEM_RENDER);
    if (!rg) return NULL;
    rg->engine  = engine;
    rg->gpu     = gpu;
    rg->terminal_node = QS_RG_NULL_IDX;
    /* Initialise all input_from_node to NULL_IDX */
    for (uint32_t i = 0; i < QS_RG_MAX_NODES; i++)
        for (uint32_t j = 0; j < QS_RG_MAX_SLOTS; j++)
            rg->nodes[i].input_from_node[j] = QS_RG_NULL_IDX;
    return rg;
}

void qs_rg_destroy(Qs_RenderGraph *rg)
{
    if (!rg) return;
    for (uint32_t i = 0; i < rg->node_count; i++) {
        RgNodeState *s = &rg->nodes[i];
        if (!s->is_source && s->type && s->type->destroy && s->impl)
            s->type->destroy(s->impl, rg->gpu);
    }
    qs_free(rg);
}

/* ================================================================
   ADD NODES
   ================================================================ */

Qs_RgNode *qs_rg_add_node(Qs_RenderGraph *rg, const Qs_RgNodeType *type)
{
    if (!rg || !type || rg->node_count >= QS_RG_MAX_NODES) return NULL;

    RgNodeState *s = &rg->nodes[rg->node_count];
    memset(s, 0, sizeof(*s));
    for (uint32_t j = 0; j < QS_RG_MAX_SLOTS; j++)
        s->input_from_node[j] = QS_RG_NULL_IDX;

    s->type     = type;
    s->self_idx = rg->node_count;

    if (type->create)
        s->impl = type->create(rg->engine, rg->gpu);

    if (!s->impl && type->create) {
        QS_LOG_ERROR("RenderGraph: node '%s' create() returned NULL", type->name);
        return NULL;
    }

    rg->node_count++;
    return as_handle(s);
}

Qs_RgNode *qs_rg_add_source(Qs_RenderGraph *rg, const char *name,
                              Qs_RgResourceKind kind)
{
    if (!rg || rg->node_count >= QS_RG_MAX_NODES) return NULL;

    RgNodeState *s = &rg->nodes[rg->node_count];
    memset(s, 0, sizeof(*s));
    for (uint32_t j = 0; j < QS_RG_MAX_SLOTS; j++)
        s->input_from_node[j] = QS_RG_NULL_IDX;

    s->is_source   = true;
    s->source_kind = kind;
    s->self_idx    = rg->node_count;
    if (name) snprintf(s->source_name, sizeof(s->source_name), "%s", name);

    rg->node_count++;
    return as_handle(s);
}

void qs_rg_source_set(Qs_RgNode *source, Qs_RgValue value)
{
    if (!source) return;
    RgNodeState *s = as_state(source);
    if (!s->is_source) return;
    s->source_val = value;
}

/* ================================================================
   CONNECTIONS
   ================================================================ */

static uint32_t node_idx(Qs_RenderGraph *rg, Qs_RgNode *node)
{
    RgNodeState *s = as_state(node);
    uint32_t idx = (uint32_t)(s - rg->nodes);
    return (idx < rg->node_count) ? idx : QS_RG_NULL_IDX;
}

bool qs_rg_connect(Qs_RenderGraph *rg,
                   Qs_RgNode *src, uint32_t src_output,
                   Qs_RgNode *dst, uint32_t dst_input)
{
    if (!rg || !src || !dst) return false;

    RgNodeState *ss = as_state(src);
    RgNodeState *ds = as_state(dst);

    uint32_t si = node_idx(rg, src);
    uint32_t di = node_idx(rg, dst);
    if (si == QS_RG_NULL_IDX || di == QS_RG_NULL_IDX) {
        QS_LOG_ERROR("RenderGraph: connect — node not in graph");
        return false;
    }

    /* Source output count */
    uint32_t src_out_count = ss->is_source ? 1u :
                             (ss->type ? ss->type->output_count : 0u);
    if (src_output >= src_out_count) {
        QS_LOG_ERROR("RenderGraph: connect — src output index %u out of range (%u)",
                     src_output, src_out_count);
        return false;
    }

    /* Destination input count */
    uint32_t dst_in_count = ds->is_source ? 0u :
                            (ds->type ? ds->type->input_count : 0u);
    if (dst_input >= dst_in_count) {
        QS_LOG_ERROR("RenderGraph: connect — dst input index %u out of range (%u)",
                     dst_input, dst_in_count);
        return false;
    }

    /* Kind check */
    Qs_RgResourceKind src_kind = ss->is_source ? ss->source_kind
                                               : ss->type->outputs[src_output].kind;
    Qs_RgResourceKind dst_kind = ds->type->inputs[dst_input].kind;
    if (src_kind != dst_kind) {
        QS_LOG_ERROR("RenderGraph: connect — kind mismatch (src=%d dst=%d)",
                     (int)src_kind, (int)dst_kind);
        return false;
    }

    if (ds->input_from_node[dst_input] != QS_RG_NULL_IDX) {
        QS_LOG_ERROR("RenderGraph: connect — dst input %u already connected", dst_input);
        return false;
    }

    ds->input_from_node[dst_input] = si;
    ds->input_from_slot[dst_input] = src_output;
    return true;
}

bool qs_rg_connect_named(Qs_RenderGraph *rg,
                          Qs_RgNode *src, const char *src_out_name,
                          Qs_RgNode *dst, const char *dst_in_name)
{
    if (!rg || !src || !dst || !src_out_name || !dst_in_name) return false;

    RgNodeState *ss = as_state(src);
    RgNodeState *ds = as_state(dst);

    /* Resolve source output slot */
    uint32_t src_output = QS_RG_NULL_IDX;
    if (ss->is_source) {
        if (strcmp(src_out_name, ss->source_name) == 0 ||
            strcmp(src_out_name, "value") == 0)
            src_output = 0;
    } else if (ss->type) {
        for (uint32_t i = 0; i < ss->type->output_count; i++) {
            if (ss->type->outputs[i].name &&
                strcmp(ss->type->outputs[i].name, src_out_name) == 0) {
                src_output = i; break;
            }
        }
    }
    if (src_output == QS_RG_NULL_IDX) {
        QS_LOG_ERROR("RenderGraph: connect_named — output '%s' not found on '%s'",
                     src_out_name,
                     ss->is_source ? ss->source_name : (ss->type ? ss->type->name : "?"));
        return false;
    }

    /* Resolve destination input slot */
    uint32_t dst_input = QS_RG_NULL_IDX;
    if (!ds->is_source && ds->type) {
        for (uint32_t i = 0; i < ds->type->input_count; i++) {
            if (ds->type->inputs[i].name &&
                strcmp(ds->type->inputs[i].name, dst_in_name) == 0) {
                dst_input = i; break;
            }
        }
    }
    if (dst_input == QS_RG_NULL_IDX) {
        QS_LOG_ERROR("RenderGraph: connect_named — input '%s' not found on '%s'",
                     dst_in_name, ds->type ? ds->type->name : "?");
        return false;
    }

    return qs_rg_connect(rg, src, src_output, dst, dst_input);
}

/* ================================================================
   COMPILE  (Kahn's topological sort)
   ================================================================ */

bool qs_rg_compile(Qs_RenderGraph *rg)
{
    if (!rg || rg->node_count == 0) return false;

    uint32_t n = rg->node_count;

    /* Compute in-degree for each node */
    uint32_t in_deg[QS_RG_MAX_NODES] = {0};
    for (uint32_t i = 0; i < n; i++) {
        RgNodeState *s = &rg->nodes[i];
        uint32_t inp = s->is_source ? 0u : (s->type ? s->type->input_count : 0u);
        for (uint32_t j = 0; j < inp; j++) {
            if (s->input_from_node[j] != QS_RG_NULL_IDX)
                in_deg[i]++;
        }
    }

    /* Kahn's algorithm */
    uint32_t queue[QS_RG_MAX_NODES];
    uint32_t q_head = 0, q_tail = 0;
    for (uint32_t i = 0; i < n; i++)
        if (in_deg[i] == 0) queue[q_tail++] = i;

    rg->exec_count = 0;
    while (q_head < q_tail) {
        uint32_t cur = queue[q_head++];
        rg->exec_order[rg->exec_count++] = cur;

        /* Decrement successors */
        for (uint32_t j = 0; j < n; j++) {
            RgNodeState *succ = &rg->nodes[j];
            uint32_t inp = succ->is_source ? 0u : (succ->type ? succ->type->input_count : 0u);
            for (uint32_t k = 0; k < inp; k++) {
                if (succ->input_from_node[k] == cur) {
                    in_deg[j]--;
                    if (in_deg[j] == 0)
                        queue[q_tail++] = j;
                }
            }
        }
    }

    if (rg->exec_count != n) {
        QS_LOG_ERROR("RenderGraph: cycle detected — compilation failed");
        return false;
    }

    /* Validate required inputs */
    for (uint32_t i = 0; i < n; i++) {
        RgNodeState *s = &rg->nodes[i];
        if (s->is_source || !s->type) continue;
        for (uint32_t j = 0; j < s->type->input_count; j++) {
            if (!s->type->inputs[j].optional &&
                s->input_from_node[j] == QS_RG_NULL_IDX) {
                QS_LOG_ERROR("RenderGraph: required input '%s.%s' is unconnected",
                             s->type->name, s->type->inputs[j].name);
                return false;
            }
        }
    }

    /* Terminal = last node in exec order with no outgoing edges */
    rg->terminal_node = rg->exec_order[rg->exec_count - 1];

    rg->compiled = true;

    /* Log the execution plan */
    QS_LOG_INFO("RenderGraph: compiled %u nodes:", rg->exec_count);
    for (uint32_t i = 0; i < rg->exec_count; i++) {
        RgNodeState *s = &rg->nodes[rg->exec_order[i]];
        const char *name = s->is_source ? s->source_name
                         : (s->type ? s->type->name : "?");
        QS_LOG_INFO("  [%u] %s%s", i, name,
                    rg->exec_order[i] == rg->terminal_node ? " (terminal)" : "");
    }
    return true;
}

/* ================================================================
   EXECUTE
   ================================================================ */

void qs_rg_execute(Qs_RenderGraph *rg, const Qs_RenderContext *ctx)
{
    if (!rg || !rg->compiled || !ctx) return;

    Qs_RgExecCtx ectx;
    memset(&ectx, 0, sizeof(ectx));
    ectx.engine            = rg->engine;
    ectx.renderer          = ctx->renderer;
    ectx.gpu               = rg->gpu;
    ectx.cmd               = ctx->cmd;
    ectx.width             = ctx->width;
    ectx.height            = ctx->height;
    ectx.dt                = ctx->dt;
    ectx.renderables       = ctx->renderables;
    ectx.renderable_count  = ctx->renderable_count;
    ectx.lights            = ctx->lights;
    ectx.light_count       = ctx->light_count;
    memcpy(ectx.view, ctx->view, sizeof(ectx.view));
    memcpy(ectx.proj, ctx->proj, sizeof(ectx.proj));

    Qs_RgValue inputs[QS_RG_MAX_SLOTS];

    for (uint32_t ei = 0; ei < rg->exec_count; ei++) {
        uint32_t    ni = rg->exec_order[ei];
        RgNodeState *s = &rg->nodes[ni];

        /* Terminal node gets the swapchain target */
        if (ni == rg->terminal_node) {
            ectx.swapchain_view   = ctx->swapchain_view;
            ectx.swapchain_width  = ctx->swapchain_width;
            ectx.swapchain_height = ctx->swapchain_height;
        } else {
            ectx.swapchain_view   = NULL;
            ectx.swapchain_width  = 0;
            ectx.swapchain_height = 0;
        }

        /* Source nodes just emit their stored value */
        if (s->is_source) {
            s->outputs[0] = s->source_val;
            continue;
        }

        if (!s->type || !s->type->execute) continue;

        /* Collect inputs from connected producers */
        uint32_t inp_count = s->type->input_count;
        memset(inputs, 0, sizeof(Qs_RgValue) * inp_count);
        for (uint32_t k = 0; k < inp_count; k++) {
            uint32_t fn = s->input_from_node[k];
            uint32_t fs = s->input_from_slot[k];
            if (fn != QS_RG_NULL_IDX)
                inputs[k] = rg->nodes[fn].outputs[fs];
        }

        ectx.inputs  = inputs;
        ectx.outputs = s->outputs;

        s->type->execute(s->impl, &ectx);
    }
}

/* ================================================================
   RESIZE
   ================================================================ */

void qs_rg_on_resize(Qs_RenderGraph *rg, uint32_t w, uint32_t h)
{
    if (!rg || w == 0 || h == 0) return;

    /* Use compile order if available, otherwise linear order */
    for (uint32_t i = 0; i < rg->node_count; i++) {
        uint32_t    ni = rg->compiled ? rg->exec_order[i] : i;
        RgNodeState *s = &rg->nodes[ni];
        if (!s->is_source && s->type && s->type->on_resize && s->impl)
            s->type->on_resize(s->impl, rg->gpu, w, h);
    }
}

/* ================================================================
   QUERY
   ================================================================ */

Qs_RgNode *qs_rg_find_node(Qs_RenderGraph *rg, const char *type_name)
{
    if (!rg || !type_name) return NULL;
    for (uint32_t i = 0; i < rg->node_count; i++) {
        RgNodeState *s = &rg->nodes[i];
        if (s->is_source) {
            if (strcmp(s->source_name, type_name) == 0)
                return as_handle(s);
        } else if (s->type && s->type->name &&
                   strcmp(s->type->name, type_name) == 0) {
            return as_handle(s);
        }
    }
    return NULL;
}

void *qs_rg_node_impl(Qs_RgNode *node)
{
    if (!node) return NULL;
    return as_state(node)->impl;
}
