#include "ed_inspector.h"
#include "editor.h"
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

/* Nerd Font / FA icons */
#define ICON_COMPONENT  "\xEF\x80\x93"   /* U+F013 cog       */
#define ICON_TRANSFORM  "\xEF\x82\xB2"   /* U+F0B2 arrows    */
#define ICON_MESH       "\xEF\x86\xB2"   /* U+F1B2 cube      */
#define ICON_LIGHT      "\xEF\x83\xAB"   /* U+F0EB lightbulb */
#define ICON_ID         "\xEF\x8A\x92"   /* U+F292 hashtag    */
#define ICON_TAG        "\xEF\x81\x84"   /* U+F044 pencil     */

/* ---- Binding data for on_change callbacks ---- */

typedef struct InputBinding {
    Qs_ComponentType *comp_type;
    size_t            field_offset;
    size_t            field_size;
    uint32_t          vec_index;
    Qs_FieldType      field_type;
} InputBinding;

/* ---- Module state ---- */

static Editor       *s_editor;
static Ca_Label     *s_no_selection;
static Ca_Div       *s_header_div;
static Ca_TextInput *s_entity_name_input;
static Ca_Label     *s_id_value;
static Ca_TextInput *s_tag_input;
static Ca_Div       *s_content_div;      /* dynamic section container */
static Qs_Entity     s_prev_entity;

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
        s_binding_cap = s_binding_cap ? s_binding_cap * 2 : 32;
        s_bindings = realloc(s_bindings, s_binding_cap * sizeof(InputBinding));
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
    if (strcmp(name, "Transform") == 0) return ICON_TRANSFORM;
    if (strcmp(name, "MeshComp") == 0)  return ICON_MESH;
    if (strcmp(name, "LightComp") == 0) return ICON_LIGHT;
    return ICON_COMPONENT;
}

static uint32_t component_icon_color(const char *name)
{
    if (strcmp(name, "Transform") == 0) return CA_THEME_ACCENT;
    if (strcmp(name, "MeshComp")  == 0) return CA_THEME_SUCCESS;
    if (strcmp(name, "LightComp") == 0) return CA_THEME_WARNING;
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

static void build_field(Qs_ComponentType *ct, const Qs_FieldInfo *fi,
                        const void *comp)
{
    const void *field_ptr = (const char *)comp + fi->offset;
    char buf[64];

    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "inspector-field-row",
    });

    ca_text(&(Ca_TextDesc){
        .text  = fi->name,
        .style = "inspector-field-name",
    });

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

        ca_input(&(Ca_InputDesc){
            .text        = (fi->type == QS_FIELD_STRING)
                            ? (const char *)field_ptr : buf,
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

        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_HORIZONTAL,
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

            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_HORIZONTAL,
                .style     = "inspector-axis-group",
            });
            ca_text(&(Ca_TextDesc){
                .text  = s_axis_text[i],
                .style = s_axis_style[i],
            });
            ca_input(&(Ca_InputDesc){
                .text        = buf,
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
        ca_checkbox(&(Ca_CheckboxDesc){
            .text        = "",
            .checked     = *(const bool *)field_ptr,
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
        ca_text(&(Ca_TextDesc){
            .text  = buf,
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

        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_HORIZONTAL,
            .style     = "inspector-section-header",
        });
        ca_text(&(Ca_TextDesc){
            .text  = section_buf,
            .style = "inspector-section-label",
            .color = component_icon_color(comp_name),
        });
        ca_div_end();

        /* Fields */
        const Qs_TypeInfo *info = qs_component_type_info(ct);
        void *data = qs_entity_get(scene, entity, ct);

        if (info && data) {
            for (uint32_t f = 0; f < info->field_count; f++)
                build_field(ct, &info->fields[f], data);
        }
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
            ca_div_set_hidden(s_header_div, true);
            ca_label_set_hidden(s_no_selection, false);
            ca_div_clear(s_content_div);
            ca_div_end();
            free_bindings();
            s_prev_entity = QS_ENTITY_INVALID;
        }
        return;
    }

    /* Only rebuild when entity selection changes */
    if (entity == s_prev_entity)
        return;

    /* Tear down old content and enter for rebuild */
    ca_div_clear(s_content_div);
    free_bindings();

    ca_label_set_hidden(s_no_selection, true);

    /* ---- Entity header ---- */
    ca_div_set_hidden(s_header_div, false);

    const char *name = qs_entity_name(scene, entity);
    ca_input_set_text(s_entity_name_input, name ? name : "");

    Qs_IdComp *id_comp = (Qs_IdComp *)qs_entity_get(
                              scene, entity, qs_id_comp_type());
    if (id_comp) {
        char id_buf[32];
        snprintf(id_buf, sizeof(id_buf), ICON_ID "  %u", id_comp->id);
        ca_label_set_text(s_id_value, id_buf);
    }

    Qs_TagComp *tag_comp = (Qs_TagComp *)qs_entity_get(
                                scene, entity, qs_tag_comp_type());
    if (tag_comp)
        ca_input_set_text(s_tag_input, tag_comp->tag);

    /* ---- Build component sections dynamically ---- */
    build_sections(scene, entity);
    ca_div_end();

    s_prev_entity = entity;
}
