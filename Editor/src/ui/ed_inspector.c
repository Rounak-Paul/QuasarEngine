#include "ed_inspector.h"
#include "editor.h"
#include "ed_icons.h"
#include "ca_theme.h"

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
} InputBinding;

/* ---- Module state ---- */

static Editor       *s_editor;
static Ca_Label     *s_no_selection;
static Ca_Div       *s_header_div;
static Ca_TextInput *s_entity_name_input;
static Ca_Label     *s_id_value;
static Ca_TextInput *s_tag_input;
static Ca_Div       *s_content_div;      /* dynamic section container */
static Qs_Entity     s_displayed_entity = QS_ENTITY_INVALID;

/* Heap-allocated bindings — freed + rebuilt on each entity change */
static InputBinding *s_bindings;
static uint32_t      s_binding_count;
static uint32_t      s_binding_cap;

/* ---- Axis label text and CSS classes ---- */

static const char *s_axis_text[4]  = { "X", "Y", "Z", "W" };
static const char *s_axis_style[4] = {
    "inspector-axis-x", "inspector-axis-y",
    "inspector-axis-z", "inspector-axis-w"
};

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
    if (!s_editor || !b->comp_type) return;

    Qs_Scene *scene = qs_scene_active();
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
}

static void on_bool_input(Ca_Checkbox *cb, void *user_data)
{
    InputBinding *b = (InputBinding *)user_data;
    if (!s_editor || !b->comp_type) return;

    Qs_Scene *scene = qs_scene_active();
    Qs_Entity entity = editor_selected_entity(s_editor);
    if (!scene || entity == QS_ENTITY_INVALID) return;

    void *comp = qs_entity_get(scene, entity, b->comp_type);
    if (!comp) return;

    *(bool *)((char *)comp + b->field_offset) = ca_checkbox_get(cb);
}

/* ---- Prototype path dropdown -------------------------------------- */

/* Cached options buffer: a stable list of C-strings we hand to ca_select.
   Refreshed on each build_field("Prototype","path"). */
static const char **s_proto_options;
static uint32_t     s_proto_option_count;
static uint32_t     s_proto_option_cap;

static void refresh_proto_options(void)
{
    Qs_Project *proj = editor_project(s_editor);
    s_proto_option_count = 0;
    if (!proj) return;
    uint32_t n = qs_project_prototype_count(proj);
    if (n + 1 > s_proto_option_cap) {
        uint32_t cap = (n + 1 < 16) ? 16 : (n + 1);
        const char **tmp = (const char **)realloc(s_proto_options,
                                                  cap * sizeof(*tmp));
        if (!tmp) return;
        s_proto_options    = tmp;
        s_proto_option_cap = cap;
    }
    s_proto_options[s_proto_option_count++] = "(none)";
    for (uint32_t i = 0; i < n; i++) {
        const char *p = qs_project_prototype_path(proj, i);
        if (p) s_proto_options[s_proto_option_count++] = p;
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
    if (idx <= 0) {
        pc->path[0] = '\0';
    } else if ((uint32_t)idx < s_proto_option_count) {
        snprintf(pc->path, sizeof(pc->path), "%s", s_proto_options[idx]);
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

    Qs_Scene *scene = qs_scene_active();
    Qs_Entity entity = editor_selected_entity(s_editor);
    if (!scene || entity == QS_ENTITY_INVALID) return;

    const char *text = ca_get_text(input);
    qs_entity_set_name(scene, entity, text ? text : "");
}

static void on_tag_input(Ca_TextInput *input, void *user_data)
{
    (void)user_data;
    if (!s_editor) return;

    Qs_Scene *scene = qs_scene_active();
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
    ca_text(&(Ca_TextDesc){
        .text  = fi->name,
        .id    = name_id,
        .style = "inspector-field-name",
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
            if (cur && strcmp(cur, s_proto_options[i]) == 0) {
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

    /* Resolve the .qproto path relative to the project */
    Qs_Project *proj = editor_project(s_editor);
    if (!proj) return;

    char full_path[1024];
    if (pc->path[0] == '/' || pc->path[0] == '\\' ||
        (pc->path[0] && pc->path[1] == ':')) {
        snprintf(full_path, sizeof(full_path), "%s", pc->path);
    } else {
        /* Resolve relative to the scene file directory */
        const char *proj_path = qs_project_path(proj);
        snprintf(full_path, sizeof(full_path), "%s/scenes/%s",
                 proj_path, pc->path);
    }

    editor_open_prototype(s_editor, full_path);
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

        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_HORIZONTAL,
            .id        = section_id,
            .style     = "inspector-section-header",
        });
        ca_text(&(Ca_TextDesc){
            .text  = section_buf,
            .id    = section_id,
            .style = "inspector-section-label",
            .color = component_icon_color(comp_name),
        });
        /* "Edit" button for Prototype component in scene mode */
        if (strcmp(comp_name, "Prototype") == 0 &&
            editor_mode(s_editor) == ED_MODE_SCENE)
        {
            ca_btn_begin(&(Ca_BtnDesc){
                .text     = "Edit",
                .id       = "ins-proto-edit-btn",
                .style    = "inspector-edit-btn",
                .on_click = on_edit_prototype,
            });
            ca_btn_end();
        }
        ca_div_end();

        /* Fields */
        const Qs_TypeInfo *info = qs_component_type_info(ct);
        void *data = qs_entity_get(scene, entity, ct);

        if (info && data) {
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

    Qs_Scene *scene = qs_scene_active();
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
    s_add_comp_options[s_add_comp_count++] = "Add Component...";

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
    Qs_Scene *scene  = qs_scene_active();

    bool valid = (entity != QS_ENTITY_INVALID && scene &&
                  qs_entity_valid(scene, entity));

    if (!valid) {
        if (s_displayed_entity != QS_ENTITY_INVALID) {
            s_displayed_entity = QS_ENTITY_INVALID;
            ca_set_hidden(s_header_div, true);
            ca_set_hidden(s_no_selection, false);
            ca_reconcile_begin(s_content_div);
            ca_div_end();
            free_bindings();
        }
        return;
    }

    if (entity == s_displayed_entity) {
        /* Same entity — refresh bound field values (e.g. during gizmo drag) */
        if (!scene) return;
        for (uint32_t i = 0; i < s_binding_count; i++) {
            InputBinding *b = &s_bindings[i];
            if (!b->widget || !b->comp_type) continue;
            void *comp = qs_entity_get(scene, entity, b->comp_type);
            if (!comp) continue;
            const void *field_ptr = (const char *)comp + b->field_offset;
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
            if (ca_input_is_focused(b->widget)) continue;
            const char *cur = ca_get_text(b->widget);
            if (!cur || strcmp(cur, buf) != 0)
                ca_set_text(b->widget, buf);
        }
        return;
    }

    s_displayed_entity = entity;
    ca_set_hidden(s_header_div, false);
    ca_set_hidden(s_no_selection, true);
    update_header(scene, entity);

    ca_reconcile_begin(s_content_div);
    free_bindings();
    build_sections(scene, entity);
    ca_div_end();
}
