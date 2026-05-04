#include "ed_inspector.h"
#include "editor.h"
#include "ed_layout.h"
#include "ed_commands.h"
#include "ca_theme.h"
#include "qs_asset_pack.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
   INSPECTOR PANEL — dynamic component property inspector
   ================================================================

   Builds a static shell once (scroll, header, placeholder).
   Component sections and field widgets are created on-demand when
   the entity selection changes — old content is torn down via
   ca_div_clear() + ca_div_end() for teardown/rebuild.  No fixed
   pool limits; scales to any number of components and fields.
   ================================================================ */

#define MAX_VEC_ELEMS 4

/* ---- Binding data for on_change callbacks ---- */

typedef struct InputBinding {
    Qs_ComponentType *comp_type;
    size_t            field_offset;
    size_t            field_size;
    uint32_t          vec_index;
    Qs_FieldType      field_type;
    Ca_TextInput     *widget;       /* NULL for checkbox / non-input fields */
    /* Stable pointers into static reflection tables — used to route edits
       through the prototype-override system when proto context is active. */
    const char       *comp_name;
    const char       *field_name;
    /* ---- Undo focus tracking ----
       Text edits land via on_change per keystroke, so we don't push an
       undo entry on every key.  Instead we snapshot the field's bytes
       on focus-gain into `before_buf`, then on focus-loss compare with
       the current value and push a single command for the whole edit. */
    bool              was_focused;
    bool              proto_had_before; ///< Override existed at focus-gain.
    uint8_t           before_buf[256];
    uint8_t           proto_before_buf[256];
    /* ---- Material PBR param editing (comp_type == NULL when active) ---- */
    bool              is_mat_param;
    Qs_Material      *mat;              ///< Target material (non-NULL when is_mat_param).
    uint32_t          mat_param_offset; ///< Byte offset into Qs_PBRParams.
    uint32_t          mat_vec_index;    ///< Element index within a float vector field.
} InputBinding;

/* Resolves the source scene + entity for the inspector to display.  When
   the editor has an active prototype-instance selection (the user clicked
   an inner entity in the hierarchy), reads come from the inner scene
   rather than the outer scene. */
static Qs_Scene *inspect_source_scene(Editor *ed)
{
    if (!ed) return qs_scene_active();
    Qs_Scene *inner = editor_proto_inner_scene(ed);
    return inner ? inner : qs_scene_active();
}

/* Returns the outer-scene PrototypeComp that should receive overrides for
   the currently selected inner entity, or NULL when not editing into a
   prototype instance. */
static Qs_PrototypeComp *active_override_target(Editor *ed)
{
    if (!ed) return NULL;
    Qs_Entity owner = editor_proto_owner(ed);
    if (owner == QS_ENTITY_INVALID) return NULL;
    Qs_Scene *outer = qs_scene_active();
    if (!outer) return NULL;
    return (Qs_PrototypeComp *)qs_entity_get(
        outer, owner, qs_prototype_comp_type());
}

/* ---- Module state ---- */

static Editor       *s_editor;
static Ca_Label     *s_no_selection;
static Ca_Div       *s_header_div;
static Ca_TextInput *s_entity_name_input;
static Ca_Label     *s_id_value;
static Ca_TextInput *s_tag_input;
static Ca_Div       *s_content_div;      /* dynamic section container */
static Qs_Entity     s_displayed_entity = QS_ENTITY_INVALID;
static Qs_Scene     *s_displayed_scene  = NULL;

/* Heap-allocated bindings — freed + rebuilt on each entity change */
static InputBinding *s_bindings;
static uint32_t      s_binding_count;
static uint32_t      s_binding_cap;

/* ---- Header text input focus tracking for undo ---- */
static bool s_name_was_focused;
static char s_name_before[128];
static bool s_tag_was_focused;
static char s_tag_before[128];

/* ---- Axis label text and CSS classes ---- */

static const char *s_axis_text[4]  = { "X", "Y", "Z", "W" };
static const char *s_axis_style[4] = {
    "inspector-axis-x", "inspector-axis-y",
    "inspector-axis-z", "inspector-axis-w"
};

/* ================================================================
   MATERIAL EDITOR STATE
   ================================================================ */

/* Material option list for the material-selector dropdown.
   s_mat_paths — project-relative path to each .qsmat.
   Materials are loaded on demand (not preloaded) to avoid holding refs. */
static const char  **s_mat_options      = NULL;
static char        **s_mat_paths        = NULL;
static char         *s_mat_name_pool    = NULL;
static uint32_t      s_mat_pool_used    = 0;
static uint32_t      s_mat_pool_cap     = 0;
static uint32_t      s_mat_option_count = 0;
static uint32_t      s_mat_option_cap   = 0;

/* Texture option list — stores project-relative paths to .qstex files.
   Textures are loaded on demand (not preloaded) to avoid mass GPU uploads. */
static const char  **s_tex_options      = NULL;
static char        **s_tex_list         = NULL;   /* project-relative paths */
static uint32_t      s_tex_option_count = 0;
static uint32_t      s_tex_option_cap   = 0;

/* Mesh option list — stores project-relative paths to .qsmesh files. */
static const char  **s_mesh_options      = NULL;
static char        **s_mesh_paths        = NULL;   /* project-relative paths */
static uint32_t      s_mesh_option_count = 0;
static uint32_t      s_mesh_option_cap   = 0;

/* Per-slot context for on_texture_select (one entry per texture slot) */
#define QS_MAT_SLOTS 5
typedef struct { Qs_Material *mat; uint32_t slot; } TexPickCtx;
static TexPickCtx s_tex_ctxs[QS_MAT_SLOTS];

/* ---- Binding allocator ---- */

static InputBinding *alloc_binding(void)
{
    if (s_binding_count == s_binding_cap) {
        uint32_t new_cap = s_binding_cap ? s_binding_cap * 2 : 32;
        InputBinding *tmp = realloc(s_bindings, new_cap * sizeof(InputBinding));
        if (!tmp) return NULL;
        s_bindings    = tmp;
        s_binding_cap = new_cap;
    }
    InputBinding *b = &s_bindings[s_binding_count++];
    memset(b, 0, sizeof(*b));
    return b;
}

static void free_bindings(void)
{
    free(s_bindings);
    s_bindings     = NULL;
    s_binding_count = 0;
    s_binding_cap   = 0;
}

/* ---- Icon helpers ---- */

static const char *component_icon(const char *name)
{
    if (strcmp(name, "Transform") == 0)  return ICON_TRANSFORM;
    if (strcmp(name, "MeshComp") == 0)   return ICON_MESH;
    if (strcmp(name, "LightComp") == 0)  return ICON_LIGHT;
    if (strcmp(name, "Prototype") == 0)  return ICON_PROTOTYPE;
    return ICON_COMPONENT;
}

static uint32_t component_icon_color(const char *name)
{
    if (strcmp(name, "Transform") == 0)  return CA_THEME_ACCENT;
    if (strcmp(name, "MeshComp")  == 0)  return CA_THEME_SUCCESS;
    if (strcmp(name, "LightComp") == 0)  return CA_THEME_WARNING;
    if (strcmp(name, "Prototype") == 0)  return CA_THEME_ACCENT;
    return CA_THEME_TEXT_MUTED;
}

/* ================================================================
   INPUT CALLBACKS — write edited values back to component data
   ================================================================ */

static void on_field_input(Ca_TextInput *input, void *user_data)
{
    InputBinding *b = (InputBinding *)user_data;
    if (!b) return;

    /* ---- Material PBR param editing ---- */
    if (b->is_mat_param) {
        if (!b->mat) return;
        const char *text = ca_get_text(input);
        const Qs_PBRParams *p = qs_material_params(b->mat);
        if (!p) return;
        Qs_PBRParams copy = *p;
        float *field = (float *)((char *)&copy + b->mat_param_offset);
        field[b->mat_vec_index] = text ? strtof(text, NULL) : 0.0f;
        qs_material_update_params(b->mat, &copy);
        return;
    }

    if (!s_editor || !b->comp_type) return;

    Qs_Scene *scene  = inspect_source_scene(s_editor);
    Qs_Entity entity = editor_selected_entity(s_editor);
    if (!scene || entity == QS_ENTITY_INVALID) return;

    void *comp = qs_entity_get(scene, entity, b->comp_type);
    if (!comp) return;

    const char *text = ca_get_text(input);
    void *field_ptr = (char *)comp + b->field_offset;

    switch (b->field_type) {
    case QS_FIELD_FLOAT:
    case QS_FIELD_FLOAT2:
    case QS_FIELD_FLOAT3:
    case QS_FIELD_FLOAT4:
        ((float *)field_ptr)[b->vec_index] = text ? strtof(text, NULL) : 0.0f;
        break;
    case QS_FIELD_INT32:
        *(int32_t *)field_ptr = text ? (int32_t)strtol(text, NULL, 10) : 0;
        break;
    case QS_FIELD_UINT32:
        *(uint32_t *)field_ptr = text ? (uint32_t)strtoul(text, NULL, 10) : 0;
        break;
    case QS_FIELD_STRING:
        snprintf((char *)field_ptr, b->field_size, "%s", text ? text : "");
        break;
    default: break;
    }

    /* If editing into a prototype instance, persist the change as an
       override on the outer-scene PrototypeComp so it survives reloads
       without modifying the source .qproto. */
    Qs_PrototypeComp *pc = active_override_target(s_editor);
    if (pc && b->comp_name && b->field_name) {
        Qs_IdComp *idc = (Qs_IdComp *)qs_entity_get(
            scene, entity, qs_id_comp_type());
        if (idc) {
            qs_prototype_set_override(
                pc, idc->id, b->comp_name, b->field_name,
                b->field_type, field_ptr);
        }
    }
}

static void on_bool_input(Ca_Checkbox *cb, void *user_data)
{
    InputBinding *b = (InputBinding *)user_data;
    if (!s_editor || !b->comp_type) return;

    Qs_Scene *scene  = inspect_source_scene(s_editor);
    Qs_Entity entity = editor_selected_entity(s_editor);
    if (!scene || entity == QS_ENTITY_INVALID) return;

    void *comp = qs_entity_get(scene, entity, b->comp_type);
    if (!comp) return;

    bool *dst = (bool *)((char *)comp + b->field_offset);
    bool before = *dst;
    bool after  = ca_checkbox_get(cb);
    *dst = after;

    Qs_PrototypeComp *pc = active_override_target(s_editor);
    if (pc && b->comp_name && b->field_name) {
        Qs_IdComp *idc = (Qs_IdComp *)qs_entity_get(
            scene, entity, qs_id_comp_type());
        if (idc) {
            /* Capture pre-edit override state for undo. */
            const Qs_PrototypeOverride *prev = qs_prototype_find_override(
                pc, idc->id, b->comp_name, b->field_name);
            bool had_before = prev != NULL;
            uint8_t before_buf[256];
            if (had_before) memcpy(before_buf, &prev->value, sizeof(prev->value));

            qs_prototype_set_override(
                pc, idc->id, b->comp_name, b->field_name,
                QS_FIELD_BOOL, dst);

            const Qs_PrototypeOverride *now = qs_prototype_find_override(
                pc, idc->id, b->comp_name, b->field_name);
            ed_undo_push_override(pc, idc->id, b->comp_name, b->field_name,
                                  QS_FIELD_BOOL,
                                  had_before, before_buf,
                                  now == NULL, now ? &now->value : NULL);
        }
    } else if (before != after) {
        ed_undo_push_field(scene, entity, b->comp_type,
                           b->field_offset, b->field_size,
                           &before, &after);
    }
}

/* ---- Prototype path dropdown -------------------------------------- */

/* Two parallel option buffers:
     s_proto_options : pretty display text shown in the dropdown
                       (basename without extension)
     s_proto_paths   : the matching project-relative paths, written
                       into PrototypeComp.path on selection.
   Index 0 is always "(none)" / "" in both arrays.  Refreshed on each
   build_field("Prototype","path"). */
static const char **s_proto_options;
static const char **s_proto_paths;
static char        *s_proto_display_pool;   /* backing storage for basenames */
static uint32_t     s_proto_display_pool_used;
static uint32_t     s_proto_display_pool_cap;
static uint32_t     s_proto_option_count;
static uint32_t     s_proto_option_cap;

static const char *push_basename(const char *path)
{
    /* Returns a stable pointer into s_proto_display_pool containing the
       file basename of `path` with its extension stripped. */
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    size_t len = strlen(base);
    /* Trim extension */
    const char *dot = strrchr(base, '.');
    if (dot) len = (size_t)(dot - base);

    if (s_proto_display_pool_used + len + 1 > s_proto_display_pool_cap) {
        uint32_t cap = s_proto_display_pool_cap ? s_proto_display_pool_cap * 2 : 256;
        while (cap < s_proto_display_pool_used + len + 1) cap *= 2;
        char *tmp = (char *)realloc(s_proto_display_pool, cap);
        if (!tmp) return path; /* fallback: use raw path */
        s_proto_display_pool      = tmp;
        s_proto_display_pool_cap  = cap;
    }
    char *out = s_proto_display_pool + s_proto_display_pool_used;
    memcpy(out, base, len);
    out[len] = '\0';
    s_proto_display_pool_used += (uint32_t)(len + 1);
    return out;
}

static void refresh_proto_options(void)
{
    Qs_Project *proj = editor_project(s_editor);
    s_proto_option_count       = 0;
    s_proto_display_pool_used  = 0;
    if (!proj) return;
    uint32_t n = qs_project_prototype_count(proj);
    if (n + 1 > s_proto_option_cap) {
        uint32_t cap = (n + 1 < 16) ? 16 : (n + 1);
        const char **a = (const char **)realloc(s_proto_options, cap * sizeof(*a));
        const char **b = (const char **)realloc(s_proto_paths,   cap * sizeof(*b));
        if (!a || !b) { free(a); free(b); return; }
        s_proto_options    = a;
        s_proto_paths      = b;
        s_proto_option_cap = cap;
    }
    s_proto_options[s_proto_option_count] = "(none)";
    s_proto_paths  [s_proto_option_count] = "";
    s_proto_option_count++;
    for (uint32_t i = 0; i < n; i++) {
        const char *p = qs_project_prototype_path(proj, i);
        if (!p) continue;
        s_proto_options[s_proto_option_count] = push_basename(p);
        s_proto_paths  [s_proto_option_count] = p;
        s_proto_option_count++;
    }
}

static void on_proto_path_select(Ca_Select *sel, void *user_data)
{
    (void)user_data;
    if (!s_editor || !sel) return;
    Qs_Scene *scene = qs_scene_active();
    Qs_Entity entity = editor_selected_entity(s_editor);
    if (!scene || entity == QS_ENTITY_INVALID) return;

    Qs_PrototypeComp *pc = (Qs_PrototypeComp *)qs_entity_get(
        scene, entity, qs_prototype_comp_type());
    if (!pc) return;

    int idx = ca_select_get(sel);
    const char *new_path = (idx > 0 && (uint32_t)idx < s_proto_option_count)
                               ? s_proto_paths[idx] : "";

    /* Cycle guard: when editing inside a .qproto, refuse to assign a
       prototype path that would (directly or transitively) embed the
       currently-open prototype.  Without this check the runtime would
       lazy-load the inner scene recursively and exhaust the editor's
       prototype edit stack. */
    if (new_path && *new_path && editor_mode(s_editor) == ED_MODE_PROTOTYPE) {
        const char *host = editor_current_proto_path(s_editor);
        if (host && *host &&
            qs_prototype_would_create_cycle(editor_project(s_editor),
                                            host, new_path))
        {
            QS_LOG_ERROR("Refusing prototype assignment '%s': would create cyclic reference (host '%s')",
                         new_path, host);
            /* Don't apply the selection — snap the dropdown back to
               "(none)" so the rejected pick is not visually retained. */
            ca_select_set(sel, 0);
            return;
        }
    }

    if (idx <= 0) {
        pc->path[0] = '\0';
    } else if ((uint32_t)idx < s_proto_option_count) {
        snprintf(pc->path, sizeof(pc->path), "%s", new_path);
    }

    /* Force lazy reload at next render */
    if (pc->inner) {
        qs_scene_destroy(pc->inner);
        pc->inner = NULL;
    }
    pc->load_failed = false;
}

static void on_entity_name_input(Ca_TextInput *input, void *user_data)
{
    (void)user_data;
    if (!s_editor) return;

    Qs_Scene *scene  = inspect_source_scene(s_editor);
    Qs_Entity entity = editor_selected_entity(s_editor);
    if (!scene || entity == QS_ENTITY_INVALID) return;

    /* Names are not part of the override system — they live in the inner
       scene's metadata.  Edits while in proto-instance mode therefore
       mutate the loaded inner scene directly; they do not persist
       across reloads of the prototype.  This matches how Unity treats
       prefab-instance hierarchy renames as session-only. */
    const char *text = ca_get_text(input);
    qs_entity_set_name(scene, entity, text ? text : "");
}

static void on_tag_input(Ca_TextInput *input, void *user_data)
{
    (void)user_data;
    if (!s_editor) return;

    Qs_Scene *scene  = inspect_source_scene(s_editor);
    Qs_Entity entity = editor_selected_entity(s_editor);
    if (!scene || entity == QS_ENTITY_INVALID) return;

    Qs_TagComp *tag = (Qs_TagComp *)qs_entity_get(
                          scene, entity, qs_tag_comp_type());
    if (!tag) return;

    const char *text = ca_get_text(input);
    snprintf(tag->tag, sizeof(tag->tag), "%s", text ? text : "");
}

/* ================================================================
   BUILD (called once during editor init)
   ================================================================ */

void ed_inspector(void *editor)
{
    s_editor = (Editor *)editor;

    /* Scrollable container */
    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "inspector-scroll",
        .id        = "inspector-scroll",
    });
    {
        /* "No entity selected" placeholder */
        s_no_selection = ca_text(&(Ca_TextDesc){
            .text  = "No entity selected",
            .style = "inspector-placeholder",
        });

        /* ---- Entity header ---- */
        s_header_div = ca_div_begin(&(Ca_DivDesc){
            .direction = CA_VERTICAL,
            .style     = "inspector-header",
            .hidden    = true,
        });
        {
            /* ID row — shown first */
            s_id_value = ca_text(&(Ca_TextDesc){
                .text  = ICON_ID "  0",
                .style = "inspector-id-label",
            });

            /* Entity name */
            s_entity_name_input = ca_input(&(Ca_InputDesc){
                .text      = "",
                .style     = "inspector-entity-input",
                .on_change = on_entity_name_input,
            });

            /* Tag row with pencil icon */
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_HORIZONTAL,
                .style     = "inspector-meta-row",
            });
            ca_text(&(Ca_TextDesc){
                .text  = ICON_TAG,
                .style = "inspector-tag-icon",
            });
            s_tag_input = ca_input(&(Ca_InputDesc){
                .text        = "",
                .placeholder = "Untagged",
                .style       = "inspector-tag-input",
                .on_change   = on_tag_input,
            });
            ca_div_end();
        }
        ca_div_end();

        /* Separator */
        ca_hr(&(Ca_HrDesc){ .color = CA_THEME_BG_SURFACE });

        /* Dynamic content container — rebuilt on entity change */
        s_content_div = ca_div_begin(&(Ca_DivDesc){
            .direction = CA_VERTICAL,
        });
        ca_div_end();
    }
    ca_div_end(); /* inspector-scroll */
}

/* ================================================================
   BUILD FIELD — create the right widget for a field type
   ================================================================ */

static void build_field(const char *comp_name, Qs_ComponentType *ct,
                        const Qs_FieldInfo *fi, const void *comp)
{
    const void *field_ptr = (const char *)comp + fi->offset;
    char buf[64];

    char row_id[96];
    snprintf(row_id, sizeof(row_id), "ins-field-%s-%s", comp_name, fi->name);

    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .id        = row_id,
        .style     = "inspector-field-row",
    });

    char name_id[96];
    snprintf(name_id, sizeof(name_id), "ins-field-name-%s-%s", comp_name, fi->name);

    /* If we are editing into a prototype instance, mark fields that have
       an active override with a leading bullet so the user can tell at a
       glance which values diverge from the source prototype. */
    bool is_overridden = false;
    if (s_editor) {
        Qs_PrototypeComp *pc = active_override_target(s_editor);
        if (pc) {
            Qs_Scene *src   = inspect_source_scene(s_editor);
            Qs_Entity sel_e = editor_selected_entity(s_editor);
            if (src && sel_e != QS_ENTITY_INVALID) {
                Qs_IdComp *idc = (Qs_IdComp *)qs_entity_get(
                    src, sel_e, qs_id_comp_type());
                if (idc) {
                    is_overridden = qs_prototype_find_override(
                        pc, idc->id, comp_name, fi->name) != NULL;
                }
            }
        }
    }

    char name_buf[96];
    if (is_overridden)
        snprintf(name_buf, sizeof(name_buf), "● %s", fi->name);
    else
        snprintf(name_buf, sizeof(name_buf), "%s", fi->name);

    ca_text(&(Ca_TextDesc){
        .text  = name_buf,
        .id    = name_id,
        .style = is_overridden
                    ? "inspector-field-name inspector-field-overridden"
                    : "inspector-field-name",
    });

    /* Special-case: Prototype.path → dropdown of registered prototypes */
    if (strcmp(comp_name, "Prototype") == 0 &&
        strcmp(fi->name, "path") == 0 &&
        fi->type == QS_FIELD_STRING)
    {
        refresh_proto_options();
        const char *cur = (const char *)field_ptr;
        int selected = 0;
        for (uint32_t i = 1; i < s_proto_option_count; i++) {
            if (cur && strcmp(cur, s_proto_paths[i]) == 0) {
                selected = (int)i;
                break;
            }
        }
        char sel_id[96];
        snprintf(sel_id, sizeof(sel_id), "ins-proto-path-select");
        ca_select(&(Ca_SelectDesc){
            .options      = s_proto_options,
            .option_count = (int)s_proto_option_count,
            .selected     = selected,
            .id           = sel_id,
            .style        = "inspector-select",
            .on_change    = on_proto_path_select,
        });
        ca_div_end();
        return;
    }

    switch (fi->type) {
    case QS_FIELD_FLOAT:
    case QS_FIELD_INT32:
    case QS_FIELD_UINT32:
    case QS_FIELD_STRING: {
        InputBinding *b = alloc_binding();
        *b = (InputBinding){
            .comp_type = ct, .field_offset = fi->offset,
            .field_size = fi->size, .field_type = fi->type,
            .comp_name = comp_name, .field_name = fi->name,
        };

        if (fi->type == QS_FIELD_FLOAT)
            snprintf(buf, sizeof(buf), "%.3f", *(const float *)field_ptr);
        else if (fi->type == QS_FIELD_INT32)
            snprintf(buf, sizeof(buf), "%d", *(const int32_t *)field_ptr);
        else if (fi->type == QS_FIELD_UINT32)
            snprintf(buf, sizeof(buf), "%u", *(const uint32_t *)field_ptr);

        char input_id[96];
        snprintf(input_id, sizeof(input_id), "ins-field-input-%s-%s", comp_name, fi->name);
        b->widget = ca_input(&(Ca_InputDesc){
            .text        = (fi->type == QS_FIELD_STRING)
                            ? (const char *)field_ptr : buf,
            .id          = input_id,
            .style       = "inspector-scalar-input",
            .on_change   = on_field_input,
            .change_data = b,
        });
        break;
    }
    case QS_FIELD_FLOAT2:
    case QS_FIELD_FLOAT3:
    case QS_FIELD_FLOAT4: {
        uint32_t n = fi->type == QS_FIELD_FLOAT2 ? 2 :
                     fi->type == QS_FIELD_FLOAT3 ? 3 : 4;
        const float *v = (const float *)field_ptr;

        char vec_row_id[96];
        snprintf(vec_row_id, sizeof(vec_row_id), "ins-field-vec-%s-%s", comp_name, fi->name);
        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_HORIZONTAL,
            .id        = vec_row_id,
            .style     = "inspector-vec-row",
        });
        for (uint32_t i = 0; i < n; i++) {
            InputBinding *b = alloc_binding();
            *b = (InputBinding){
                .comp_type = ct, .field_offset = fi->offset,
                .field_size = fi->size, .vec_index = i,
                .field_type = fi->type,
                .comp_name = comp_name, .field_name = fi->name,
            };
            snprintf(buf, sizeof(buf), "%.3f", v[i]);

            char axis_group_id[96];
            snprintf(axis_group_id, sizeof(axis_group_id), "ins-field-axis-%s-%s-%u", comp_name, fi->name, i);
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_HORIZONTAL,
                .id        = axis_group_id,
                .style     = "inspector-axis-group",
            });
            char axis_label_id[96];
            snprintf(axis_label_id, sizeof(axis_label_id), "ins-field-axis-label-%s-%s-%u", comp_name, fi->name, i);
            ca_text(&(Ca_TextDesc){
                .text  = s_axis_text[i],
                .id    = axis_label_id,
                .style = s_axis_style[i],
            });
            char axis_input_id[96];
            snprintf(axis_input_id, sizeof(axis_input_id), "ins-field-axis-input-%s-%s-%u", comp_name, fi->name, i);
            b->widget = ca_input(&(Ca_InputDesc){
                .text        = buf,
                .id          = axis_input_id,
                .style       = "inspector-vec-input",
                .on_change   = on_field_input,
                .change_data = b,
            });
            ca_div_end();
        }
        ca_div_end();
        break;
    }
    case QS_FIELD_BOOL: {
        InputBinding *b = alloc_binding();
        *b = (InputBinding){
            .comp_type = ct, .field_offset = fi->offset,
            .field_size = fi->size, .field_type = fi->type,
            .comp_name = comp_name, .field_name = fi->name,
        };
        char check_id[96];
        snprintf(check_id, sizeof(check_id), "ins-field-check-%s-%s", comp_name, fi->name);
        ca_checkbox(&(Ca_CheckboxDesc){
            .text        = "",
            .checked     = *(const bool *)field_ptr,
            .id          = check_id,
            .on_change   = on_bool_input,
            .change_data = b,
        });
        break;
    }
    case QS_FIELD_ENTITY: {
        Qs_Entity ref = *(const Qs_Entity *)field_ptr;
        if (ref == QS_ENTITY_INVALID)
            snprintf(buf, sizeof(buf), "(none)");
        else
            snprintf(buf, sizeof(buf), "Entity %u", ref);
        char value_id[96];
        snprintf(value_id, sizeof(value_id), "ins-field-value-%s-%s", comp_name, fi->name);
        ca_text(&(Ca_TextDesc){
            .text  = buf,
            .id    = value_id,
            .style = "inspector-field-value",
        });
        break;
    }
    default: break;
    }

    ca_div_end(); /* field row */
}

/* ================================================================
   BUILD SECTIONS — rebuild dynamic content for selected entity
   ================================================================ */

static void on_remove_component(Ca_Button *btn, void *user_data)
{
    (void)btn;
    if (!s_editor || !user_data) return;
    Qs_Scene *scene  = inspect_source_scene(s_editor);
    Qs_Entity entity = editor_selected_entity(s_editor);
    if (!scene || entity == QS_ENTITY_INVALID) return;
    Qs_ComponentType *ct = (Qs_ComponentType *)user_data;
    if (!qs_entity_has(scene, entity, ct)) return;
    qs_entity_remove(scene, entity, ct);
    /* Force rebuild of inspector content next frame */
    s_displayed_entity = QS_ENTITY_INVALID;
}

static void on_edit_prototype(Ca_Button *btn, void *user_data)
{
    (void)btn; (void)user_data;
    if (!s_editor) return;

    /* Already in prototype mode — don't nest */
    if (editor_mode(s_editor) == ED_MODE_PROTOTYPE) return;

    Qs_Scene *scene = qs_scene_active();
    Qs_Entity entity = editor_selected_entity(s_editor);
    if (!scene || entity == QS_ENTITY_INVALID) return;

    Qs_PrototypeComp *pc = (Qs_PrototypeComp *)qs_entity_get(
        scene, entity, qs_prototype_comp_type());
    if (!pc || !pc->path[0]) return;

    /* Resolve the .qproto path the same way the runtime does:
       absolute paths pass through, otherwise try project-relative first
       (matches how qs_scene.c's resolve_path() works for prototypes). */
    Qs_Project *proj = editor_project(s_editor);
    if (!proj) return;

    char full_path[1024];
    if (pc->path[0] == '/' || pc->path[0] == '\\' ||
        (pc->path[0] && pc->path[1] == ':')) {
        snprintf(full_path, sizeof(full_path), "%s", pc->path);
    } else {
        const char *proj_path = qs_project_path(proj);
        snprintf(full_path, sizeof(full_path), "%s/%s", proj_path, pc->path);
    }

    editor_open_prototype(s_editor, full_path);
}

/* ================================================================
   MATERIAL EDITOR HELPERS
   ================================================================ */

/* Extract the filename stem (basename without extension) from a path.
   E.g. "assets/ABeautifulGame/materials/King_Black_0.qsmat" → "King_Black_0" */
static const char *path_stem(const char *path, char *buf, size_t bufsz)
{
    if (!path || !buf || bufsz == 0) return path;
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
    const char *base = slash ? slash + 1 : path;
    snprintf(buf, bufsz, "%s", base);
    char *dot = strrchr(buf, '.');
    if (dot) *dot = '\0';
    return buf;
}

static void refresh_mat_options(void)
{
    s_mat_option_count = 0;
    s_mat_pool_used    = 0;

    Qs_Project *proj = s_editor ? editor_project(s_editor) : NULL;
    uint32_t n = proj ? qs_project_material_count(proj) : 0;

    uint32_t need = n + 1;
    if (need > s_mat_option_cap) {
        uint32_t cap = need < 16 ? 16 : need;
        const char **a = realloc(s_mat_options, cap * sizeof(*a));
        char       **c = realloc(s_mat_paths,   cap * sizeof(*c));
        if (a) { s_mat_options = a; s_mat_option_cap = cap; }
        if (c)   s_mat_paths   = c;
    }
    s_mat_options[s_mat_option_count++] = "(none)";

    for (uint32_t i = 0; i < n; i++) {
        const char *rel = qs_project_material_path(proj, i);
        if (!rel) continue;

        /* Display name: stem of the filename */
        char stem[128];
        path_stem(rel, stem, sizeof(stem));
        size_t len = strlen(stem) + 1;
        if (s_mat_pool_used + len > s_mat_pool_cap) {
            uint32_t cap = s_mat_pool_cap ? s_mat_pool_cap * 2 : 512;
            while (cap < s_mat_pool_used + len) cap *= 2;
            char *tmp = realloc(s_mat_name_pool, cap);
            if (tmp) { s_mat_name_pool = tmp; s_mat_pool_cap = cap; }
        }
        char *dst = s_mat_name_pool + s_mat_pool_used;
        memcpy(dst, stem, len);
        s_mat_pool_used += (uint32_t)len;

        uint32_t idx = s_mat_option_count;
        if (idx < s_mat_option_cap) {
            s_mat_paths  [idx - 1] = (char *)rel;   /* project-relative, owned by project */
            s_mat_options[idx]     = dst;
            s_mat_option_count++;
        }
    }
}

static void refresh_tex_options(void)
{
    s_tex_option_count = 0;

    Qs_Project *proj = s_editor ? editor_project(s_editor) : NULL;
    uint32_t n = proj ? qs_project_texture_count(proj) : 0;

    uint32_t need = n + 1;
    if (need > s_tex_option_cap) {
        uint32_t cap = need < 16 ? 16 : need;
        const char **a = realloc(s_tex_options, cap * sizeof(*a));
        char       **b = realloc(s_tex_list,    cap * sizeof(*b));
        if (a) { s_tex_options = a; s_tex_option_cap = cap; }
        if (b)   s_tex_list    = b;
    }
    if (!s_tex_options || !s_tex_list) return;
    s_tex_options[s_tex_option_count++] = "(none)";

    for (uint32_t i = 0; i < n; i++) {
        const char *rel = qs_project_texture_path(proj, i);
        if (!rel) continue;
        if (s_tex_option_count < s_tex_option_cap) {
            s_tex_list   [s_tex_option_count - 1] = (char *)rel;  /* project-relative */
            s_tex_options[s_tex_option_count]     = rel;
            s_tex_option_count++;
        }
    }
}

static void refresh_mesh_options(void)
{
    s_mesh_option_count = 0;

    Qs_Project *proj = s_editor ? editor_project(s_editor) : NULL;
    uint32_t n = proj ? qs_project_mesh_count(proj) : 0;

    uint32_t need = n + 1;
    if (need > s_mesh_option_cap) {
        uint32_t cap = need < 16 ? 16 : need;
        const char **a = realloc(s_mesh_options, cap * sizeof(*a));
        char       **b = realloc(s_mesh_paths,   cap * sizeof(*b));
        if (a) { s_mesh_options = a; s_mesh_option_cap = cap; }
        if (b)   s_mesh_paths   = b;
    }
    if (!s_mesh_options || !s_mesh_paths) return;
    s_mesh_options[s_mesh_option_count++] = "(none)";

    for (uint32_t i = 0; i < n; i++) {
        const char *rel = qs_project_mesh_path(proj, i);
        if (!rel) continue;
        if (s_mesh_option_count < s_mesh_option_cap) {
            s_mesh_paths  [s_mesh_option_count - 1] = (char *)rel;
            s_mesh_options[s_mesh_option_count]     = rel;
            s_mesh_option_count++;
        }
    }
}

static void on_material_select(Ca_Select *sel, void *user_data)
{
    (void)user_data;
    if (!s_editor || !sel) return;
    Qs_Project *proj = editor_project(s_editor);
    Qs_Engine  *eng  = editor_engine(s_editor);
    if (!proj || !eng) return;
    Qs_Scene  *scene  = inspect_source_scene(s_editor);
    Qs_Entity  entity = editor_selected_entity(s_editor);
    if (!scene || entity == QS_ENTITY_INVALID) return;
    Qs_MeshComp *mc = (Qs_MeshComp *)qs_entity_get(scene, entity, qs_mesh_comp_type());
    if (!mc) return;

    /* Release the old material's ref before assigning a new one */
    if (mc->material_path[0]) {
        char abs[1024];
        qs_project_resolve(proj, mc->material_path, abs, sizeof(abs));
        qs_asset_cache_release_material(abs);
        mc->material = NULL;
    }

    int idx = ca_select_get(sel);
    if (idx <= 0) {
        mc->material_path[0] = '\0';
    } else if ((uint32_t)idx < s_mat_option_count) {
        const char *rel = s_mat_paths[idx - 1];
        char abs[1024];
        qs_project_resolve(proj, rel, abs, sizeof(abs));
        Qs_Material *m = qs_asset_cache_material(eng, abs);   /* acquires ref */
        mc->material = m;
        snprintf(mc->material_path, sizeof(mc->material_path), "%s", rel ? rel : "");
    }
    /* Force material editor rebuild */
    s_displayed_entity = QS_ENTITY_INVALID;
}

static void on_texture_select(Ca_Select *sel, void *user_data)
{
    TexPickCtx *ctx = (TexPickCtx *)user_data;
    if (!sel || !ctx || !ctx->mat) return;

    /* Release ref on whatever texture is currently in this slot */
    Qs_Texture *old_tex = qs_material_get_texture(ctx->mat, ctx->slot);
    if (old_tex) {
        const char *old_name = qs_texture_name(old_tex);
        if (old_name && *old_name)
            qs_asset_cache_release_texture(old_name);
    }

    int idx = ca_select_get(sel);
    Qs_Texture *tex = NULL;
    if (idx > 0 && (uint32_t)idx < s_tex_option_count && s_tex_list) {
        Qs_Project *proj = s_editor ? editor_project(s_editor) : NULL;
        Qs_Engine  *eng  = s_editor ? editor_engine(s_editor)  : NULL;
        if (proj && eng) {
            char abs[1024];
            qs_project_resolve(proj, s_tex_list[idx - 1], abs, sizeof(abs));
            tex = qs_asset_cache_texture(eng, abs);   /* acquires ref */
        }
    }
    qs_material_set_texture(ctx->mat, ctx->slot, tex);
}

static void on_mesh_select(Ca_Select *sel, void *user_data)
{
    (void)user_data;
    if (!s_editor || !sel) return;
    Qs_Scene  *scene  = inspect_source_scene(s_editor);
    Qs_Entity  entity = editor_selected_entity(s_editor);
    if (!scene || entity == QS_ENTITY_INVALID) return;
    Qs_MeshComp *mc = (Qs_MeshComp *)qs_entity_get(scene, entity, qs_mesh_comp_type());
    if (!mc) return;

    Qs_Project *proj = editor_project(s_editor);
    Qs_Engine  *eng  = editor_engine(s_editor);
    if (!proj || !eng) return;

    /* Release old mesh ref before assigning a new one */
    if (mc->mesh_path[0]) {
        char abs[1024];
        qs_project_resolve(proj, mc->mesh_path, abs, sizeof(abs));
        qs_asset_cache_release_mesh(abs);
        mc->mesh = NULL;
        mc->mesh_path[0] = '\0';
    }

    int idx = ca_select_get(sel);
    if (idx > 0 && (uint32_t)idx < s_mesh_option_count && s_mesh_paths) {
        const char *rel = s_mesh_paths[idx - 1];
        char abs[1024];
        qs_project_resolve(proj, rel, abs, sizeof(abs));
        Qs_Mesh *m = qs_asset_cache_mesh(eng, abs);   /* acquires ref */
        if (m) {
            mc->mesh = m;
            snprintf(mc->mesh_path, sizeof(mc->mesh_path), "%s", rel);
        }
    }
}

/* Build a scalar float input bound to a material PBR param */
static void build_mat_float_row(const char *label, Qs_Material *mat,
                                size_t param_offset, uint32_t vec_idx,
                                const char *id_sfx)
{
    char row_id[96];
    snprintf(row_id, sizeof(row_id), "mat-row-%s", id_sfx);
    ca_div_begin(&(Ca_DivDesc){ .direction = CA_VERTICAL, .id = row_id,
                                .style = "inspector-field-row" });
    {
        ca_text(&(Ca_TextDesc){ .text = label, .style = "inspector-field-name" });
        InputBinding *b = alloc_binding();
        b->is_mat_param     = true;
        b->mat              = mat;
        b->mat_param_offset = (uint32_t)param_offset;
        b->mat_vec_index    = vec_idx;
        b->field_type       = QS_FIELD_FLOAT;
        const Qs_PBRParams *p = qs_material_params(mat);
        char vbuf[32];
        if (p) {
            const float *fld = (const float *)((const char *)p + param_offset);
            snprintf(vbuf, sizeof(vbuf), "%.3f", fld[vec_idx]);
        } else {
            snprintf(vbuf, sizeof(vbuf), "0.000");
        }
        char input_id[96];
        snprintf(input_id, sizeof(input_id), "mat-input-%s", id_sfx);
        b->widget = ca_input(&(Ca_InputDesc){
            .text        = vbuf,
            .id          = input_id,
            .style       = "inspector-scalar-input",
            .on_change   = on_field_input,
            .change_data = b,
        });
    }
    ca_div_end();
}

/* Build a float vector (RGBA/RGB) row bound to a material PBR param */
static void build_mat_vec_row(const char *label, Qs_Material *mat,
                               size_t param_offset, uint32_t n_elems,
                               const char *id_sfx)
{
    char row_id[96];
    snprintf(row_id, sizeof(row_id), "mat-vrow-%s", id_sfx);
    ca_div_begin(&(Ca_DivDesc){ .direction = CA_VERTICAL, .id = row_id,
                                .style = "inspector-field-row" });
    {
        ca_text(&(Ca_TextDesc){ .text = label, .style = "inspector-field-name" });
        const Qs_PBRParams *p = qs_material_params(mat);
        char vec_row_id[96];
        snprintf(vec_row_id, sizeof(vec_row_id), "mat-vec-%s", id_sfx);
        ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .id = vec_row_id,
                                    .style = "inspector-vec-row" });
        for (uint32_t i = 0; i < n_elems; i++) {
            InputBinding *b = alloc_binding();
            b->is_mat_param     = true;
            b->mat              = mat;
            b->mat_param_offset = (uint32_t)param_offset;
            b->mat_vec_index    = i;
            b->field_type       = QS_FIELD_FLOAT;
            char vbuf[32];
            if (p) {
                const float *fld = (const float *)((const char *)p + param_offset);
                snprintf(vbuf, sizeof(vbuf), "%.3f", fld[i]);
            } else {
                snprintf(vbuf, sizeof(vbuf), "0.000");
            }
            char grp_id[96], lbl_id[96], inp_id[96];
            snprintf(grp_id, sizeof(grp_id), "mat-ax-grp-%s-%u", id_sfx, i);
            snprintf(lbl_id, sizeof(lbl_id), "mat-ax-lbl-%s-%u", id_sfx, i);
            snprintf(inp_id, sizeof(inp_id), "mat-ax-inp-%s-%u", id_sfx, i);
            ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .id = grp_id,
                                        .style = "inspector-axis-group" });
            ca_text(&(Ca_TextDesc){ .text  = s_axis_text[i],
                                    .id    = lbl_id,
                                    .style = s_axis_style[i] });
            b->widget = ca_input(&(Ca_InputDesc){
                .text        = vbuf,
                .id          = inp_id,
                .style       = "inspector-vec-input",
                .on_change   = on_field_input,
                .change_data = b,
            });
            ca_div_end();
        }
        ca_div_end();
    }
    ca_div_end();
}

/* Build a texture-slot row: label + dropdown of project textures */
static void build_mat_texture_row(const char *label, Qs_Material *mat,
                                   uint32_t slot)
{
    char row_id[96];
    snprintf(row_id, sizeof(row_id), "mat-tex-row-%u", slot);
    ca_div_begin(&(Ca_DivDesc){ .direction = CA_VERTICAL, .id = row_id,
                                .style = "inspector-field-row" });
    {
        ca_text(&(Ca_TextDesc){ .text = label, .style = "inspector-field-name" });
        /* Find current texture index by comparing the texture's abs path
           (set as its name by qs_asset_cache_texture) against resolved paths */
        Qs_Texture *cur_tex = qs_material_get_texture(mat, slot);
        int selected = 0;
        if (cur_tex && s_tex_list) {
            Qs_Project *proj = s_editor ? editor_project(s_editor) : NULL;
            const char *cur_name = qs_texture_name(cur_tex);
            for (uint32_t i = 1; i < s_tex_option_count; i++) {
                if (!s_tex_list[i - 1]) continue;
                char abs[1024];
                if (proj) qs_project_resolve(proj, s_tex_list[i - 1], abs, sizeof(abs));
                else      snprintf(abs, sizeof(abs), "%s", s_tex_list[i - 1]);
                if (cur_name && strcmp(cur_name, abs) == 0) { selected = (int)i; break; }
            }
        }
        s_tex_ctxs[slot].mat  = mat;
        s_tex_ctxs[slot].slot = slot;
        char sel_id[96];
        snprintf(sel_id, sizeof(sel_id), "mat-tex-sel-%u", slot);
        ca_select(&(Ca_SelectDesc){
            .options      = s_tex_options,
            .option_count = s_tex_option_count > 0 ? (int)s_tex_option_count : 1,
            .selected     = selected,
            .id           = sel_id,
            .style        = "mat-tex-select",
            .on_change    = on_texture_select,
            .change_data  = &s_tex_ctxs[slot],
        });
    }
    ca_div_end();
}

/* Build a styled sub-section divider label */
static void build_mat_subsection(const char *title, const char *id)
{
    ca_text(&(Ca_TextDesc){ .text = title, .id = id, .style = "mat-subsection-label" });
}

/* Build the full MeshComp inspector section with inline material editor */
static void build_mesh_comp_section(Qs_Scene *scene, Qs_Entity entity,
                                    Qs_ComponentType *ct, Qs_MeshComp *mc)
{
    (void)scene; (void)entity;

    /* Ensure project assets are scanned (lazy, safe to call repeatedly) */
    Qs_Project *proj = s_editor ? editor_project(s_editor) : NULL;
    if (proj && qs_project_mesh_count(proj) == 0 &&
                qs_project_material_count(proj) == 0 &&
                qs_project_texture_count(proj) == 0) {
        qs_project_scan_assets(proj);
    }

    /* ---- visible checkbox ---- */
    const Qs_TypeInfo *info = qs_component_type_info(ct);
    if (info) {
        for (uint32_t f = 0; f < info->field_count; f++) {
            if (strcmp(info->fields[f].name, "visible") == 0) {
                build_field("MeshComp", ct, &info->fields[f], mc);
                break;
            }
        }
    }

    /* ---- Mesh picker dropdown ---- */
    ca_hr(&(Ca_HrDesc){ .color = 0xFF242430u });
    build_mat_subsection(ICON_MESH "  Mesh", "mat-sub-mesh");

    refresh_mesh_options();
    int sel_mesh_idx = 0;
    if (mc->mesh_path[0] != '\0' && s_mesh_paths) {
        for (uint32_t i = 1; i < s_mesh_option_count; i++) {
            if (s_mesh_paths[i - 1] &&
                strcmp(mc->mesh_path, s_mesh_paths[i - 1]) == 0)
            { sel_mesh_idx = (int)i; break; }
        }
    } else if (mc->mesh && s_mesh_paths) {
        /* Fallback: match by mesh surface name */
        const char *cur_name = qs_mesh_name(mc->mesh);
        if (cur_name) {
            Qs_Engine *eng = s_editor ? editor_engine(s_editor) : NULL;
            for (uint32_t i = 1; i < s_mesh_option_count && eng; i++) {
                if (!s_mesh_paths[i - 1]) continue;
                char abs[1024];
                qs_project_resolve(proj, s_mesh_paths[i - 1], abs, sizeof(abs));
                Qs_Mesh *m = qs_asset_cache_mesh(eng, abs);
                if (m && qs_mesh_name(m) && strcmp(qs_mesh_name(m), cur_name) == 0) {
                    sel_mesh_idx = (int)i; break;
                }
            }
        }
    }
    ca_div_begin(&(Ca_DivDesc){ .direction = CA_VERTICAL, .id = "mat-mesh-row",
                                .style = "inspector-field-row" });
    ca_select(&(Ca_SelectDesc){
        .options      = s_mesh_options,
        .option_count = s_mesh_option_count > 0 ? (int)s_mesh_option_count : 1,
        .selected     = sel_mesh_idx,
        .id           = "mat-mesh-select",
        .style        = "inspector-select",
        .on_change    = on_mesh_select,
    });
    ca_div_end();

    /* ---- Material selector ---- */
    ca_hr(&(Ca_HrDesc){ .color = 0xFF242430u });
    build_mat_subsection(ICON_MESH "  Material", "mat-sub-material");

    refresh_mat_options();
    int sel_mat_idx = 0;
    if (mc->material_path[0] && s_mat_paths) {
        for (uint32_t i = 1; i < s_mat_option_count; i++) {
            if (s_mat_paths[i - 1] && strcmp(s_mat_paths[i - 1], mc->material_path) == 0) {
                sel_mat_idx = (int)i; break;
            }
        }
    }
    ca_div_begin(&(Ca_DivDesc){ .direction = CA_VERTICAL, .id = "mat-mat-row",
                                .style = "inspector-field-row" });
    ca_select(&(Ca_SelectDesc){
        .options      = s_mat_options,
        .option_count = s_mat_option_count > 0 ? (int)s_mat_option_count : 1,
        .selected     = sel_mat_idx,
        .id           = "mat-mat-select",
        .style        = "inspector-select",
        .on_change    = on_material_select,
    });
    ca_div_end();

    if (!mc->material) return;

    /* ---- PBR Properties ---- */
    ca_hr(&(Ca_HrDesc){ .color = 0xFF242430u });
    build_mat_subsection("  Properties", "mat-sub-props");

    build_mat_vec_row("Base Color",  mc->material,
                      offsetof(Qs_PBRParams, base_color_factor), 4, "base-col");
    build_mat_float_row("Metallic",      mc->material,
                        offsetof(Qs_PBRParams, metallic_factor),    0, "metallic");
    build_mat_float_row("Roughness",     mc->material,
                        offsetof(Qs_PBRParams, roughness_factor),   0, "roughness");
    build_mat_float_row("Normal Scale",  mc->material,
                        offsetof(Qs_PBRParams, normal_scale),       0, "nrm-scale");
    build_mat_float_row("Occlusion",     mc->material,
                        offsetof(Qs_PBRParams, occlusion_strength), 0, "occlusion");
    build_mat_vec_row("Emissive",    mc->material,
                      offsetof(Qs_PBRParams, emissive_factor), 3, "emissive");
    const Qs_PBRParams *p = qs_material_params(mc->material);
    if (p && p->alpha_mode == (uint32_t)QS_ALPHA_MODE_MASK) {
        build_mat_float_row("Alpha Cutoff", mc->material,
                            offsetof(Qs_PBRParams, alpha_cutoff), 0, "alpha-cut");
    }

    /* ---- Texture Slots ---- */
    ca_hr(&(Ca_HrDesc){ .color = 0xFF242430u });
    build_mat_subsection("  Textures", "mat-sub-tex");

    refresh_tex_options();
    static const char *s_slot_labels[QS_MAT_SLOTS] = {
        "Albedo", "Metal / Rough", "Normal", "Occlusion", "Emissive"
    };
    for (uint32_t slot = 0; slot < QS_MAT_SLOTS; slot++)
        build_mat_texture_row(s_slot_labels[slot], mc->material, slot);
}
static void build_add_component(Qs_Scene *scene, Qs_Entity entity);

static void build_sections(Qs_Scene *scene, Qs_Entity entity)
{
    uint32_t type_count = qs_component_type_count();
    for (uint32_t t = 0; t < type_count; t++) {
        Qs_ComponentType *ct = qs_component_type_at(t);
        if (!ct) continue;
        if (!qs_entity_has(scene, entity, ct)) continue;

        const char *comp_name = qs_component_type_name(ct);
        if (strcmp(comp_name, "IdComp") == 0 ||
            strcmp(comp_name, "TagComp") == 0)
            continue;

        /* Section header — single label with icon + name */
        char section_buf[128];
        snprintf(section_buf, sizeof(section_buf), "%s  %s",
                 component_icon(comp_name), comp_name);

        char section_id[96];
        snprintf(section_id, sizeof(section_id), "ins-section-%s", comp_name);
        char label_id[96];
        snprintf(label_id, sizeof(label_id), "ins-section-label-%s", comp_name);

        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_HORIZONTAL,
            .id        = section_id,
            .style     = "inspector-section-header",
        });
        ca_text(&(Ca_TextDesc){
            .text  = section_buf,
            .id    = label_id,
            .style = "inspector-section-label",
            .color = component_icon_color(comp_name),
        });
        /* Remove-component button — hidden for required components */
        if (strcmp(comp_name, "Transform") != 0) {
            char rm_id[96];
            snprintf(rm_id, sizeof(rm_id), "ins-section-rm-%s", comp_name);
            ca_btn_begin(&(Ca_BtnDesc){
                .text       = ICON_TRASH,
                .id         = rm_id,
                .style      = "inspector-remove-btn",
                .on_click   = on_remove_component,
                .click_data = ct,
            });
            ca_btn_end();
        }
        ca_div_end();

        /* Prominent "Edit Prototype" action row — opens the prototype in
           an isolated editor window so the user can modify its inner
           entities without polluting the outer scene's hierarchy. */
        if (strcmp(comp_name, "Prototype") == 0 &&
            editor_mode(s_editor) == ED_MODE_SCENE)
        {
            ca_btn_begin(&(Ca_BtnDesc){
                .text     = ICON_PROTOTYPE "  Edit Prototype",
                .id       = "ins-proto-edit-btn",
                .style    = "inspector-proto-edit-row",
                .on_click = on_edit_prototype,
            });
            ca_btn_end();
        }

        /* Fields */
        const Qs_TypeInfo *info = qs_component_type_info(ct);
        void *data = qs_entity_get(scene, entity, ct);

        if (strcmp(comp_name, "MeshComp") == 0 && data) {
            build_mesh_comp_section(scene, entity, ct, (Qs_MeshComp *)data);
        } else if (info && data) {
            for (uint32_t f = 0; f < info->field_count; f++)
                build_field(comp_name, ct, &info->fields[f], data);
        }
    }

    /* Add Component dropdown at the bottom of the section list */
    build_add_component(scene, entity);
}

/* ---- Add Component dropdown --------------------------------------- */

#define INS_MAX_ADDABLE_TYPES 64
static const char       *s_add_comp_options[INS_MAX_ADDABLE_TYPES + 1];
static Qs_ComponentType *s_add_comp_types  [INS_MAX_ADDABLE_TYPES];
static uint32_t          s_add_comp_count;

static void on_add_component_select(Ca_Select *sel, void *user_data)
{
    (void)user_data;
    if (!s_editor || !sel) return;
    int idx = ca_select_get(sel);
    if (idx <= 0 || (uint32_t)idx > s_add_comp_count) return;

    Qs_Scene *scene  = inspect_source_scene(s_editor);
    Qs_Entity entity = editor_selected_entity(s_editor);
    if (!scene || entity == QS_ENTITY_INVALID) return;

    Qs_ComponentType *ct = s_add_comp_types[idx - 1];
    if (!ct) return;
    if (qs_entity_has(scene, entity, ct)) return;
    qs_entity_add(scene, entity, ct);

    /* Force rebuild of inspector content next frame */
    s_displayed_entity = QS_ENTITY_INVALID;
}

static void build_add_component(Qs_Scene *scene, Qs_Entity entity)
{
    s_add_comp_count = 0;
    s_add_comp_options[s_add_comp_count++] = ICON_PLUS "  Add Component";

    uint32_t type_count = qs_component_type_count();
    for (uint32_t t = 0; t < type_count && s_add_comp_count <= INS_MAX_ADDABLE_TYPES; t++) {
        Qs_ComponentType *ct = qs_component_type_at(t);
        if (!ct) continue;
        const char *name = qs_component_type_name(ct);
        if (!name) continue;
        /* Hide bookkeeping components and ones already present. */
        if (strcmp(name, "IdComp")  == 0 || strcmp(name, "TagComp") == 0)
            continue;
        if (strcmp(name, "Transform") == 0)
            continue; /* always present */
        if (qs_entity_has(scene, entity, ct))
            continue;
        s_add_comp_types  [s_add_comp_count - 1] = ct;
        s_add_comp_options[s_add_comp_count]     = name;
        s_add_comp_count++;
    }

    /* When there's nothing left to add, hide the row entirely. */
    if (s_add_comp_count <= 1) return;

    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .id        = "inspector-add-comp-row",
        .style     = "inspector-add-comp-row",
    });
    ca_select(&(Ca_SelectDesc){
        .options      = s_add_comp_options,
        .option_count = (int)s_add_comp_count,
        .selected     = 0,
        .id           = "inspector-add-comp-select",
        .style        = "inspector-add-comp-select",
        .on_change    = on_add_component_select,
    });
    ca_div_end();
}

/* ================================================================
   UPDATE (called every frame)
   ================================================================ */

static void update_header(Qs_Scene *scene, Qs_Entity entity)
{
    const char *name = qs_entity_name(scene, entity);
    ca_set_text(s_entity_name_input, name ? name : "");

    Qs_IdComp *id_comp = (Qs_IdComp *)qs_entity_get(
                              scene, entity, qs_id_comp_type());
    if (id_comp) {
        char id_buf[32];
        snprintf(id_buf, sizeof(id_buf), ICON_ID "  %u", id_comp->id);
        ca_set_text(s_id_value, id_buf);
    }

    Qs_TagComp *tag_comp = (Qs_TagComp *)qs_entity_get(
                                scene, entity, qs_tag_comp_type());
    if (tag_comp)
        ca_set_text(s_tag_input, tag_comp->tag);
}

void ed_inspector_update(void *editor)
{
    Editor *ed = (Editor *)editor;
    Qs_Entity entity = editor_selected_entity(ed);
    Qs_Scene *scene  = inspect_source_scene(ed);

    bool valid = (entity != QS_ENTITY_INVALID && scene &&
                  qs_entity_valid(scene, entity));

    if (!valid) {
        if (s_displayed_entity != QS_ENTITY_INVALID) {
            s_displayed_entity = QS_ENTITY_INVALID;
            s_displayed_scene  = NULL;
            ca_set_hidden(s_header_div, true);
            ca_set_hidden(s_no_selection, false);
            ca_reconcile_begin(s_content_div);
            ca_div_end();
            free_bindings();
        }
        return;
    }

    if (entity == s_displayed_entity && scene == s_displayed_scene) {
        /* Same entity — refresh bound field values (e.g. during gizmo drag) */
        if (!scene) return;
        Qs_PrototypeComp *pc = active_override_target(ed);
        Qs_IdComp *idc = (Qs_IdComp *)qs_entity_get(scene, entity, qs_id_comp_type());

        /* ---- Name input focus tracking ---- */
        if (s_entity_name_input) {
            bool nf = ca_input_is_focused(s_entity_name_input);
            if (nf && !s_name_was_focused) {
                const char *cur = qs_entity_name(scene, entity);
                snprintf(s_name_before, sizeof(s_name_before), "%s", cur ? cur : "");
            } else if (!nf && s_name_was_focused) {
                const char *cur = qs_entity_name(scene, entity);
                if (cur && strcmp(cur, s_name_before) != 0)
                    ed_undo_push_name(scene, entity, s_name_before, cur);
            }
            s_name_was_focused = nf;
        }
        /* ---- Tag input focus tracking ---- */
        if (s_tag_input) {
            bool tf = ca_input_is_focused(s_tag_input);
            Qs_TagComp *tg = (Qs_TagComp *)qs_entity_get(scene, entity, qs_tag_comp_type());
            if (tf && !s_tag_was_focused && tg) {
                snprintf(s_tag_before, sizeof(s_tag_before), "%s", tg->tag);
            } else if (!tf && s_tag_was_focused && tg) {
                if (strcmp(tg->tag, s_tag_before) != 0)
                    ed_undo_push_tag(scene, entity, s_tag_before, tg->tag);
            }
            s_tag_was_focused = tf;
        }

        for (uint32_t i = 0; i < s_binding_count; i++) {
            InputBinding *b = &s_bindings[i];
            if (!b->widget) continue;

            /* ---- Material PBR param binding: update live float value ---- */
            if (b->is_mat_param) {
                if (!b->mat || ca_input_is_focused(b->widget)) continue;
                const Qs_PBRParams *p = qs_material_params(b->mat);
                if (!p) continue;
                const float *fld = (const float *)((const char *)p + b->mat_param_offset);
                char vbuf[32];
                snprintf(vbuf, sizeof(vbuf), "%.3f", fld[b->mat_vec_index]);
                const char *cur = ca_get_text(b->widget);
                if (!cur || strcmp(cur, vbuf) != 0)
                    ca_set_text(b->widget, vbuf);
                continue;
            }

            if (!b->comp_type) continue;
            void *comp = qs_entity_get(scene, entity, b->comp_type);
            if (!comp) continue;
            void *field_ptr = (char *)comp + b->field_offset;

            /* ---- Undo focus tracking ---- */
            bool focused = ca_input_is_focused(b->widget);
            if (focused && !b->was_focused) {
                /* Focus gained: snapshot the live field value. */
                size_t copy = b->field_size;
                if (copy > sizeof(b->before_buf)) copy = sizeof(b->before_buf);
                memcpy(b->before_buf, field_ptr, copy);
                /* Snapshot prior override value if relevant. */
                b->proto_had_before = false;
                if (pc && idc && b->comp_name && b->field_name) {
                    const Qs_PrototypeOverride *ov = qs_prototype_find_override(
                        pc, idc->id, b->comp_name, b->field_name);
                    if (ov) {
                        b->proto_had_before = true;
                        memcpy(b->proto_before_buf, &ov->value, sizeof(ov->value));
                    }
                }
            } else if (!focused && b->was_focused) {
                /* Focus lost: emit one undo command for the whole edit. */
                size_t copy = b->field_size;
                if (copy > sizeof(b->before_buf)) copy = sizeof(b->before_buf);
                if (memcmp(b->before_buf, field_ptr, copy) != 0) {
                    if (pc && idc && b->comp_name && b->field_name) {
                        /* Override edit: undo restores prior override (or
                           clears if there was none); redo writes the
                           current override value. */
                        const Qs_PrototypeOverride *cur =
                            qs_prototype_find_override(pc, idc->id,
                                                       b->comp_name, b->field_name);
                        ed_undo_push_override(
                            pc, idc->id, b->comp_name, b->field_name,
                            b->field_type,
                            b->proto_had_before, b->proto_before_buf,
                            cur == NULL,
                            cur ? &cur->value : NULL);
                    } else {
                        ed_undo_push_field(scene, entity, b->comp_type,
                                           b->field_offset, b->field_size,
                                           b->before_buf, field_ptr);
                    }
                }
            }
            b->was_focused = focused;

            char buf[64];
            switch (b->field_type) {
            case QS_FIELD_FLOAT:
                snprintf(buf, sizeof(buf), "%.3f", ((const float *)field_ptr)[0]);
                break;
            case QS_FIELD_FLOAT2:
            case QS_FIELD_FLOAT3:
            case QS_FIELD_FLOAT4:
                snprintf(buf, sizeof(buf), "%.3f", ((const float *)field_ptr)[b->vec_index]);
                break;
            case QS_FIELD_INT32:
                snprintf(buf, sizeof(buf), "%d", *(const int32_t *)field_ptr);
                break;
            case QS_FIELD_UINT32:
                snprintf(buf, sizeof(buf), "%u", *(const uint32_t *)field_ptr);
                break;
            case QS_FIELD_STRING:
                snprintf(buf, sizeof(buf), "%s", (const char *)field_ptr);
                break;
            default: continue;
            }
            if (focused) continue;
            const char *cur = ca_get_text(b->widget);
            if (!cur || strcmp(cur, buf) != 0)
                ca_set_text(b->widget, buf);
        }
        return;
    }

    s_displayed_entity = entity;
    s_displayed_scene  = scene;
    s_name_was_focused = false;
    s_tag_was_focused  = false;
    ca_set_hidden(s_header_div, false);
    ca_set_hidden(s_no_selection, true);
    update_header(scene, entity);

    ca_reconcile_begin(s_content_div);
    free_bindings();
    build_sections(scene, entity);
    ca_div_end();
}
