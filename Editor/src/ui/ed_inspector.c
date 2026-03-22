#include "ed_inspector.h"
#include "editor.h"

#include <stdio.h>
#include <string.h>

/* ================================================================
   INSPECTOR PANEL — reflection-driven component property display
   ================================================================

   Pre-allocates a pool of labels and organises them into component
   sections.  Each frame, ed_inspector_update() populates the labels
   based on the currently selected entity and its components.

   Any component type registered with Qs_TypeInfo reflection
   automatically gets its fields displayed — no hard-coding needed.
   ================================================================ */

/* Pool sizes */
#define MAX_SECTIONS        8
#define MAX_FIELDS         10

/* Nerd Font / FA icons */
#define ICON_COMPONENT  "\xEF\x80\x93"   /* U+F013 cog       */
#define ICON_TRANSFORM  "\xEF\x82\xB2"   /* U+F0B2 arrows    */
#define ICON_MESH       "\xEF\x86\xB2"   /* U+F1B2 cube      */
#define ICON_LIGHT      "\xEF\x83\xAB"   /* U+F0EB lightbulb */
#define ICON_VISIBLE    "\xEF\x81\xAE"   /* U+F06E eye       */

/* ---- Pre-allocated widget pool ---- */

typedef struct {
    Ca_Label *name;
    Ca_Label *value;
} FieldRow;

typedef struct {
    Ca_Label  *header_icon;
    Ca_Label  *header_name;
    FieldRow   fields[MAX_FIELDS];
} InspectorSection;

static Ca_Label         *s_entity_name;
static Ca_Label         *s_no_selection;
static InspectorSection  s_sections[MAX_SECTIONS];
static Qs_Entity         s_prev_entity;

/* ---- Field value formatting ---- */

static void format_float(char *buf, size_t sz, const void *ptr)
{
    snprintf(buf, sz, "%.3f", *(const float *)ptr);
}

static void format_float2(char *buf, size_t sz, const void *ptr)
{
    const float *v = (const float *)ptr;
    snprintf(buf, sz, "X: %.3f   Y: %.3f", v[0], v[1]);
}

static void format_float3(char *buf, size_t sz, const void *ptr)
{
    const float *v = (const float *)ptr;
    snprintf(buf, sz, "X: %.3f   Y: %.3f   Z: %.3f", v[0], v[1], v[2]);
}

static void format_float4(char *buf, size_t sz, const void *ptr)
{
    const float *v = (const float *)ptr;
    snprintf(buf, sz, "X: %.3f   Y: %.3f   Z: %.3f   W: %.3f",
             v[0], v[1], v[2], v[3]);
}

static void format_int32(char *buf, size_t sz, const void *ptr)
{
    snprintf(buf, sz, "%d", *(const int32_t *)ptr);
}

static void format_uint32(char *buf, size_t sz, const void *ptr)
{
    snprintf(buf, sz, "%u", *(const uint32_t *)ptr);
}

static void format_bool(char *buf, size_t sz, const void *ptr)
{
    snprintf(buf, sz, "%s", *(const bool *)ptr ? "true" : "false");
}

static void format_string(char *buf, size_t sz, const void *ptr)
{
    snprintf(buf, sz, "%s", (const char *)ptr);
}

static void format_entity(char *buf, size_t sz, const void *ptr)
{
    Qs_Entity e = *(const Qs_Entity *)ptr;
    if (e == QS_ENTITY_INVALID)
        snprintf(buf, sz, "(none)");
    else
        snprintf(buf, sz, "Entity %u", e);
}

static void format_field_value(char *buf, size_t sz,
                               Qs_FieldType type, const void *ptr)
{
    switch (type) {
    case QS_FIELD_FLOAT:  format_float(buf, sz, ptr);  break;
    case QS_FIELD_FLOAT2: format_float2(buf, sz, ptr); break;
    case QS_FIELD_FLOAT3: format_float3(buf, sz, ptr); break;
    case QS_FIELD_FLOAT4: format_float4(buf, sz, ptr); break;
    case QS_FIELD_INT32:  format_int32(buf, sz, ptr);  break;
    case QS_FIELD_UINT32: format_uint32(buf, sz, ptr); break;
    case QS_FIELD_BOOL:   format_bool(buf, sz, ptr);   break;
    case QS_FIELD_STRING: format_string(buf, sz, ptr); break;
    case QS_FIELD_ENTITY: format_entity(buf, sz, ptr); break;
    default:              snprintf(buf, sz, "???");     break;
    }
}

/* Pick icon for a component by name */
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
   BUILD (called once)
   ================================================================ */

void ed_inspector(void *editor)
{
    (void)editor;

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

        /* Entity name header */
        s_entity_name = ca_text(&(Ca_TextDesc){
            .text   = "",
            .style  = "inspector-entity-name",
            .hidden = true,
        });

        /* Pre-allocate component sections */
        for (int s = 0; s < MAX_SECTIONS; s++) {
            InspectorSection *sec = &s_sections[s];

            /* Section header row: icon + name */
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_HORIZONTAL,
                .style     = "inspector-section-header",
            });
            sec->header_icon = ca_text(&(Ca_TextDesc){
                .text   = "",
                .style  = "inspector-section-icon",
                .hidden = true,
            });
            sec->header_name = ca_text(&(Ca_TextDesc){
                .text   = "",
                .style  = "inspector-section-name",
                .hidden = true,
            });
            ca_div_end();

            /* Field rows */
            for (int f = 0; f < MAX_FIELDS; f++) {
                ca_div_begin(&(Ca_DivDesc){
                    .direction = CA_HORIZONTAL,
                    .style     = "inspector-field-row",
                });
                sec->fields[f].name = ca_text(&(Ca_TextDesc){
                    .text   = "",
                    .style  = "inspector-field-name",
                    .hidden = true,
                });
                sec->fields[f].value = ca_text(&(Ca_TextDesc){
                    .text   = "",
                    .style  = "inspector-field-value",
                    .hidden = true,
                });
                ca_div_end();
            }
        }
    }
    ca_div_end();
}

/* ================================================================
   HIDE ALL — reset pool visibility
   ================================================================ */

static void hide_all(void)
{
    ca_label_set_hidden(s_entity_name, true);
    ca_label_set_hidden(s_no_selection, true);

    for (int s = 0; s < MAX_SECTIONS; s++) {
        InspectorSection *sec = &s_sections[s];
        ca_label_set_hidden(sec->header_icon, true);
        ca_label_set_hidden(sec->header_name, true);
        for (int f = 0; f < MAX_FIELDS; f++) {
            ca_label_set_hidden(sec->fields[f].name, true);
            ca_label_set_hidden(sec->fields[f].value, true);
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

    /* No selection */
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

    /* Always refresh values (component data changes each frame) */
    hide_all();

    /* Show entity name */
    const char *name = qs_entity_name(scene, entity);
    ca_label_set_text(s_entity_name, name ? name : "(unnamed)");
    ca_label_set_hidden(s_entity_name, false);

    /* Iterate registered component types */
    uint32_t type_count = qs_component_type_count();
    uint32_t sec_idx = 0;

    for (uint32_t t = 0; t < type_count && sec_idx < MAX_SECTIONS; t++) {
        Qs_ComponentType *ct = qs_component_type_at(t);
        if (!ct) continue;
        if (!qs_entity_has(scene, entity, ct)) continue;

        InspectorSection *sec = &s_sections[sec_idx];
        const char *comp_name = qs_component_type_name(ct);

        /* Section header */
        ca_label_set_text(sec->header_icon, component_icon(comp_name));
        ca_label_set_color(sec->header_icon, component_icon_color(comp_name));
        ca_label_set_hidden(sec->header_icon, false);

        ca_label_set_text(sec->header_name, comp_name);
        ca_label_set_hidden(sec->header_name, false);

        /* Render fields using reflection */
        const Qs_TypeInfo *info = qs_component_type_info(ct);
        void *data = qs_entity_get(scene, entity, ct);

        if (info && data) {
            uint32_t field_count = info->field_count;
            if (field_count > MAX_FIELDS)
                field_count = MAX_FIELDS;

            for (uint32_t f = 0; f < field_count; f++) {
                const Qs_FieldInfo *fi = &info->fields[f];
                FieldRow *row = &sec->fields[f];

                ca_label_set_text(row->name, fi->name);
                ca_label_set_hidden(row->name, false);

                char val_buf[256];
                const void *field_ptr = (const char *)data + fi->offset;
                format_field_value(val_buf, sizeof(val_buf),
                                   fi->type, field_ptr);
                ca_label_set_text(row->value, val_buf);
                ca_label_set_hidden(row->value, false);
            }
        }

        sec_idx++;
    }

    s_prev_entity = entity;
}
