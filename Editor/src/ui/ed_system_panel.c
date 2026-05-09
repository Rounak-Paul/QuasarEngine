/*
 * ed_system_panel.c — System / Memory diagnostics panel.
 *
 * Shows CPU heap and GPU VRAM usage in a compact, uniform-font layout.
 * Built once in ed_system_panel(); per-frame data pushed via statics in
 * ed_system_panel_update().
 */

#include "ed_system_panel.h"
#include "quasar.h"
#include "ca_theme.h"

#include <stdio.h>
#include <string.h>

/* ================================================================
   HELPERS
   ================================================================ */

typedef struct TagMeta {
    const char *icon;
    const char *label;
    uint32_t    bar_color;
} TagMeta;

#define ICON_CPU       "\xEF\x8B\xBB"
#define ICON_RENDER    "\xEF\x81\xA5"
#define ICON_GPU       "\xEF\x8B\x87"
#define ICON_TEXTURE   "\xEF\x80\xBE"
#define ICON_MESH      "\xEF\x86\x99"
#define ICON_MATERIAL  "\xEF\x81\x82"
#define ICON_SCENE     "\xEF\x80\xBC"
#define ICON_ASSET     "\xEF\x81\xBB"
#define ICON_JOB       "\xEF\x80\x93"
#define ICON_EVENT     "\xEF\x83\xA1"
#define ICON_LOG       "\xEF\x85\xA9"
#define ICON_PLUGIN    "\xEF\x87\xBC"
#define ICON_PROJECT   "\xEF\x81\xAE"
#define ICON_UI        "\xEF\x84\xA8"
#define ICON_EDITOR    "\xEF\x81\x9C"
#define ICON_GENERAL   "\xEF\x81\xAD"

static const TagMeta k_meta[QS_MEM_TAG_COUNT] = {
    /* QS_MEM_GENERAL  */ { ICON_GENERAL,  "General",  0xFF8866FFu },
    /* QS_MEM_ENGINE   */ { ICON_CPU,      "Engine",   0xFF88AAFFu },
    /* QS_MEM_RENDER   */ { ICON_RENDER,   "Render",   0xFF66DDFFu },
    /* QS_MEM_GPU      */ { ICON_GPU,      "GPU",      0xFFAA66FFu },
    /* QS_MEM_TEXTURE  */ { ICON_TEXTURE,  "Texture",  0xFF66FF99u },
    /* QS_MEM_MESH     */ { ICON_MESH,     "Mesh",     0xFFFF9944u },
    /* QS_MEM_MATERIAL */ { ICON_MATERIAL, "Material", 0xFFFFCC44u },
    /* QS_MEM_SCENE    */ { ICON_SCENE,    "Scene",    0xFF44CCFFu },
    /* QS_MEM_ASSET    */ { ICON_ASSET,    "Asset",    0xFFFF6699u },
    /* QS_MEM_JOB      */ { ICON_JOB,      "Job",      0xFFCCFF66u },
    /* QS_MEM_EVENT    */ { ICON_EVENT,    "Event",    0xFFFFAA33u },
    /* QS_MEM_LOG      */ { ICON_LOG,      "Log",      0xFF99AABBu },
    /* QS_MEM_PLUGIN   */ { ICON_PLUGIN,   "Plugin",   0xFFCC88FFu },
    /* QS_MEM_PROJECT  */ { ICON_PROJECT,  "Project",  0xFF88FFCCu },
    /* QS_MEM_UI       */ { ICON_UI,       "UI",       0xFF66AAFFu },
    /* QS_MEM_EDITOR   */ { ICON_EDITOR,   "Editor",   0xFFFFEE55u },
};

/* ================================================================
   STATIC WIDGET HANDLES
   ================================================================ */

/* CPU overview row */
static Ca_Label    *s_total_bytes_lbl;
static Ca_Label    *s_total_allocs_lbl;
static Ca_Progress *s_total_bar;

/* Per-tag rows */
typedef struct TagRow {
    Ca_Label    *bytes_lbl;
    Ca_Label    *alloc_lbl;
    Ca_Progress *bar;
} TagRow;
static TagRow s_rows[QS_MEM_TAG_COUNT];

/* GPU section */
static Ca_Label    *s_gpu_name_lbl;
static Ca_Label    *s_gpu_device_lbl;    /* "123 MB / 8 GB" */
static Ca_Label    *s_gpu_host_lbl;      /* "12 MB host"    */
static Ca_Progress *s_gpu_device_bar;
static Ca_Progress *s_gpu_host_bar;
/* Per-purpose GPU tag labels */
static Ca_Label    *s_gpu_tag_lbl[QS_GPU_MEM_TAG_COUNT];

static const TagMeta k_gpu_meta[QS_GPU_MEM_TAG_COUNT] = {
    /* QS_GPU_MEM_VERTEX   */ { ICON_MESH,     "Vertex",    0xFFFF9944u },
    /* QS_GPU_MEM_INDEX    */ { ICON_MESH,     "Index",     0xFFFFCC44u },
    /* QS_GPU_MEM_UNIFORM  */ { ICON_GPU,      "Uniforms",  0xFFAA88FFu },
    /* QS_GPU_MEM_STORAGE  */ { ICON_GPU,      "Storage",   0xFF8866FFu },
    /* QS_GPU_MEM_TEXTURE  */ { ICON_TEXTURE,  "Textures",  0xFF66FF99u },
    /* QS_GPU_MEM_RT_COLOR */ { ICON_RENDER,   "Color RTs", 0xFF66DDFFu },
    /* QS_GPU_MEM_RT_DEPTH */ { ICON_RENDER,   "Depth",     0xFF44CCFFu },
    /* QS_GPU_MEM_OTHER    */ { ICON_GENERAL,  "Other",     0xFF99AABBu },
};

/* ================================================================
   FORMAT HELPERS
   ================================================================ */

static void fmt_bytes(char *buf, size_t cap, size_t bytes)
{
    if      (bytes >= 1024ULL * 1024 * 1024)
        snprintf(buf, cap, "%.2f GB", (double)bytes / (1024.0*1024.0*1024.0));
    else if (bytes >= 1024 * 1024)
        snprintf(buf, cap, "%.2f MB", (double)bytes / (1024.0*1024.0));
    else if (bytes >= 1024)
        snprintf(buf, cap, "%.1f KB", (double)bytes / 1024.0);
    else
        snprintf(buf, cap, "%zu B", bytes);
}

static void fmt_count(char *buf, size_t cap, size_t n)
{
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%zu", n);
    size_t len = strlen(tmp);
    size_t out = 0;
    for (size_t i = 0; i < len && out + 1 < cap; i++) {
        if (i > 0 && (len - i) % 3 == 0 && out + 2 < cap)
            buf[out++] = ',';
        buf[out++] = tmp[i];
    }
    buf[out] = '\0';
}

/* ================================================================
   BUILD (called once)
   ================================================================ */

void ed_system_panel(void)
{
    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "sys-scroll",
        .id        = "sys-scroll",
    });

    /* ── GPU card ─────────────────────────────────────────────── */
    ca_div_begin(&(Ca_DivDesc){ .direction = CA_VERTICAL, .style = "sys-card" });
    {
        ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "sys-card-header" });
        ca_text(&(Ca_TextDesc){ .text = ICON_GPU "  GPU MEMORY", .style = "sys-section-title" });
        ca_spacer(&(Ca_SpacerDesc){ .style = "fb-spacer-grow" });
        s_gpu_name_lbl = ca_text(&(Ca_TextDesc){ .text = "—", .style = "sys-alloc-badge" });
        ca_div_end();

        /* VRAM device-local row */
        ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "sys-row-top" });
        ca_text(&(Ca_TextDesc){ .text = "Device VRAM",   .style = "sys-col-cat" });
        s_gpu_device_lbl = ca_text(&(Ca_TextDesc){ .text = "0 B", .style = "sys-col-bytes-wide" });
        ca_div_end();
        s_gpu_device_bar = ca_progress(&(Ca_ProgressDesc){
            .value = 0.0f, .height = 3.0f,
            .bar_color = 0xFF6E8AFFu, .style = "sys-mini-bar",
        });

        /* Host-visible (mapped GPU buffers: UBOs, etc.) row */
        ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "sys-row-top" });
        ca_text(&(Ca_TextDesc){ .text = "Mapped Buffers", .style = "sys-col-cat" });
        s_gpu_host_lbl = ca_text(&(Ca_TextDesc){ .text = "0 B", .style = "sys-col-bytes-wide" });
        ca_div_end();
        s_gpu_host_bar = ca_progress(&(Ca_ProgressDesc){
            .value = 0.0f, .height = 3.0f,
            .bar_color = 0xFFAA88FFu, .style = "sys-mini-bar",
        });

        /* Per-purpose breakdown */
        ca_hr(&(Ca_HrDesc){ .color = 0xFF1E2030u });
        ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "sys-row-header" });
        ca_text(&(Ca_TextDesc){ .text = "TYPE", .style = "sys-col-cat"        });
        ca_text(&(Ca_TextDesc){ .text = "VRAM", .style = "sys-col-bytes-wide" });
        ca_div_end();
        for (int t = 0; t < QS_GPU_MEM_TAG_COUNT; t++) {
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_VERTICAL,
                .style     = (t % 2 == 0) ? "sys-row" : "sys-row sys-row-alt",
            });
            {
                ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "sys-row-top" });
                char icon_label[64];
                snprintf(icon_label, sizeof(icon_label), "%s  %s",
                         k_gpu_meta[t].icon, k_gpu_meta[t].label);
                ca_text(&(Ca_TextDesc){ .text = icon_label, .style = "sys-col-cat" });
                s_gpu_tag_lbl[t] = ca_text(&(Ca_TextDesc){
                    .text = "0 B", .style = "sys-col-bytes-wide",
                });
                ca_div_end();
            }
            ca_div_end();
        }
    }
    ca_div_end();

    /* ── CPU overview card ─────────────────────────────────────── */
    ca_div_begin(&(Ca_DivDesc){ .direction = CA_VERTICAL, .style = "sys-card" });
    {
        ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "sys-card-header" });
        ca_text(&(Ca_TextDesc){ .text = ICON_CPU "  CPU HEAP", .style = "sys-section-title" });
        ca_spacer(&(Ca_SpacerDesc){ .style = "fb-spacer-grow" });
        s_total_allocs_lbl = ca_text(&(Ca_TextDesc){ .text = "0 allocs", .style = "sys-alloc-badge" });
        ca_div_end();

        ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "sys-row-top" });
        ca_text(&(Ca_TextDesc){ .text = "Live", .style = "sys-col-cat" });
        s_total_bytes_lbl = ca_text(&(Ca_TextDesc){ .text = "0 B", .style = "sys-col-bytes-wide" });
        ca_div_end();
        s_total_bar = ca_progress(&(Ca_ProgressDesc){
            .value = 0.0f, .height = 3.0f,
            .bar_color = 0xFF6E8AFFu, .style = "sys-mini-bar",
        });
    }
    ca_div_end();

    /* ── Column header ─────────────────────────────────────────── */
    ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "sys-row-header" });
    ca_text(&(Ca_TextDesc){ .text = "CATEGORY", .style = "sys-col-cat"   });
    ca_text(&(Ca_TextDesc){ .text = "LIVE",     .style = "sys-col-bytes" });
    ca_text(&(Ca_TextDesc){ .text = "ALLOCS",   .style = "sys-col-cnt"   });
    ca_div_end();

    ca_hr(&(Ca_HrDesc){ .color = 0xFF1E2030u });

    /* ── Per-tag rows ─────────────────────────────────────────── */
    for (int t = 0; t < QS_MEM_TAG_COUNT; t++) {
        const TagMeta *m = &k_meta[t];
        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_VERTICAL,
            .style     = (t % 2 == 0) ? "sys-row" : "sys-row sys-row-alt",
        });
        {
            ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "sys-row-top" });
            {
                char icon_label[64];
                snprintf(icon_label, sizeof(icon_label), "%s  %s", m->icon, m->label);
                ca_text(&(Ca_TextDesc){ .text = icon_label, .style = "sys-col-cat" });
                s_rows[t].bytes_lbl = ca_text(&(Ca_TextDesc){ .text = "0 B", .style = "sys-col-bytes" });
                s_rows[t].alloc_lbl = ca_text(&(Ca_TextDesc){ .text = "0",   .style = "sys-col-cnt"   });
            }
            ca_div_end();
            s_rows[t].bar = ca_progress(&(Ca_ProgressDesc){
                .value = 0.0f, .height = 2.0f,
                .bar_color = m->bar_color, .style = "sys-mini-bar",
            });
        }
        ca_div_end();
    }

    ca_spacer(&(Ca_SpacerDesc){ .height = 6.0f });
    ca_div_end(); /* sys-scroll */
}

/* ================================================================
   UPDATE (called every frame)
   ================================================================ */

void ed_system_panel_update(Qs_Engine *engine)
{
    char buf[64];
    char cnt_buf[32];

    /* ---- GPU stats ---- */
    {
        Qs_GpuContext *gpu = engine ? qs_engine_gpu(engine) : NULL;
        Qs_GpuMemStats gs = {0};
        qs_gpu_mem_stats(gpu, &gs);

        /* Device name — only query once (it doesn't change) */
        static bool s_name_fetched = false;
        if (!s_name_fetched && gpu) {
            char name[256];
            qs_gpu_device_name(gpu, name, sizeof(name));
            ca_set_text(s_gpu_name_lbl, name);
            s_name_fetched = true;
        }

        /* VRAM label: "used / total" or just "used" if total unknown */
        if (gs.device_total_bytes > 0) {
            char used[32], total[32];
            fmt_bytes(used,  sizeof(used),  gs.device_bytes);
            fmt_bytes(total, sizeof(total), gs.device_total_bytes);
            snprintf(buf, sizeof(buf), "%s / %s", used, total);
        } else {
            fmt_bytes(buf, sizeof(buf), gs.device_bytes);
        }
        ca_set_text(s_gpu_device_lbl, buf);

        float dev_frac = (gs.device_total_bytes > 0)
            ? (float)((double)gs.device_bytes / (double)gs.device_total_bytes)
            : 0.0f;
        if (dev_frac > 1.0f) dev_frac = 1.0f;
        ca_progress_set(s_gpu_device_bar, dev_frac);

        fmt_bytes(buf, sizeof(buf), gs.host_bytes);
        ca_set_text(s_gpu_host_lbl, buf);

        /* Host bar: relative to 1 GB soft cap */
        float host_frac = (float)((double)gs.host_bytes / (double)(1024ULL * 1024 * 1024));
        if (host_frac > 1.0f) host_frac = 1.0f;
        ca_progress_set(s_gpu_host_bar, host_frac);

        /* Per-purpose breakdown */
        for (int t = 0; t < QS_GPU_MEM_TAG_COUNT; t++) {
            fmt_bytes(buf, sizeof(buf), gs.tag_bytes[t]);
            ca_set_text(s_gpu_tag_lbl[t], buf);
        }
    }

    /* ---- CPU heap stats ---- */
    Qs_MemStats stats[QS_MEM_TAG_COUNT];
    size_t tag_bytes[QS_MEM_TAG_COUNT];
    size_t total = 0;
    size_t total_allocs = 0;

    for (int t = 0; t < QS_MEM_TAG_COUNT; t++) {
        stats[t]      = qs_mem_stats((Qs_MemTag)t);
        tag_bytes[t]  = stats[t].bytes_allocated;
        total        += tag_bytes[t];
        total_allocs += stats[t].allocation_count;
    }

    size_t max_tag = 1;
    for (int t = 0; t < QS_MEM_TAG_COUNT; t++)
        if (tag_bytes[t] > max_tag) max_tag = tag_bytes[t];

    fmt_bytes(buf, sizeof(buf), total);
    ca_set_text(s_total_bytes_lbl, buf);

    fmt_count(cnt_buf, sizeof(cnt_buf), total_allocs);
    char alloc_label[48];
    snprintf(alloc_label, sizeof(alloc_label), "%s allocs", cnt_buf);
    ca_set_text(s_total_allocs_lbl, alloc_label);

    const size_t SOFT_CAP = 256ULL * 1024 * 1024;
    float total_frac = (float)((double)total / (double)SOFT_CAP);
    if (total_frac > 1.0f) total_frac = 1.0f;
    ca_progress_set(s_total_bar, total_frac);

    for (int t = 0; t < QS_MEM_TAG_COUNT; t++) {
        fmt_bytes(buf, sizeof(buf), tag_bytes[t]);
        ca_set_text(s_rows[t].bytes_lbl, buf);

        fmt_count(cnt_buf, sizeof(cnt_buf), stats[t].allocation_count);
        ca_set_text(s_rows[t].alloc_lbl, cnt_buf);

        float frac = (float)((double)tag_bytes[t] / (double)max_tag);
        ca_progress_set(s_rows[t].bar, frac);
    }
}

