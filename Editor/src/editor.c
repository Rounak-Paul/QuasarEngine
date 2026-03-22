#include "editor.h"
#include "ui/ed_menu_bar.h"
#include "ui/ed_toolbar.h"
#include "ui/ed_layout.h"
#include "ui/ed_status_bar.h"

#include "ui/ed_file_browser.h"

#include <stdio.h>
#include <stdlib.h>

struct Editor {
    Qs_Engine     *engine;
    Qs_Project    *project;
    Qs_Renderer   *scene_renderer;
    Ca_Viewport   *scene_viewport;
};

/* ---- Editor CSS theme ---- */

static const char *g_editor_css =

    /* Root — dark indigo background */
    ".editor-root {"
    "  background: #1a1a2e;"
    "  gap: 0px;"
    "  overflow: hidden;"
    "}"

    /* Menu bar — slim, dark */
    ".menu-bar {"
    "  background: #16162a;"
    "  width: 100%;"
    "  height: 22px;"
    "}"

    /* Menu bar header items */
    ".menu-bar-item {"
    "  padding-left: 8px;"
    "  padding-right: 8px;"
    "  align-items: center;"
    "}"

    /* Toolbar — icon row */
    ".toolbar {"
    "  background: #16162a;"
    "  width: 100%;"
    "  height: 18px;"
    "  align-items: center;"
    "  padding-left: 4px;"
    "  padding-right: 4px;"
    "  border-width: 1px;"
    "  border-color: #12122a;"
    "}"

    /* Panels */
    ".panel {"
    "  background: #16162a;"
    "  overflow: hidden;"
    "}"

    ".panel-viewport {"
    "  background: #1a1a2e;"
    "}"

    ".panel-bottom {"
    "  background: #16162a;"
    "}"

    /* Console */
    ".console-scroll {"
    "  overflow-y: scroll;"
    "  padding: 4px;"
    "  flex-grow: 1;"
    "  align-items: flex-start;"
    "}"

    ".console-line {"
    "  font-size: 11px;"
    "  height: 14px;"
    "  width: 100%;"
    "  text-align: left;"
    "}"

    /* Panel tab bars (row of tabs at top of each panel) */
    ".panel-tab-bar {"
    "  background: #12122a;"
    "  height: 24px;"
    "  width: 100%;"
    "  align-items: center;"
    "  padding-left: 4px;"
    "  gap: 2px;"
    "}"

    /* Panel tabs (individual tab labels) */
    ".panel-tab {"
    "  color: #555566;"
    "  font-size: 11px;"
    "  padding-left: 8px;"
    "  padding-right: 8px;"
    "}"

    ".active {"
    "  color: #b0b0cc;"
    "}"

    /* Status bar */
    ".status-bar {"
    "  background: #12122a;"
    "  width: 100%;"
    "  height: 20px;"
    "  align-items: center;"
    "  padding-left: 8px;"
    "}"

    ".status-text {"
    "  color: #6a6a88;"
    "  font-size: 11px;"
    "}"

    /* ---- File browser ---- */

    ".fb-root {"
    "  width: 100%;"
    "  height: 100%;"
    "}"

    ".fb-title-bar {"
    "  background: #14142a;"
    "  height: 32px;"
    "  width: 100%;"
    "  padding-left: 10px;"
    "  padding-right: 4px;"
    "  align-items: center;"
    "}"

    ".fb-title {"
    "  color: #c0c0d8;"
    "  font-size: 12px;"
    "}"

    ".fb-spacer-grow {"
    "  flex-grow: 1;"
    "}"

    ".fb-close-btn {"
    "  width: 28px;"
    "  height: 24px;"
    "  background: transparent;"
    "  color: #7777aa;"
    "  font-size: 12px;"
    "  corner-radius: 3px;"
    "}"

    ".fb-nav-bar {"
    "  background: #18182e;"
    "  height: 34px;"
    "  width: 100%;"
    "  padding-left: 4px;"
    "  padding-right: 4px;"
    "  align-items: center;"
    "  gap: 2px;"
    "}"

    ".fb-nav-btn {"
    "  width: 28px;"
    "  height: 24px;"
    "  background: #222240;"
    "  color: #8888aa;"
    "  font-size: 12px;"
    "  corner-radius: 3px;"
    "}"

    ".fb-path-input {"
    "  flex-grow: 1;"
    "  height: 24px;"
    "  background: #10102a;"
    "  color: #c0c0d8;"
    "  font-size: 11px;"
    "  padding-left: 6px;"
    "  corner-radius: 3px;"
    "}"

    ".fb-col-header {"
    "  width: 100%;"
    "  height: 20px;"
    "  padding-left: 12px;"
    "  padding-right: 12px;"
    "  align-items: center;"
    "  background: #16162c;"
    "}"

    ".fb-col-name {"
    "  color: #666688;"
    "  font-size: 10px;"
    "  flex-grow: 1;"
    "  text-align: left;"
    "}"

    ".fb-col-size {"
    "  color: #666688;"
    "  font-size: 10px;"
    "  width: 80px;"
    "  text-align: right;"
    "}"

    ".fb-file-list {"
    "  flex-grow: 1;"
    "  overflow-y: scroll;"
    "  padding: 2px;"
    "  gap: 1px;"
    "  align-items: flex-start;"
    "}"

    ".fb-entry {"
    "  width: 100%;"
    "  height: 22px;"
    "  background: transparent;"
    "  color: #b0b0cc;"
    "  font-size: 11px;"
    "  text-align: left;"
    "  padding-left: 4px;"
    "  corner-radius: 2px;"
    "}"

    ".fb-entry-selected {"
    "  background: #2a2a55;"
    "}"

    ".fb-entry-dir {"
    "  color: #7799dd;"
    "}"

    ".fb-empty {"
    "  color: #555570;"
    "  font-size: 11px;"
    "  padding: 8px;"
    "}"

    ".fb-bottom {"
    "  background: #18182e;"
    "  width: 100%;"
    "  padding: 6px 8px;"
    "  gap: 4px;"
    "}"

    ".fb-bottom-row {"
    "  width: 100%;"
    "  height: 26px;"
    "  align-items: center;"
    "  gap: 6px;"
    "}"

    ".fb-label {"
    "  color: #8888aa;"
    "  font-size: 11px;"
    "  width: 44px;"
    "  text-align: right;"
    "}"

    ".fb-selected-name {"
    "  color: #c0c0d8;"
    "  font-size: 11px;"
    "  flex-grow: 1;"
    "  text-align: left;"
    "}"

    ".fb-filter-select {"
    "  width: 200px;"
    "}"

    ".fb-btn {"
    "  width: 90px;"
    "  height: 24px;"
    "  background: #252540;"
    "  color: #b0b0cc;"
    "  font-size: 11px;"
    "  corner-radius: 3px;"
    "}"

    ".fb-btn-primary {"
    "  background: #3355aa;"
    "  color: #e0e0f0;"
    "}"
;

static void editor_build_ui(Editor *ed)
{
    Ca_Window *window = qs_engine_window(ed->engine);

    ca_ui_begin(window, &(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "editor-root",
    });

    ed_menu_bar(window, ed);
    ed_toolbar(window, ed);
    ed_layout(window, ed);
    ed_status_bar(window, ed);

    ca_ui_end();
}

static void on_key_event(const Ca_Event *event, void *userdata)
{
    (void)userdata;
    qs_input_key_event((Qs_Key)event->key.key,
                       (Qs_KeyAction)event->key.action,
                       event->key.mods);
}

static void on_frame(Qs_Engine *engine, void *userdata)
{
    (void)engine;
    Editor *ed = userdata;
    if (ed->scene_viewport)
        ca_viewport_request_redraw(ed->scene_viewport);
    ed_console_update(ed);
    ed_file_browser_update();
}

static void on_log(void *userdata)
{
    (void)userdata;
    qs_engine_wake();
}

Editor *editor_create(const EditorDesc *desc)
{
    Editor *ed = calloc(1, sizeof(Editor));
    if (!ed) return NULL;

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
        .clear_color = {{ 0.06f, 0.06f, 0.12f, 1.0f }},
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

    /* Initialize forward rendering pipeline */
    qs_forward_init(ed->engine, ed->scene_renderer);

    /* ---- Test scene ---- */
    Qs_Scene *scene = qs_scene_create(ed->engine, &(Qs_SceneDesc){
        .name = "TestScene",
    });
    qs_scene_set_active(scene);

    /* Create shared primitive meshes */
    Qs_Mesh *plane_mesh  = qs_primitive_plane(ed->engine, 12.0f, 1);
    Qs_Mesh *cube_mesh   = qs_primitive_cube(ed->engine, 1.0f);
    Qs_Mesh *sphere_mesh = qs_primitive_sphere(ed->engine, 0.5f, 24, 16);

    /* Create some materials with different colors */
    Qs_Material *mat_floor = qs_material_create(ed->engine, &(Qs_MaterialDesc){
        .name              = "floor",
        .base_color_factor = { 0.3f, 0.3f, 0.35f, 1.0f },
        .roughness_factor  = 0.9f,
        .metallic_factor   = 0.0f,
    });
    Qs_Material *mat_red = qs_material_create(ed->engine, &(Qs_MaterialDesc){
        .name              = "red",
        .base_color_factor = { 0.8f, 0.15f, 0.1f, 1.0f },
        .roughness_factor  = 0.5f,
        .metallic_factor   = 0.1f,
    });
    Qs_Material *mat_blue = qs_material_create(ed->engine, &(Qs_MaterialDesc){
        .name              = "blue",
        .base_color_factor = { 0.1f, 0.3f, 0.85f, 1.0f },
        .roughness_factor  = 0.4f,
        .metallic_factor   = 0.2f,
    });
    Qs_Material *mat_green = qs_material_create(ed->engine, &(Qs_MaterialDesc){
        .name              = "green",
        .base_color_factor = { 0.15f, 0.7f, 0.2f, 1.0f },
        .roughness_factor  = 0.6f,
        .metallic_factor   = 0.0f,
    });
    Qs_Material *mat_gold = qs_material_create(ed->engine, &(Qs_MaterialDesc){
        .name              = "gold",
        .base_color_factor = { 0.9f, 0.75f, 0.2f, 1.0f },
        .roughness_factor  = 0.3f,
        .metallic_factor   = 0.9f,
    });

    /* Floor platform */
    {
        Qs_Entity e = qs_entity_create(scene, "Floor");
        Qs_MeshComp *mc = (Qs_MeshComp *)qs_entity_add(scene, e, qs_mesh_comp_type());
        mc->mesh     = plane_mesh;
        mc->material = mat_floor;
    }

    /* Red cube — left */
    {
        Qs_Entity e = qs_entity_create(scene, "RedCube");
        Qs_Transform *tf = (Qs_Transform *)qs_entity_get(scene, e, qs_transform_type());
        tf->position[0] = -2.0f;
        tf->position[1] =  0.5f;
        tf->position[2] =  0.0f;
        Qs_MeshComp *mc = (Qs_MeshComp *)qs_entity_add(scene, e, qs_mesh_comp_type());
        mc->mesh     = cube_mesh;
        mc->material = mat_red;
    }

    /* Blue cube — right */
    {
        Qs_Entity e = qs_entity_create(scene, "BlueCube");
        Qs_Transform *tf = (Qs_Transform *)qs_entity_get(scene, e, qs_transform_type());
        tf->position[0] =  2.0f;
        tf->position[1] =  0.5f;
        tf->position[2] =  0.0f;
        Qs_MeshComp *mc = (Qs_MeshComp *)qs_entity_add(scene, e, qs_mesh_comp_type());
        mc->mesh     = cube_mesh;
        mc->material = mat_blue;
    }

    /* Green sphere — center back */
    {
        Qs_Entity e = qs_entity_create(scene, "GreenSphere");
        Qs_Transform *tf = (Qs_Transform *)qs_entity_get(scene, e, qs_transform_type());
        tf->position[0] =  0.0f;
        tf->position[1] =  0.5f;
        tf->position[2] = -2.0f;
        Qs_MeshComp *mc = (Qs_MeshComp *)qs_entity_add(scene, e, qs_mesh_comp_type());
        mc->mesh     = sphere_mesh;
        mc->material = mat_green;
    }

    /* Gold sphere — center front */
    {
        Qs_Entity e = qs_entity_create(scene, "GoldSphere");
        Qs_Transform *tf = (Qs_Transform *)qs_entity_get(scene, e, qs_transform_type());
        tf->position[0] =  0.0f;
        tf->position[1] =  0.8f;
        tf->position[2] =  2.0f;
        tf->scale[0]    =  1.5f;
        tf->scale[1]    =  1.5f;
        tf->scale[2]    =  1.5f;
        Qs_MeshComp *mc = (Qs_MeshComp *)qs_entity_add(scene, e, qs_mesh_comp_type());
        mc->mesh     = sphere_mesh;
        mc->material = mat_gold;
    }

    /* Small stacked cubes — right-back */
    {
        Qs_Entity e = qs_entity_create(scene, "SmallCube1");
        Qs_Transform *tf = (Qs_Transform *)qs_entity_get(scene, e, qs_transform_type());
        tf->position[0] =  3.0f;
        tf->position[1] =  0.3f;
        tf->position[2] = -2.5f;
        tf->scale[0]    =  0.6f;
        tf->scale[1]    =  0.6f;
        tf->scale[2]    =  0.6f;
        Qs_MeshComp *mc = (Qs_MeshComp *)qs_entity_add(scene, e, qs_mesh_comp_type());
        mc->mesh     = cube_mesh;
        mc->material = mat_gold;
    }
    {
        Qs_Entity e = qs_entity_create(scene, "SmallCube2");
        Qs_Transform *tf = (Qs_Transform *)qs_entity_get(scene, e, qs_transform_type());
        tf->position[0] =  3.0f;
        tf->position[1] =  0.9f;
        tf->position[2] = -2.5f;
        tf->scale[0]    =  0.4f;
        tf->scale[1]    =  0.4f;
        tf->scale[2]    =  0.4f;
        Qs_MeshComp *mc = (Qs_MeshComp *)qs_entity_add(scene, e, qs_mesh_comp_type());
        mc->mesh     = cube_mesh;
        mc->material = mat_red;
    }

    editor_build_ui(ed);

    ed_file_browser_init(qs_engine_ca_instance(ed->engine));

    qs_engine_set_event_handler(ed->engine, CA_EVENT_KEY, on_key_event, ed);
    qs_engine_set_on_frame(ed->engine, on_frame, ed);
    qs_log_set_listener(on_log, ed);

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

void editor_destroy(Editor *ed)
{
    if (!ed) return;
    qs_forward_shutdown();
    qs_engine_destroy(ed->engine);
    qs_project_destroy(ed->project);
    free(ed);
}
