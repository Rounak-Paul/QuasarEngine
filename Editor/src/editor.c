#include "editor.h"
#include "ed_camera.h"
#include "ui/ed_menu_bar.h"
#include "ui/ed_toolbar.h"
#include "ui/ed_layout.h"
#include "ui/ed_status_bar.h"
#include "ui/ed_inspector.h"
#include "ui/ed_hierarchy.h"
#include "ui/ed_plugin_manager.h"

#include "ui/ed_file_browser.h"

#include "quasar.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Editor {
    Qs_Engine     *engine;
    Qs_Project    *project;
    Qs_Renderer   *scene_renderer;
    Ca_Viewport   *scene_viewport;
    Qs_Entity      selected_entity;
    EditorCamera   cam;
    /* Dynamic menu bar host div — rebuilt every frame */
    Ca_Div        *menu_bar_div;
};

/* ---- Editor CSS theme (Quasar Dark) ---- */
/*
 * Color palette — professional dark game engine aesthetic
 *
 * Background hierarchy (darkest → lightest):
 *   #0d0d0f  void        (deepest inset, inputs, viewport bg)
 *   #111114  base        (primary panel background)
 *   #16161a  elevated    (panel chrome, toolbars)
 *   #1c1c22  surface     (tab bars, headers, section dividers)
 *   #242430  overlay     (hover states, selected items)
 *   #2e2e3e  border      (dividers, input borders)
 *
 * Accent colors:
 *   #6e8aff  primary     (active tabs, primary buttons — blue-violet)
 *   #c8d0ff  text-bright (labels, interactive text)
 *   #8890b0  text-muted  (secondary labels, hints)
 *   #4a4e6a  text-dim    (disabled, placeholder)
 *   #ff6b6b  danger      (errors, destructive)
 *   #6bffb8  success     (play, confirm)
 *   #ffd166  warning     (warnings)
 *   #ff8c42  orange      (scene objects, mesh icons)
 *
 * Axis colors (XYZ):
 *   #ff5370  x-axis      (red)
 *   #80ff80  y-axis      (green)
 *   #5b9cff  z-axis      (blue)
 *   #bf80ff  w-axis      (purple)
 */

static const char *g_editor_css =

    /* Root */
    ".editor-root {"
    "  background: #111114;"
    "  gap: 0px;"
    "  overflow: hidden;"
    "}"

    /* ---- Menu bar ---- */
    ".menu-bar {"
    "  background: #16161a;"
    "  width: 100%;"
    "  height: 24px;"
    "}"

    ".menu-bar-item {"
    "  padding-left: 10px;"
    "  padding-right: 10px;"
    "  align-items: center;"
    "  color: #8890b0;"
    "  font-size: 12px;"
    "}"

    /* ---- Toolbar ---- */
    ".toolbar {"
    "  background: #16161a;"
    "  width: 100%;"
    "  height: 28px;"
    "  align-items: center;"
    "  padding-left: 6px;"
    "  padding-right: 6px;"
    "}"

    /* ---- Panels ---- */
    ".panel {"
    "  background: #111114;"
    "  overflow: hidden;"
    "}"

    ".panel-viewport {"
    "  background: #0d0d0f;"
    "}"

    ".panel-bottom {"
    "  background: #111114;"
    "}"

    /* ---- Console ---- */
    ".console-scroll {"
    "  overflow-y: scroll;"
    "  padding: 4px;"
    "  flex-grow: 1;"
    "  align-items: flex-start;"
    "}"

    ".console-line {"
    "  font-size: 12px;"
    "  height: 16px;"
    "  width: 100%;"
    "  text-align: left;"
    "  color: #8890b0;"
    "}"

    /* ---- Panel tab bars ---- */
    ".panel-tab-bar {"
    "  background: #1c1c22;"
    "  height: 26px;"
    "  width: 100%;"
    "  align-items: center;"
    "  padding-left: 6px;"
    "  gap: 2px;"
    "}"

    ".panel-tab {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  height: 20px;"
    "  padding-left: 10px;"
    "  padding-right: 10px;"
    "}"

    ".active {"
    "  color: #6e8aff;"
    "}"

    /* ---- Status bar ---- */
    ".status-bar {"
    "  background: #0d0d0f;"
    "  width: 100%;"
    "  height: 20px;"
    "  align-items: center;"
    "  padding-left: 10px;"
    "}"

    ".status-text {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "}"

    /* ---- File browser ---- */

    ".fb-root {"
    "  width: 100%;"
    "  height: 100%;"
    "}"

    ".fb-title-bar {"
    "  background: #1c1c22;"
    "  height: 36px;"
    "  width: 100%;"
    "  padding-left: 14px;"
    "  padding-right: 6px;"
    "  align-items: center;"
    "}"

    ".fb-title {"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "  flex-grow: 1;"
    "}"

    ".fb-spacer-grow {"
    "  flex-grow: 1;"
    "}"

    ".fb-close-btn {"
    "  width: 26px;"
    "  height: 26px;"
    "  background: transparent;"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  corner-radius: 4px;"
    "}"

    ".fb-nav-bar {"
    "  background: #111114;"
    "  height: 38px;"
    "  width: 100%;"
    "  padding-left: 8px;"
    "  padding-right: 8px;"
    "  align-items: center;"
    "  gap: 4px;"
    "}"

    ".fb-nav-btn {"
    "  width: 28px;"
    "  height: 24px;"
    "  background: #1c1c22;"
    "  color: #8890b0;"
    "  font-size: 12px;"
    "  corner-radius: 4px;"
    "}"

    ".fb-path-input {"
    "  flex-grow: 1;"
    "  height: 26px;"
    "  background: #0d0d0f;"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  padding-left: 8px;"
    "  corner-radius: 4px;"
    "}"

    ".fb-col-header {"
    "  width: 100%;"
    "  height: 22px;"
    "  padding-left: 14px;"
    "  padding-right: 14px;"
    "  align-items: center;"
    "  background: #1c1c22;"
    "}"

    ".fb-col-name {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  flex-grow: 1;"
    "  text-align: left;"
    "}"

    ".fb-col-size {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  width: 80px;"
    "  text-align: right;"
    "}"

    ".fb-file-list {"
    "  flex-grow: 1;"
    "  overflow-y: scroll;"
    "  padding: 4px;"
    "  gap: 2px;"
    "  align-items: flex-start;"
    "  background: #111114;"
    "}"

    ".fb-entry {"
    "  width: 100%;"
    "  height: 26px;"
    "  background: transparent;"
    "  color: #8890b0;"
    "  font-size: 12px;"
    "  text-align: left;"
    "  padding-left: 8px;"
    "  corner-radius: 3px;"
    "}"

    ".fb-entry-selected {"
    "  background: #242430;"
    "}"

    ".fb-entry-dir {"
    "  color: #6e8aff;"
    "}"

    ".fb-empty {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  padding: 14px;"
    "}"

    ".fb-bottom {"
    "  background: #1c1c22;"
    "  width: 100%;"
    "  padding: 6px 10px;"
    "  gap: 6px;"
    "}"

    ".fb-bottom-row {"
    "  width: 100%;"
    "  height: 26px;"
    "  align-items: center;"
    "  gap: 8px;"
    "}"

    ".fb-label {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  width: 50px;"
    "  text-align: right;"
    "}"

    ".fb-selected-name {"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  flex-grow: 1;"
    "  text-align: left;"
    "}"

    ".fb-filter-select {"
    "  width: 200px;"
    "}"

    ".fb-btn {"
    "  width: 80px;"
    "  height: 26px;"
    "  background: #242430;"
    "  color: #8890b0;"
    "  font-size: 12px;"
    "  corner-radius: 4px;"
    "}"

    ".fb-btn-primary {"
    "  background: #6e8aff;"
    "  color: #0d0d0f;"
    "}"

    /* ---- Hierarchy panel ---- */

    ".hierarchy-tree {"
    "  overflow-y: scroll;"
    "  padding-left: 6px;"
    "  padding-right: 4px;"
    "  padding-top: 2px;"
    "  gap: 0px;"
    "  flex-grow: 1;"
    "  align-items: flex-start;"
    "}"

    ".hierarchy-scene {"
    "  color: #6e8aff;"
    "  font-size: 12px;"
    "  height: 16px;"
    "}"

    ".hierarchy-entity {"
    "  color: #8890b0;"
    "  font-size: 12px;"
    "  height: 16px;"
    "}"

    ".hierarchy-component {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  height: 16px;"
    "}"

    ".hierarchy-add-btn {"
    "  width: 100%;"
    "  height: 24px;"
    "  background: transparent;"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "}"

    /* ---- Inspector panel ---- */

    ".inspector-scroll {"
    "  overflow-y: scroll;"
    "  padding: 4px 0px;"
    "  gap: 1px;"
    "  flex-grow: 1;"
    "  width: 100%;"
    "  align-items: flex-start;"
    "}"

    ".inspector-placeholder {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  padding: 14px 0px 0px 8px;"
    "  width: 100%;"
    "  text-align: left;"
    "}"

    ".inspector-header {"
    "  width: 100%;"
    "  gap: 4px;"
    "  padding: 6px 8px;"
    "  align-items: flex-start;"
    "}"

    ".inspector-entity-input {"
    "  width: 100%;"
    "  height: 22px;"
    "  background: #0d0d0f;"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "  padding-left: 6px;"
    "  corner-radius: 3px;"
    "}"

    ".inspector-meta-row {"
    "  width: 100%;"
    "  height: 18px;"
    "  gap: 4px;"
    "  align-items: center;"
    "  padding-left: 0px;"
    "}"

    ".inspector-tag-input {"
    "  height: 18px;"
    "  flex-grow: 1;"
    "  background: #0d0d0f;"
    "  color: #8890b0;"
    "  font-size: 12px;"
    "  padding-left: 4px;"
    "  corner-radius: 3px;"
    "}"

    ".inspector-section-header {"
    "  background: #1c1c22;"
    "  width: 100%;"
    "  height: 20px;"
    "  padding: 0px 8px;"
    "  margin-top: 4px;"
    "  align-items: center;"
    "}"

    ".inspector-section-label {"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "  color: #8890b0;"
    "  width: 100%;"
    "  text-align: left;"
    "}"

    ".inspector-field-row {"
    "  width: 100%;"
    "  padding: 2px 6px;"
    "  gap: 2px;"
    "  align-items: flex-start;"
    "}"

    ".inspector-field-name {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  text-align: left;"
    "  width: 100%;"
    "}"

    ".inspector-vec-row {"
    "  width: 100%;"
    "  gap: 0px;"
    "  align-items: center;"
    "  flex-wrap: wrap;"
    "}"

    ".inspector-axis-x { color: #ff5370; font-size: 12px; font-weight: bold; width: 10px; }"
    ".inspector-axis-y { color: #80ff80; font-size: 12px; font-weight: bold; width: 10px; }"
    ".inspector-axis-z { color: #5b9cff; font-size: 12px; font-weight: bold; width: 10px; }"
    ".inspector-axis-w { color: #bf80ff; font-size: 12px; font-weight: bold; width: 10px; }"

    ".inspector-axis-group {"
    "  width: 68px;"
    "  padding-left: 4px;"
    "  gap: 2px;"
    "  align-items: center;"
    "}"

    ".inspector-vec-input {"
    "  width: 50px;"
    "  height: 17px;"
    "  background: #0d0d0f;"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  padding-left: 4px;"
    "  corner-radius: 3px;"
    "}"

    ".inspector-scalar-input {"
    "  width: 100%;"
    "  height: 17px;"
    "  background: #0d0d0f;"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  padding-left: 4px;"
    "  corner-radius: 3px;"
    "}"

    ".inspector-field-value {"
    "  color: #8890b0;"
    "  font-size: 12px;"
    "}"

    ".inspector-id-label {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  width: 100%;"
    "  text-align: left;"
    "  padding-left: 2px;"
    "}"

    ".inspector-tag-icon {"
    "  color: #6bffb8;"
    "  font-size: 12px;"
    "  width: 16px;"
    "  flex-shrink: 0;"
    "}"

    ".hierarchy-selected {"
    "  background: #242430;"
    "}"

    /* ---- Menu bar host ---- */
    ".menu-bar-host {"
    "  width: 100%;"
    "  height: 24px;"
    "  flex-shrink: 0;"
    "}"


    /* ---- Plugin Manager modal ---- */
    ".plugin-manager-modal {"
    "  width: 520px;"
    "  height: 440px;"
    "  background: #16161a;"
    "  corner-radius: 8px;"
    "  padding: 0px;"
    "  gap: 0px;"
    "}"

    ".plugin-manager-titlebar {"
    "  background: #1c1c22;"
    "  width: 100%;"
    "  height: 32px;"
    "  padding: 0px 12px;"
    "  align-items: center;"
    "  corner-radius-top-left: 8px;"
    "  corner-radius-top-right: 8px;"
    "}"

    ".plugin-manager-title {"
    "  color: #c8d0ff;"
    "  font-size: 13px;"
    "  font-weight: bold;"
    "  flex-grow: 1;"
    "}"

    ".plugin-manager-close-btn {"
    "  background: transparent;"
    "  color: #8890b0;"
    "  font-size: 14px;"
    "  width: 24px;"
    "  height: 24px;"
    "  align-items: center;"
    "  text-align: center;"
    "}"

    ".plugin-manager-content {"
    "  width: 100%;"
    "  flex-grow: 1;"
    "  overflow-y: scroll;"
    "  padding: 8px;"
    "  gap: 0px;"
    "}"

    ".plugin-manager-empty {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  text-align: center;"
    "  padding: 16px;"
    "}"

    /* ---- Plugin list row ---- */
    ".plugin-row {"
    "  width: 100%;"
    "  background: #111114;"
    "  corner-radius: 6px;"
    "  padding: 8px;"
    "  margin-bottom: 4px;"
    "}"

    ".plugin-row-header {"
    "  width: 100%;"
    "  align-items: center;"
    "  gap: 6px;"
    "}"

    ".plugin-name {"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "}"

    ".plugin-version {"
    "  color: #4a4e6a;"
    "  font-size: 11px;"
    "}"

    ".plugin-spacer {"
    "  flex-grow: 1;"
    "}"

    ".plugin-status-loaded {"
    "  color: #6bffb8;"
    "  font-size: 11px;"
    "}"

    ".plugin-status-loading {"
    "  color: #ffd166;"
    "  font-size: 11px;"
    "}"

    ".plugin-status-disabled {"
    "  color: #4a4e6a;"
    "  font-size: 11px;"
    "}"

    ".plugin-reload-btn {"
    "  background: transparent;"
    "  color: #6e8aff;"
    "  font-size: 12px;"
    "  width: 22px;"
    "  height: 22px;"
    "  align-items: center;"
    "  text-align: center;"
    "}"

    ".plugin-description {"
    "  color: #8890b0;"
    "  font-size: 11px;"
    "  margin-top: 4px;"
    "}"

    ".plugin-author {"
    "  color: #4a4e6a;"
    "  font-size: 11px;"
    "  margin-top: 2px;"
    "}"

    /* ---- Renderer Settings modal ---- */
    ".renderer-settings-modal {"
    "  width: 340px;"
    "  height: auto;"
    "  background: #16161a;"
    "  corner-radius: 8px;"
    "  padding: 0px;"
    "  gap: 0px;"
    "}"

    ".renderer-settings-panel {"
    "  width: 100%;"
    "  padding: 0px;"
    "}"

    ".renderer-setting-row {"
    "  width: 100%;"
    "  padding: 8px 12px 4px 12px;"
    "  gap: 4px;"
    "}"

    ".renderer-setting-label {"
    "  color: #8890b0;"
    "  font-size: 12px;"
    "}"

    ".renderer-section-label {"
    "  color: #a0a8c8;"
    "  font-size: 11px;"
    "  padding: 8px 12px 2px 12px;"
    "}"

    /* ---- Renderer Stats modal ---- */
    ".renderer-stats-modal {"
    "  width: 260px;"
    "  height: auto;"
    "  background: #16161a;"
    "  corner-radius: 8px;"
    "  padding: 0px;"
    "  gap: 0px;"
    "}"

    ".renderer-stats-panel {"
    "  width: 100%;"
    "  padding: 0px;"
    "}"

    ".renderer-stat-row {"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  padding: 4px 12px;"
    "  font-family: monospace;"
    "}"
;

static void editor_build_ui(Editor *ed)
{
    Ca_Window *window = qs_engine_window(ed->engine);

    ca_ui_begin(window, &(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "editor-root",
    });

    ed->menu_bar_div = ed_menu_bar(window, ed);
    ed_toolbar(window, ed);
    ed_layout(window, ed);
    ed_status_bar(window, ed);

    ca_ui_end();
}

static void on_mouse_button(const Ca_Event *event, void *userdata)
{
    (void)userdata;
    qs_input_mouse_button_event((Qs_MouseButton)event->mouse_button.button,
                                event->mouse_button.action);
}

/* Fires synchronously before the plugin library is unloaded.
   Destroy any scene renderer that references the plugin's backend so
   the backend and its Vulkan context can be cleanly shut down. */
static bool on_plugin_reload_begin(const Qs_Event *e, void *userdata)
{
    (void)e;
    Editor *ed = (Editor *)userdata;
    if (ed->scene_renderer) {
        qs_renderer_destroy(ed->scene_renderer);
        ed->scene_renderer = NULL;
    }
    return false;
}

/* Fires synchronously after the plugin has been successfully reloaded.
   Recreate the scene renderer pointing at the freshly registered backend. */
static bool on_plugin_reload_end(const Qs_Event *e, void *userdata)
{
    (void)e;
    Editor *ed = (Editor *)userdata;
    if (!ed->scene_renderer) {
        ed->scene_renderer = qs_renderer_create(ed->engine, &(Qs_RendererDesc){
            .name        = "scene",
            .clear_color = { 0.0f, 0.0f, 0.0f, 1.0f },
            .depth_test  = true,
        });
        if (ed->scene_renderer) {
            /* Camera position/facing will be reapplied by ed_camera_update next frame */
        }
    }
    return false;
}

static void on_mouse_move(const Ca_Event *event, void *userdata)
{
    (void)userdata;
    qs_input_mouse_pos_event(event->mouse_pos.x, event->mouse_pos.y);
}

static void on_mouse_scroll(const Ca_Event *event, void *userdata)
{
    (void)userdata;
    qs_input_mouse_scroll_event(event->mouse_scroll.dx, event->mouse_scroll.dy);
}

static void on_key_event(const Ca_Event *event, void *userdata)
{
    (void)userdata;
    qs_input_key_event((Qs_Key)event->key.key,
                       (Qs_KeyAction)event->key.action,
                       event->key.mods);
}

/* Builds a column-major 4x4 TRS model matrix from a Qs_Transform. */
static void trs_to_matrix(const Qs_Transform *t, float m[16])
{
    if (!t) {
        for (int i = 0; i < 16; i++) m[i] = 0.0f;
        m[0] = m[5] = m[10] = m[15] = 1.0f;
        return;
    }
    float qx = t->rotation[0], qy = t->rotation[1];
    float qz = t->rotation[2], qw = t->rotation[3];
    float sx = t->scale[0],    sy = t->scale[1],    sz = t->scale[2];
    float px = t->position[0], py = t->position[1], pz = t->position[2];
    float xx = qx*qx, yy = qy*qy, zz = qz*qz;
    float xy = qx*qy, xz = qx*qz, yz = qy*qz;
    float wx = qw*qx, wy = qw*qy, wz = qw*qz;
    m[ 0] = (1.0f - 2.0f*(yy+zz)) * sx;  m[ 1] = (2.0f*(xy+wz)) * sx;
    m[ 2] = (2.0f*(xz-wy)) * sx;         m[ 3] = 0.0f;
    m[ 4] = (2.0f*(xy-wz)) * sy;         m[ 5] = (1.0f - 2.0f*(xx+zz)) * sy;
    m[ 6] = (2.0f*(yz+wx)) * sy;         m[ 7] = 0.0f;
    m[ 8] = (2.0f*(xz+wy)) * sz;         m[ 9] = (2.0f*(yz-wx)) * sz;
    m[10] = (1.0f - 2.0f*(xx+yy)) * sz;  m[11] = 0.0f;
    m[12] = px; m[13] = py; m[14] = pz;  m[15] = 1.0f;
}

static void on_frame(Qs_Engine *engine, void *userdata)
{
    (void)engine;
    Editor *ed = userdata;
    if (ed->scene_viewport)
        ca_viewport_request_redraw(ed->scene_viewport);
    ed_camera_update(&ed->cam, ed->scene_renderer, qs_engine_dt(ed->engine));

    /* Submit scene renderables and lights for this frame */
    Qs_Scene *scene = qs_scene_active();
    if (scene && ed->scene_renderer) {
        qs_renderer_clear_renderables(ed->scene_renderer);
        qs_renderer_clear_lights(ed->scene_renderer);

        for (Qs_Entity e = qs_scene_first(scene, qs_mesh_comp_type());
             e != QS_ENTITY_INVALID;
             e = qs_scene_next(scene, qs_mesh_comp_type(), e))
        {
            Qs_MeshComp  *mc = qs_entity_get(scene, e, qs_mesh_comp_type());
            if (!mc || !mc->visible || !mc->mesh || !mc->material) continue;
            Qs_Transform *tr = qs_entity_get(scene, e, qs_transform_type());
            Qs_RenderableDesc r;
            r.mesh            = mc->mesh;
            r.material        = mc->material;
            r.cast_shadows    = true;
            r.receive_shadows = true;
            r.bounds.min[0] = r.bounds.min[1] = r.bounds.min[2] = -100.0f;
            r.bounds.max[0] = r.bounds.max[1] = r.bounds.max[2] =  100.0f;
            trs_to_matrix(tr, r.transform);
            qs_renderer_submit_renderable(ed->scene_renderer, &r);
        }

        for (Qs_Entity e = qs_scene_first(scene, qs_light_comp_type());
             e != QS_ENTITY_INVALID;
             e = qs_scene_next(scene, qs_light_comp_type(), e))
        {
            Qs_LightComp *lc = qs_entity_get(scene, e, qs_light_comp_type());
            if (!lc || !lc->enabled) continue;
            qs_renderer_submit_light_comp(ed->scene_renderer, lc);
        }
    }

    ed_menu_bar_update(ed->menu_bar_div, qs_engine_window(ed->engine), ed);
    ed_hierarchy_update(ed);
    ed_console_update(ed);
    ed_inspector_update(ed);
    ed_file_browser_update();
}

static void on_log(void *userdata)
{
    (void)userdata;
    qs_engine_wake();
}

/* ---- Scene file loading ---- */

#define MAX_SCENE_RESOURCES 64

typedef struct {
    char  name[64];
    void *ptr;
} NamedResource;

static Qs_Mesh *find_mesh(NamedResource *res, int count, const char *name)
{
    for (int i = 0; i < count; i++)
        if (strcmp(res[i].name, name) == 0)
            return (Qs_Mesh *)res[i].ptr;
    return NULL;
}

static Qs_Material *find_material(NamedResource *res, int count, const char *name)
{
    for (int i = 0; i < count; i++)
        if (strcmp(res[i].name, name) == 0)
            return (Qs_Material *)res[i].ptr;
    return NULL;
}

static bool editor_load_scene(Editor *ed, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        QS_LOG_ERROR("Failed to open scene: %s", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return false; }

    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return false; }
    size_t nread = fread(buf, 1, (size_t)len, f);
    buf[nread] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        QS_LOG_ERROR("Failed to parse scene: %s", path);
        return false;
    }

    /* Scene name */
    const cJSON *name_val = cJSON_GetObjectItemCaseSensitive(root, "name");
    const char *scene_name = cJSON_IsString(name_val)
                           ? name_val->valuestring : "Untitled";

    Qs_Scene *scene = qs_scene_create(ed->engine, &(Qs_SceneDesc){
        .name = scene_name,
    });
    qs_scene_set_active(scene);

    /* ---- Create resources from the "resources" section ---- */
    NamedResource meshes[MAX_SCENE_RESOURCES];
    NamedResource materials[MAX_SCENE_RESOURCES];
    int mesh_count = 0, mat_count = 0;

    const cJSON *resources = cJSON_GetObjectItemCaseSensitive(root, "resources");
    if (resources) {
        /* Materials */
        const cJSON *mats_json =
            cJSON_GetObjectItemCaseSensitive(resources, "materials");
        const cJSON *mat_json;
        cJSON_ArrayForEach(mat_json, mats_json) {
            if (mat_count >= MAX_SCENE_RESOURCES) break;

            float base_color[4] = {1,1,1,1};
            const cJSON *bc =
                cJSON_GetObjectItemCaseSensitive(mat_json, "base_color");
            if (cJSON_IsArray(bc)) {
                for (int i = 0; i < 4 && i < cJSON_GetArraySize(bc); i++)
                    base_color[i] = (float)cJSON_GetArrayItem(bc, i)->valuedouble;
            }
            const cJSON *rough =
                cJSON_GetObjectItemCaseSensitive(mat_json, "roughness");
            const cJSON *metal =
                cJSON_GetObjectItemCaseSensitive(mat_json, "metallic");

            Qs_Material *m = qs_material_create(ed->engine, &(Qs_MaterialDesc){
                .name              = mat_json->string,
                .base_color_factor = { base_color[0], base_color[1],
                                       base_color[2], base_color[3] },
                .roughness_factor  = rough ? (float)rough->valuedouble : 0.5f,
                .metallic_factor   = metal ? (float)metal->valuedouble : 0.0f,
            });
            if (m) {
                snprintf(materials[mat_count].name, 64, "%s", mat_json->string);
                materials[mat_count].ptr = m;
                mat_count++;
            }
        }

        /* Meshes (primitives) */
        const cJSON *meshes_json =
            cJSON_GetObjectItemCaseSensitive(resources, "meshes");
        const cJSON *mesh_json;
        cJSON_ArrayForEach(mesh_json, meshes_json) {
            if (mesh_count >= MAX_SCENE_RESOURCES) break;
            const cJSON *type_val =
                cJSON_GetObjectItemCaseSensitive(mesh_json, "type");
            if (!cJSON_IsString(type_val)) continue;
            const char *type = type_val->valuestring;

            Qs_Mesh *m = NULL;
            if (strcmp(type, "plane") == 0) {
                const cJSON *sz =
                    cJSON_GetObjectItemCaseSensitive(mesh_json, "size");
                const cJSON *sd =
                    cJSON_GetObjectItemCaseSensitive(mesh_json, "subdivisions");
                m = qs_primitive_plane(ed->engine,
                        sz ? (float)sz->valuedouble : 1.0f,
                        sd ? (uint32_t)sd->valueint  : 1);
            } else if (strcmp(type, "cube") == 0) {
                const cJSON *sz =
                    cJSON_GetObjectItemCaseSensitive(mesh_json, "size");
                m = qs_primitive_cube(ed->engine,
                        sz ? (float)sz->valuedouble : 1.0f);
            } else if (strcmp(type, "sphere") == 0) {
                const cJSON *rad =
                    cJSON_GetObjectItemCaseSensitive(mesh_json, "radius");
                const cJSON *sl =
                    cJSON_GetObjectItemCaseSensitive(mesh_json, "slices");
                const cJSON *st =
                    cJSON_GetObjectItemCaseSensitive(mesh_json, "stacks");
                m = qs_primitive_sphere(ed->engine,
                        rad ? (float)rad->valuedouble    : 0.5f,
                        sl  ? (uint32_t)sl->valueint     : 24,
                        st  ? (uint32_t)st->valueint     : 16);
            }
            if (m) {
                snprintf(meshes[mesh_count].name, 64, "%s", mesh_json->string);
                meshes[mesh_count].ptr = m;
                mesh_count++;
            }
        }
    }

    /* ---- Load entities ---- */
    qs_scene_from_json(scene, ed->engine, root);

    /* ---- Resolve mesh/material name references ---- */
    for (Qs_Entity e = qs_scene_first(scene, qs_mesh_comp_type());
         e != QS_ENTITY_INVALID;
         e = qs_scene_next(scene, qs_mesh_comp_type(), e))
    {
        Qs_MeshComp *mc = (Qs_MeshComp *)qs_entity_get(
                              scene, e, qs_mesh_comp_type());
        if (!mc) continue;
        if (mc->mesh_name[0])
            mc->mesh = find_mesh(meshes, mesh_count, mc->mesh_name);
        if (mc->material_name[0])
            mc->material = find_material(materials, mat_count,
                                         mc->material_name);
    }

    /* Add a default directional sun light if the scene has no lights */
    if (qs_scene_first(scene, qs_light_comp_type()) == QS_ENTITY_INVALID) {
        Qs_Entity sun = qs_entity_create(scene, "Sun");
        Qs_LightComp *lc = qs_entity_add(scene, sun, qs_light_comp_type());
        if (lc) {
            lc->color[0]     = 1.0f;
            lc->color[1]     = 0.95f;
            lc->color[2]     = 0.9f;
            lc->intensity    = 3.0f;
        }
    }

    cJSON_Delete(root);
    QS_LOG_INFO("Scene loaded: %s", path);
    return true;
}

Editor *editor_create(const EditorDesc *desc)
{
    Editor *ed = calloc(1, sizeof(Editor));
    if (!ed) return NULL;
    ed->selected_entity = QS_ENTITY_INVALID;

    /* Open project */
    if (desc->project_path) {
        ed->project = qs_project_open(desc->project_path);
        if (!ed->project) {
            free(ed);
            return NULL;
        }
    }

    /* Build window title: "Quasar Editor — ProjectName" */
    char title[256];
    if (ed->project)
        snprintf(title, sizeof(title), "%s \xE2\x80\x94 %s",
                 desc->title ? desc->title : "Quasar Editor",
                 qs_project_name(ed->project));
    else
        snprintf(title, sizeof(title), "%s",
                 desc->title ? desc->title : "Quasar Editor");

    ed->engine = qs_engine_create(&(Qs_EngineDesc){
        .app_name      = title,
        .version_major = 0,
        .version_minor = 1,
        .version_patch = 0,
        .window_width  = desc->width,
        .window_height = desc->height,
    });
    if (!ed->engine) {
        free(ed);
        return NULL;
    }

    qs_engine_set_stylesheet(ed->engine, g_editor_css);

    ed->scene_renderer = qs_renderer_create(ed->engine, &(Qs_RendererDesc){
        .name        = "scene",
        .clear_color = { 0.0f, 0.0f, 0.0f, 1.0f },
        .depth_test  = true,
    });

    /* Position camera for a good view of the test scene */
    Qs_Camera *cam = qs_renderer_camera(ed->scene_renderer);
    cam->position[0] =  5.0f;
    cam->position[1] =  4.0f;
    cam->position[2] =  8.0f;
    cam->target[0]   =  0.0f;
    cam->target[1]   =  0.5f;
    cam->target[2]   =  0.0f;

    /* ---- Load scene from project ---- */
    if (ed->project) {
        char scene_path[512];
        snprintf(scene_path, sizeof(scene_path), "%s/scenes/default.qscene",
                 qs_project_path(ed->project));
        editor_load_scene(ed, scene_path);
    }

    editor_build_ui(ed);

    ed_plugin_manager_init(ed);
    ed_file_browser_init(ca_window_instance(qs_engine_window(ed->engine)));

    qs_engine_set_event_handler(ed->engine, CA_EVENT_KEY,          on_key_event,    ed);
    qs_engine_set_event_handler(ed->engine, CA_EVENT_MOUSE_BUTTON, on_mouse_button, ed);
    qs_engine_set_event_handler(ed->engine, CA_EVENT_MOUSE_MOVE,   on_mouse_move,   ed);
    qs_engine_set_event_handler(ed->engine, CA_EVENT_MOUSE_SCROLL, on_mouse_scroll, ed);
    qs_engine_set_on_frame(ed->engine, on_frame, ed);
    qs_log_set_listener(on_log, ed);

    /* Subscribe to plugin reload events to refresh the scene renderer */
    Qs_EventBus *bus = qs_engine_event_bus(ed->engine);
    qs_event_subscribe(bus, QS_EVENT_PLUGIN_RELOAD_BEGIN, on_plugin_reload_begin, ed);
    qs_event_subscribe(bus, QS_EVENT_PLUGIN_RELOAD_END,   on_plugin_reload_end,   ed);

    ed_camera_init(&ed->cam);

    return ed;
}

int editor_run(Editor *ed)
{
    if (!ed) return 1;
    return qs_engine_run(ed->engine);
}

void editor_request_exit(Editor *ed)
{
    if (ed) qs_engine_request_exit(ed->engine);
}

Qs_Renderer *editor_scene_renderer(Editor *ed)
{
    return ed ? ed->scene_renderer : NULL;
}

void editor_set_scene_viewport(Editor *ed, Ca_Viewport *viewport)
{
    if (ed) ed->scene_viewport = viewport;
}

Qs_Engine *editor_engine(Editor *ed)
{
    return ed ? ed->engine : NULL;
}

Qs_Entity editor_selected_entity(const Editor *ed)
{
    return ed ? ed->selected_entity : QS_ENTITY_INVALID;
}

void editor_set_selected_entity(Editor *ed, Qs_Entity entity)
{
    if (ed) ed->selected_entity = entity;
}

void editor_destroy(Editor *ed)
{
    if (!ed) return;
    qs_engine_destroy(ed->engine);
    qs_project_destroy(ed->project);
    free(ed);
}
