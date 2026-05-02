/*
 * ed_import_dialog.c — Asset import preview dialog (reactive UI).
 *
 *   1. File→Import Asset opens the file browser; a picked path
 *      enters ed_import_dialog_open().
 *   2. The matching Qs_AssetImporterExt runs synchronously to
 *      produce a Qs_ImportResult on the UI thread (importers must
 *      be cheap enough — a few ms for a typical glTF parse).
 *   3. Causality window is created; the body div is BUILT ONCE
 *      with checkboxes for each mesh / material / texture plus
 *      per-item flags (optimize / sRGB / mips) and Cancel/Import
 *      buttons.  Checkbox state is mutated via on_change → static
 *      flag arrays — no per-frame rebuild.
 *   4. Clicking Import packs the flag arrays into Qs_*ImportOpts
 *      and dispatches qs_asset_cook on the engine job system.
 *      The dialog body is reconciled to a "Importing..." view.
 *      A window on_frame callback polls the atomic done flag
 *      and closes the window when finished.
 *   5. ca_window_close() (NOT _destroy) is used for teardown so
 *      Vulkan resources are freed by the event loop, not mid-frame.
 */

#include "ed_import_dialog.h"
#include "quasar.h"
#include "../ed_style.h"
#include "qs_asset.h"
#include "qs_asset_pack.h"
#include "qs_job.h"
#include "qs_log.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_ITEMS 1024

/* ---- Module state -------------------------------------------------- */

static Editor   *s_editor;
static Ca_Window *s_win;
static Ca_Div    *s_body;            /* reconcilable container */
static char       s_source_path[1024];
static char       s_asset_name[128];

static Qs_ImportResult s_result;
static bool       s_has_result;

/* Global cook flags (checkboxes in the dialog footer).  Per-item
   toggling would blow Causality's checkbox pool on big assets like
   Sponza and is rarely useful in practice — Unity / Unreal-style
   importers expose only global flags. */
static bool s_g_optim_meshes = true;
static bool s_g_gen_mips     = true;
static bool s_g_srgb_albedo  = true;

typedef struct GlobalToggleCtx { bool *flag; } GlobalToggleCtx;
static GlobalToggleCtx s_tg_optim = { &s_g_optim_meshes };
static GlobalToggleCtx s_tg_mips  = { &s_g_gen_mips };
static GlobalToggleCtx s_tg_srgb  = { &s_g_srgb_albedo };

/* ---- Async cook state --------------------------------------------- */

typedef struct CookJob {
    Qs_ImportResult    result;
    Qs_TexImportOpts  *tex_opts;
    Qs_MatImportOpts  *mat_opts;
    Qs_MeshImportOpts *mesh_opts;
    Qs_CookOptions     opts;
    char               qproto[1024];
    char               asset_name[128];
    char               out_dir[1024];
    bool               ok;
    atomic_int         done;
} CookJob;

static CookJob *s_cook_job;          /* heap-owned while running */
static bool      s_pending_progress;  /* defer body swap to next frame */

/* ---- Helpers ------------------------------------------------------- */

static const Qs_AssetImporterExt *
find_importer(Qs_Engine *engine, const char *path, void **out_data)
{
    if (!engine || !path) return NULL;
    const char *dot = strrchr(path, '.');
    if (!dot) return NULL;

    uint32_t n = qs_engine_ext_count(engine, QS_EXT_ASSET_IMPORTER);
    for (uint32_t i = 0; i < n; i++) {
        const Qs_AssetImporterExt *ext =
            qs_engine_ext_interface(engine, QS_EXT_ASSET_IMPORTER, i);
        if (!ext || !ext->extensions || !ext->import) continue;
        if (strstr(ext->extensions, dot)) {
            if (out_data) *out_data = qs_engine_ext_data(engine,
                                                         QS_EXT_ASSET_IMPORTER, i);
            return ext;
        }
    }
    return NULL;
}

static void derive_asset_name(const char *path, char *out, size_t out_size)
{
    const char *base = strrchr(path, '/');
    const char *bsla = strrchr(path, '\\');
    if (bsla > base) base = bsla;
    base = base ? base + 1 : path;
    snprintf(out, out_size, "%s", base);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

static void close_dialog(void)
{
    if (s_win && ca_window_is_open(s_win))
        ca_window_close(s_win);
    s_win  = NULL;
    s_body = NULL;
    if (s_has_result) {
        qs_import_result_free(&s_result);
        memset(&s_result, 0, sizeof(s_result));
        s_has_result = false;
    }
}

/* ---- Toggle callbacks --------------------------------------------- */

static void on_global_toggle(Ca_Checkbox *cb, void *user_data)
{
    GlobalToggleCtx *t = (GlobalToggleCtx *)user_data;
    if (t && t->flag) *t->flag = ca_checkbox_get(cb);
}

/* ---- Body-content builders (called with s_body on the stack as parent) -- */

static void build_preview_children(void);

static void build_progress_children(void)
{
    ca_text(&(Ca_TextDesc){
        .text  = "Importing... please wait.",
        .style = "import-progress",
    });
}

/* ---- Async cook --------------------------------------------------- */

static void cook_job_fn(void *data)
{
    CookJob *cj = (CookJob *)data;
    cj->ok = qs_asset_cook(&cj->result, &cj->opts, cj->qproto, sizeof(cj->qproto));
    atomic_store(&cj->done, 1);
}

static void on_progress_frame(void *user_data)
{
    (void)user_data;

    /* Deferred body swap: must run inside ca_widget_ctx_enter/leave
       (which Causality wraps around on_frame callbacks). */
    if (s_pending_progress && s_body) {
        ca_reconcile_begin(s_body);
        build_progress_children();
        ca_div_end();
        s_pending_progress = false;
    }

    if (!s_cook_job) return;
    if (atomic_load(&s_cook_job->done) == 0) return;

    if (s_cook_job->ok)
        QS_LOG_INFO("Imported %s -> %s", s_source_path, s_cook_job->qproto);
    else
        QS_LOG_ERROR("Import failed for %s", s_source_path);

    qs_import_result_free(&s_cook_job->result);
    free(s_cook_job->tex_opts);
    free(s_cook_job->mat_opts);
    free(s_cook_job->mesh_opts);
    free(s_cook_job);
    s_cook_job = NULL;

    /* Detach poll callback before close to avoid re-entry. */
    if (s_win) ca_window_set_on_frame(s_win, NULL, NULL);

    /* Result was moved into the job; clear staging copy without freeing. */
    memset(&s_result, 0, sizeof(s_result));
    s_has_result = false;
    close_dialog();
}

static void start_cook_job(void)
{
    if (!s_has_result || !s_editor) return;
    Qs_Project *project = editor_project(s_editor);
    Qs_Engine  *engine  = editor_engine(s_editor);
    if (!project || !engine) {
        QS_LOG_ERROR("Import: no active project");
        return;
    }

    CookJob *cj = (CookJob *)calloc(1, sizeof(CookJob));
    if (!cj) return;

    /* Move the import result into the job (transfer ownership). */
    cj->result = s_result;
    memset(&s_result, 0, sizeof(s_result));
    s_has_result = false;

    if (cj->result.texture_count) {
        cj->tex_opts = (Qs_TexImportOpts *)calloc(cj->result.texture_count,
                                                  sizeof(*cj->tex_opts));
        for (uint32_t i = 0; i < cj->result.texture_count; i++) {
            cj->tex_opts[i].include       = true;
            /* Default to importer-suggested sRGB; if the user disabled
               sRGB-on-albedo globally, flip albedo textures to linear. */
            bool importer_srgb = cj->result.textures[i].srgb;
            cj->tex_opts[i].srgb          = importer_srgb && s_g_srgb_albedo;
            cj->tex_opts[i].generate_mips = s_g_gen_mips;
        }
    }
    if (cj->result.material_count) {
        cj->mat_opts = (Qs_MatImportOpts *)calloc(cj->result.material_count,
                                                  sizeof(*cj->mat_opts));
        for (uint32_t i = 0; i < cj->result.material_count; i++)
            cj->mat_opts[i].include = true;
    }
    if (cj->result.mesh_count) {
        cj->mesh_opts = (Qs_MeshImportOpts *)calloc(cj->result.mesh_count,
                                                    sizeof(*cj->mesh_opts));
        for (uint32_t i = 0; i < cj->result.mesh_count; i++) {
            cj->mesh_opts[i].include  = true;
            cj->mesh_opts[i].optimize = s_g_optim_meshes;
        }
    }

    snprintf(cj->asset_name, sizeof(cj->asset_name), "%s", s_asset_name);
    snprintf(cj->out_dir,    sizeof(cj->out_dir),    "%s/Assets/%s",
             qs_project_path(project), cj->asset_name);

    cj->opts = (Qs_CookOptions){
        .project    = project,
        .out_dir    = cj->out_dir,
        .asset_name = cj->asset_name,
        .tex_opts   = cj->tex_opts,
        .mat_opts   = cj->mat_opts,
        .mesh_opts  = cj->mesh_opts,
    };

    atomic_init(&cj->done, 0);
    s_cook_job = cj;

    /* Defer the body view-swap to the next on_frame tick where the
       widget context is active. */
    s_pending_progress = true;
    ca_window_set_on_frame(s_win, on_progress_frame, NULL);

    qs_job_dispatch(qs_engine_job_system(engine), &(Qs_JobDesc){
        .fn   = cook_job_fn,
        .data = cj,
    }, NULL);
}

/* ---- Button callbacks --------------------------------------------- */

static void on_import_click(Ca_Button *btn, void *user_data)
{
    (void)btn; (void)user_data;
    start_cook_job();
}

static void on_cancel_click(Ca_Button *btn, void *user_data)
{
    (void)btn; (void)user_data;
    close_dialog();
}

/* ---- Preview body content (children of s_body) ------------------- */

static void build_item_column(const char *title,
                              uint32_t count,
                              const char *(*name_at)(uint32_t),
                              const char *col_id)
{
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "%s (%u)", title, count);

    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .id        = col_id,
        .style     = "import-col",
    });
    {
        ca_text(&(Ca_TextDesc){
            .text  = hdr,
            .style = "import-col-title",
        });
        for (uint32_t i = 0; i < count && i < MAX_ITEMS; i++) {
            const char *name = name_at(i);
            ca_text(&(Ca_TextDesc){
                .text  = (name && name[0]) ? name : "(unnamed)",
                .style = "import-item",
            });
        }
    }
    ca_div_end();
}

static const char *mesh_name_at(uint32_t i)
    { return s_result.meshes[i].name; }
static const char *mat_name_at(uint32_t i)
    { return s_result.materials[i].name; }
static const char *tex_name_at(uint32_t i)
    { return s_result.textures[i].name; }

static void build_preview_children(void)
{
    /* Source path header */
    ca_text(&(Ca_TextDesc){
        .text  = s_source_path,
        .style = "import-source",
    });

    /* Three-column grid (text-only lists) */
    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .style     = "import-cols",
    });
    {
        build_item_column("Meshes",    s_result.mesh_count,
                          mesh_name_at, "imp-col-meshes");
        build_item_column("Materials", s_result.material_count,
                          mat_name_at,  "imp-col-materials");
        build_item_column("Textures",  s_result.texture_count,
                          tex_name_at,  "imp-col-textures");
    }
    ca_div_end();

    /* Global cook flags */
    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .style     = "import-flags",
    });
    {
        ca_checkbox(&(Ca_CheckboxDesc){
            .text        = "Optimize meshes",
            .checked     = s_g_optim_meshes,
            .id          = "imp-flag-optim",
            .style       = "import-cb",
            .on_change   = on_global_toggle,
            .change_data = &s_tg_optim,
        });
        ca_checkbox(&(Ca_CheckboxDesc){
            .text        = "Generate mips",
            .checked     = s_g_gen_mips,
            .id          = "imp-flag-mips",
            .style       = "import-cb",
            .on_change   = on_global_toggle,
            .change_data = &s_tg_mips,
        });
        ca_checkbox(&(Ca_CheckboxDesc){
            .text        = "sRGB albedo",
            .checked     = s_g_srgb_albedo,
            .id          = "imp-flag-srgb",
            .style       = "import-cb",
            .on_change   = on_global_toggle,
            .change_data = &s_tg_srgb,
        });
    }
    ca_div_end();

    /* Footer */
    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .style     = "import-footer",
    });
    {
        ca_btn_begin(&(Ca_BtnDesc){
            .text     = "Cancel",
            .id       = "imp-cancel-btn",
            .style    = "import-btn",
            .on_click = on_cancel_click,
        });
        ca_btn_end();
        ca_btn_begin(&(Ca_BtnDesc){
            .text     = "Import",
            .id       = "imp-import-btn",
            .style    = "import-btn import-btn-primary",
            .on_click = on_import_click,
        });
        ca_btn_end();
    }
    ca_div_end();
}

/* ---- Public API ---------------------------------------------------- */

void ed_import_dialog_init(Editor *editor)
{
    s_editor = editor;
}

void ed_import_dialog_open(const char *source_path)
{
    if (!s_editor || !source_path) return;
    if (s_win && ca_window_is_open(s_win)) return;

    Qs_Engine *engine = editor_engine(s_editor);
    if (!engine) return;

    /* Find a matching importer */
    void *importer_data = NULL;
    const Qs_AssetImporterExt *imp = find_importer(engine, source_path, &importer_data);
    if (!imp) {
        QS_LOG_ERROR("No importer for: %s", source_path);
        return;
    }

    /* Reset per-import state */
    if (s_has_result) qs_import_result_free(&s_result);
    memset(&s_result, 0, sizeof(s_result));

    snprintf(s_source_path, sizeof(s_source_path), "%s", source_path);
    derive_asset_name(source_path, s_asset_name, sizeof(s_asset_name));

    /* Run importer synchronously (cheap CPU parse). */
    if (!imp->import(importer_data, source_path, &s_result)) {
        QS_LOG_ERROR("Importer failed for: %s", source_path);
        return;
    }
    s_has_result = true;

    /* Create window */
    Ca_Window *main_win = qs_engine_window(engine);
    if (!main_win) return;
    Ca_Instance *inst = ca_window_instance(main_win);
    if (!inst) return;

    s_win = ca_window_create(inst, &(Ca_WindowDesc){
        .title  = "Import Asset",
        .width  = 720,
        .height = 480,
    });
    if (!s_win) {
        qs_import_result_free(&s_result);
        memset(&s_result, 0, sizeof(s_result));
        s_has_result = false;
        return;
    }

    ca_window_set_scale(s_win, ED_UI_SCALE);
    s_pending_progress = false;

    /* Build static shell + initial preview content in one pass. */
    ca_ui_begin(s_win, &(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "import-root",
    });
    {
        s_body = ca_div_begin(&(Ca_DivDesc){
            .direction = CA_VERTICAL,
            .style     = "import-body",
        });
        build_preview_children();
        ca_div_end();
    }
    ca_ui_end();
}
