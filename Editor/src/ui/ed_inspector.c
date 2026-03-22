#include "ed_inspector.h"
#include "editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
   INSPECTOR PANEL — editable component property inspector
   ================================================================

   Pre-allocates widget pools for component sections and field rows.
   Values are populated when entity selection changes.  Each field
   type renders the appropriate widget (input, checkbox, vec row).
   On-change callbacks write edited values back to component data
   immediately through the reflection / ECS layer.

   IdComp and TagComp are shown in the entity header (not as
   component sections) since they are always-present metadata.
   ================================================================ */

/* Pool sizes */
#define MAX_SECTIONS        8
#define MAX_FIELDS          8
#define MAX_VEC_ELEMS       4

/* Nerd Font / FA icons */
#define ICON_COMPONENT  "\xEF\x80\x93"   /* U+F013 cog       */
#define ICON_TRANSFORM  "\xEF\x82\xB2"   /* U+F0B2 arrows    */
#define ICON_MESH       "\xEF\x86\xB2"   /* U+F1B2 cube      */
#define ICON_LIGHT      "\xEF\x83\xAB"   /* U+F0EB lightbulb */

/* ---- Binding data for on_change callbacks ---- */

typedef struct InputBinding {
    Qs_ComponentType *comp_type;
    size_t            field_offset;
    size_t            field_size;
    uint32_t          vec_index;
    Qs_FieldType      field_type;
} InputBinding;

/* ---- Per-field widget set ---- */

typedef struct FieldWidget {
    Ca_Div       *row;
    Ca_Label     *name_label;

    Ca_TextInput *scalar_input;
    InputBinding  scalar_bind;

    Ca_Div       *vec_div;
    Ca_Div       *axis_groups[MAX_VEC_ELEMS];
    Ca_Label     *axis_labels[MAX_VEC_ELEMS];
    Ca_TextInput *axis_inputs[MAX_VEC_ELEMS];
    InputBinding  axis_binds[MAX_VEC_ELEMS];

    Ca_Checkbox  *bool_check;
    InputBinding  bool_bind;

    Ca_Label     *entity_label;
} FieldWidget;

/* ---- Component section ---- */

typedef struct InspectorSection {
    Ca_Div     *header_div;
    Ca_Label   *header_icon;
    Ca_Label   *header_name;
    FieldWidget fields[MAX_FIELDS];
} InspectorSection;

/* ---- Module state ---- */

static Editor           *s_editor;
static Ca_Label         *s_no_selection;
static Ca_Div           *s_header_div;
static Ca_TextInput     *s_entity_name_input;
static Ca_Label         *s_id_value;
static Ca_TextInput     *s_tag_input;
static InspectorSection  s_sections[MAX_SECTIONS];
static Qs_Entity         s_prev_entity;

/* ---- Axis label text and CSS classes ---- */

static const char *s_axis_text[4]  = { "X", "Y", "Z", "W" };
static const char *s_axis_style[4] = {
    "inspector-axis-x", "inspector-axis-y",
    "inspector-axis-z", "inspector-axis-w"
};

/* ---- Icon helpers ---- */

static const char *component_icon(const char *name)
{
    if (strcmp(name, "Transform") == 0) return ICON_TRANSFORM;
    if (strcmp(name, "MeshComp") == 0)  return ICON_MESH;
    if (strcmp(name, "LightComp") == 0) return ICON_LIGHT;
    return ICON_COMPONENT;
}

static uint32_t component_icon_color(const char *name)
{
    if (strcmp(name, "Transform") == 0)
        return ca_color(0.40f, 0.60f, 0.90f, 1.0f);
    if (strcmp(name, "MeshComp") == 0)
        return ca_color(0.45f, 0.75f, 0.55f, 1.0f);
    if (strcmp(name, "LightComp") == 0)
        return ca_color(0.95f, 0.85f, 0.35f, 1.0f);
    return ca_color(0.55f, 0.55f, 0.70f, 1.0f);
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

    const char *text = ca_input_get_text(input);
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

static void on_entity_name_input(Ca_TextInput *input, void *user_data)
{
    (void)user_data;
    if (!s_editor) return;

    Qs_Scene *scene = qs_scene_active();
    Qs_Entity entity = editor_selected_entity(s_editor);
    if (!scene || entity == QS_ENTITY_INVALID) return;

    const char *text = ca_input_get_text(input);
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

    const char *text = ca_input_get_text(input);
    snprintf(tag->tag, sizeof(tag->tag), "%s", text ? text : "");
}

/* ================================================================
   BUILD (called once during editor init)
   ================================================================ */

void ed_inspector(void *editor)
{
    s_editor = (Editor *)editor;
    s_prev_entity = QS_ENTITY_INVALID;

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
            /* Entity name input */
            s_entity_name_input = ca_input(&(Ca_InputDesc){
                .text      = "",
                .style     = "inspector-entity-input",
                .on_change = on_entity_name_input,
            });

            /* ID row: "ID  123" */
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_HORIZONTAL,
                .style     = "inspector-meta-row",
            });
            ca_text(&(Ca_TextDesc){
                .text  = "ID",
                .style = "inspector-meta-label",
            });
            s_id_value = ca_text(&(Ca_TextDesc){
                .text  = "0",
                .style = "inspector-meta-value",
            });
            ca_div_end();

            /* Tag row: "Tag [_____]" */
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_HORIZONTAL,
                .style     = "inspector-meta-row",
            });
            ca_text(&(Ca_TextDesc){
                .text  = "Tag",
                .style = "inspector-meta-label",
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
        ca_hr(&(Ca_HrDesc){ .color = ca_color(0.10f, 0.10f, 0.10f, 1.0f) });

        /* ---- Component sections ---- */
        for (int s = 0; s < MAX_SECTIONS; s++) {
            InspectorSection *sec = &s_sections[s];

            /* Section header: icon + name */
            sec->header_div = ca_div_begin(&(Ca_DivDesc){
                .direction = CA_HORIZONTAL,
                .style     = "inspector-section-header",
                .hidden    = true,
            });
            sec->header_icon = ca_text(&(Ca_TextDesc){
                .text  = "",
                .style = "inspector-section-icon",
            });
            sec->header_name = ca_text(&(Ca_TextDesc){
                .text  = "",
                .style = "inspector-section-name",
            });
            ca_div_end();

            /* Field widgets */
            for (int f = 0; f < MAX_FIELDS; f++) {
                FieldWidget *fw = &sec->fields[f];

                fw->row = ca_div_begin(&(Ca_DivDesc){
                    .direction = CA_HORIZONTAL,
                    .style     = "inspector-field-row",
                    .hidden    = true,
                });
                {
                    /* Field name label */
                    fw->name_label = ca_text(&(Ca_TextDesc){
                        .text  = "",
                        .style = "inspector-field-name",
                    });

                    /* Scalar input (float, int, uint, string) */
                    fw->scalar_input = ca_input(&(Ca_InputDesc){
                        .text        = "",
                        .style       = "inspector-scalar-input",
                        .on_change   = on_field_input,
                        .change_data = &fw->scalar_bind,
                        .hidden      = true,
                    });

                    /* Vec row container (float2, float3, float4) */
                    fw->vec_div = ca_div_begin(&(Ca_DivDesc){
                        .direction = CA_HORIZONTAL,
                        .style     = "inspector-vec-row",
                        .hidden    = true,
                    });
                    for (int v = 0; v < MAX_VEC_ELEMS; v++) {
                        fw->axis_groups[v] = ca_div_begin(&(Ca_DivDesc){
                            .direction = CA_HORIZONTAL,
                            .style     = "inspector-axis-group",
                        });
                        fw->axis_labels[v] = ca_text(&(Ca_TextDesc){
                            .text  = s_axis_text[v],
                            .style = s_axis_style[v],
                        });
                        fw->axis_inputs[v] = ca_input(&(Ca_InputDesc){
                            .text        = "0.000",
                            .style       = "inspector-vec-input",
                            .on_change   = on_field_input,
                            .change_data = &fw->axis_binds[v],
                        });
                        ca_div_end();
                    }
                    ca_div_end(); /* vec_div */

                    /* Bool checkbox */
                    fw->bool_check = ca_checkbox(&(Ca_CheckboxDesc){
                        .text        = "",
                        .on_change   = on_bool_input,
                        .change_data = &fw->bool_bind,
                        .hidden      = true,
                    });

                    /* Entity reference (read-only) */
                    fw->entity_label = ca_text(&(Ca_TextDesc){
                        .text   = "",
                        .style  = "inspector-field-value",
                        .hidden = true,
                    });
                }
                ca_div_end(); /* row */
            }
        }
    }
    ca_div_end(); /* inspector-scroll */
}

/* ================================================================
   HIDE ALL — reset pool visibility
   ================================================================ */

static void hide_all(void)
{
    ca_div_set_hidden(s_header_div, true);
    ca_label_set_hidden(s_no_selection, true);

    for (int s = 0; s < MAX_SECTIONS; s++) {
        InspectorSection *sec = &s_sections[s];
        ca_div_set_hidden(sec->header_div, true);
        for (int f = 0; f < MAX_FIELDS; f++)
            ca_div_set_hidden(sec->fields[f].row, true);
    }
}

/* ================================================================
   POPULATE FIELD — show the right widget for a field type
   ================================================================ */

static void populate_field(FieldWidget *fw, Qs_ComponentType *ct,
                           const Qs_FieldInfo *fi, const void *comp)
{
    ca_div_set_hidden(fw->row, false);
    ca_label_set_text(fw->name_label, fi->name);

    const void *field_ptr = (const char *)comp + fi->offset;
    char buf[64];

    /* Reset sub-widget visibility */
    ca_input_set_hidden(fw->scalar_input, true);
    ca_div_set_hidden(fw->vec_div, true);
    ca_checkbox_set_hidden(fw->bool_check, true);
    ca_label_set_hidden(fw->entity_label, true);

    switch (fi->type) {
    case QS_FIELD_FLOAT: {
        fw->scalar_bind = (InputBinding){
            .comp_type = ct, .field_offset = fi->offset,
            .field_size = fi->size, .field_type = fi->type,
        };
        snprintf(buf, sizeof(buf), "%.3f", *(const float *)field_ptr);
        ca_input_set_text(fw->scalar_input, buf);
        ca_input_set_hidden(fw->scalar_input, false);
        break;
    }
    case QS_FIELD_INT32: {
        fw->scalar_bind = (InputBinding){
            .comp_type = ct, .field_offset = fi->offset,
            .field_size = fi->size, .field_type = fi->type,
        };
        snprintf(buf, sizeof(buf), "%d", *(const int32_t *)field_ptr);
        ca_input_set_text(fw->scalar_input, buf);
        ca_input_set_hidden(fw->scalar_input, false);
        break;
    }
    case QS_FIELD_UINT32: {
        fw->scalar_bind = (InputBinding){
            .comp_type = ct, .field_offset = fi->offset,
            .field_size = fi->size, .field_type = fi->type,
        };
        snprintf(buf, sizeof(buf), "%u", *(const uint32_t *)field_ptr);
        ca_input_set_text(fw->scalar_input, buf);
        ca_input_set_hidden(fw->scalar_input, false);
        break;
    }
    case QS_FIELD_STRING: {
        fw->scalar_bind = (InputBinding){
            .comp_type = ct, .field_offset = fi->offset,
            .field_size = fi->size, .field_type = fi->type,
        };
        ca_input_set_text(fw->scalar_input, (const char *)field_ptr);
        ca_input_set_hidden(fw->scalar_input, false);
        break;
    }
    case QS_FIELD_FLOAT2:
    case QS_FIELD_FLOAT3:
    case QS_FIELD_FLOAT4: {
        uint32_t n = fi->type == QS_FIELD_FLOAT2 ? 2 :
                     fi->type == QS_FIELD_FLOAT3 ? 3 : 4;
        const float *v = (const float *)field_ptr;

        ca_div_set_hidden(fw->vec_div, false);
        for (uint32_t i = 0; i < MAX_VEC_ELEMS; i++) {
            ca_div_set_hidden(fw->axis_groups[i], i >= n);
            if (i < n) {
                fw->axis_binds[i] = (InputBinding){
                    .comp_type = ct, .field_offset = fi->offset,
                    .field_size = fi->size, .vec_index = i,
                    .field_type = fi->type,
                };
                snprintf(buf, sizeof(buf), "%.3f", v[i]);
                ca_input_set_text(fw->axis_inputs[i], buf);
            }
        }
        break;
    }
    case QS_FIELD_BOOL: {
        fw->bool_bind = (InputBinding){
            .comp_type = ct, .field_offset = fi->offset,
            .field_size = fi->size, .field_type = fi->type,
        };
        ca_checkbox_set(fw->bool_check, *(const bool *)field_ptr);
        ca_checkbox_set_hidden(fw->bool_check, false);
        break;
    }
    case QS_FIELD_ENTITY: {
        Qs_Entity ref = *(const Qs_Entity *)field_ptr;
        if (ref == QS_ENTITY_INVALID)
            snprintf(buf, sizeof(buf), "(none)");
        else
            snprintf(buf, sizeof(buf), "Entity %u", ref);
        ca_label_set_text(fw->entity_label, buf);
        ca_label_set_hidden(fw->entity_label, false);
        break;
    }
    default: break;
    }
}

/* ================================================================
   UPDATE (called every frame)
   ================================================================ */

void ed_inspector_update(void *editor)
{
    Editor *ed = (Editor *)editor;
    Qs_Entity entity = editor_selected_entity(ed);
    Qs_Scene *scene  = qs_scene_active();

    /* No selection — show placeholder */
    if (entity == QS_ENTITY_INVALID || !scene ||
        !qs_entity_valid(scene, entity))
    {
        if (s_prev_entity != QS_ENTITY_INVALID) {
            hide_all();
            ca_label_set_hidden(s_no_selection, false);
            s_prev_entity = QS_ENTITY_INVALID;
        }
        return;
    }

    /* Only refresh when entity selection changes */
    if (entity == s_prev_entity)
        return;

    hide_all();
    ca_label_set_hidden(s_no_selection, true);

    /* ---- Entity header ---- */
    ca_div_set_hidden(s_header_div, false);

    const char *name = qs_entity_name(scene, entity);
    ca_input_set_text(s_entity_name_input, name ? name : "");

    /* ID (read-only) */
    Qs_IdComp *id_comp = (Qs_IdComp *)qs_entity_get(
                              scene, entity, qs_id_comp_type());
    if (id_comp) {
        char id_buf[16];
        snprintf(id_buf, sizeof(id_buf), "%u", id_comp->id);
        ca_label_set_text(s_id_value, id_buf);
    }

    /* Tag */
    Qs_TagComp *tag_comp = (Qs_TagComp *)qs_entity_get(
                                scene, entity, qs_tag_comp_type());
    if (tag_comp)
        ca_input_set_text(s_tag_input, tag_comp->tag);

    /* ---- Component sections ---- */
    uint32_t type_count = qs_component_type_count();
    uint32_t sec_idx = 0;

    for (uint32_t t = 0; t < type_count && sec_idx < MAX_SECTIONS; t++) {
        Qs_ComponentType *ct = qs_component_type_at(t);
        if (!ct) continue;
        if (!qs_entity_has(scene, entity, ct)) continue;

        /* Skip IdComp and TagComp — shown in header */
        const char *comp_name = qs_component_type_name(ct);
        if (strcmp(comp_name, "IdComp") == 0 ||
            strcmp(comp_name, "TagComp") == 0)
            continue;

        InspectorSection *sec = &s_sections[sec_idx];

        /* Section header */
        ca_div_set_hidden(sec->header_div, false);
        ca_label_set_text(sec->header_icon, component_icon(comp_name));
        ca_label_set_color(sec->header_icon, component_icon_color(comp_name));
        ca_label_set_text(sec->header_name, comp_name);

        /* Populate fields using reflection */
        const Qs_TypeInfo *info = qs_component_type_info(ct);
        void *data = qs_entity_get(scene, entity, ct);

        if (info && data) {
            uint32_t field_count = info->field_count;
            if (field_count > MAX_FIELDS)
                field_count = MAX_FIELDS;

            for (uint32_t f = 0; f < field_count; f++)
                populate_field(&sec->fields[f], ct,
                               &info->fields[f], data);
        }

        sec_idx++;
    }

    s_prev_entity = entity;
}
