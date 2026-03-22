#include "editor.h"
#include "ui/ed_menu_bar.h"
#include "ui/ed_toolbar.h"
#include "ui/ed_layout.h"
#include "ui/ed_status_bar.h"

#include <stdlib.h>

struct Editor {
    Qs_Engine     *engine;
    Ca_Instance   *instance;
    Ca_Window     *window;
    Ca_Stylesheet *stylesheet;
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
;

static void editor_build_ui(Editor *ed)
{
    ca_ui_begin(ed->window, &(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "editor-root",
    });

    ed_menu_bar(ed->window, ed);
    ed_toolbar(ed->window, ed);
    ed_layout(ed->window, ed);
    ed_status_bar(ed->window, ed);

    ca_ui_end();
}

static void on_key_event(const Ca_Event *event, void *userdata)
{
    (void)userdata;
    qs_input_key_event((Qs_Key)event->key.key,
                       (Qs_KeyAction)event->key.action,
                       event->key.mods);
}

static void on_frame(void *userdata)
{
    Editor *ed = userdata;
    if (ed->scene_viewport)
        ca_viewport_request_redraw(ed->scene_viewport);
    ed_console_update(ed);
}

static void on_log(void *userdata)
{
    (void)userdata;
    ca_instance_wake();
}

Editor *editor_create(const EditorDesc *desc)
{
    Editor *ed = calloc(1, sizeof(Editor));
    if (!ed) return NULL;

    ed->engine = qs_engine_create(&(Qs_EngineDesc){
        .app_name      = desc->title ? desc->title : "Quasar Editor",
        .version_major = 0,
        .version_minor = 1,
        .version_patch = 0,
    });
    if (!ed->engine) {
        free(ed);
        return NULL;
    }

    ed->instance = ca_instance_create(&(Ca_InstanceDesc){
        .app_name     = desc->title ? desc->title : "Quasar Editor",
        .font_size_px = 14.0f,
    });
    if (!ed->instance) {
        qs_engine_destroy(ed->engine);
        free(ed);
        return NULL;
    }

    ed->stylesheet = ca_css_parse(g_editor_css);
    if (ed->stylesheet)
        ca_instance_set_stylesheet(ed->instance, ed->stylesheet);

    ed->window = ca_window_create(ed->instance, &(Ca_WindowDesc){
        .title  = desc->title ? desc->title : "Quasar Editor",
        .width  = desc->width  > 0 ? desc->width  : 1280,
        .height = desc->height > 0 ? desc->height : 720,
    });
    if (!ed->window) {
        if (ed->stylesheet) ca_css_destroy(ed->stylesheet);
        ca_instance_destroy(ed->instance);
        qs_engine_destroy(ed->engine);
        free(ed);
        return NULL;
    }

    Qs_SystemDesc render_desc = qs_render_system_desc(ed->instance);
    if (!qs_system_register(qs_engine_systems(ed->engine), &render_desc)) {
        ca_window_destroy(ed->window);
        if (ed->stylesheet) ca_css_destroy(ed->stylesheet);
        ca_instance_destroy(ed->instance);
        qs_engine_destroy(ed->engine);
        free(ed);
        return NULL;
    }

    Qs_SystemDesc tex_desc = qs_texture_system_desc(ed->instance);
    qs_system_register(qs_engine_systems(ed->engine), &tex_desc);

    Qs_SystemDesc mesh_desc = qs_mesh_system_desc(ed->instance);
    qs_system_register(qs_engine_systems(ed->engine), &mesh_desc);

    Qs_SystemDesc mat_desc = qs_material_system_desc(ed->instance);
    qs_system_register(qs_engine_systems(ed->engine), &mat_desc);

    Qs_SystemDesc light_desc = qs_light_system_desc();
    qs_system_register(qs_engine_systems(ed->engine), &light_desc);

    Qs_SystemDesc scene_desc = qs_scene_system_desc();
    qs_system_register(qs_engine_systems(ed->engine), &scene_desc);

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

    /* Feed keyboard events from Causality into the engine input system */
    ca_event_set_handler(ed->instance, CA_EVENT_KEY, on_key_event, ed);

    /* Per-frame callback updates the console */
    ca_window_set_on_frame(ed->window, on_frame, ed);

    /* Wake the event loop when new log entries arrive from background threads */
    qs_log_set_listener(on_log, ed);

    return ed;
}

int editor_run(Editor *ed)
{
    if (!ed) return 1;
    while (ca_instance_tick(ed->instance)) { }
    return 0;
}

void editor_request_exit(Editor *ed)
{
    if (ed && ed->window)
        ca_window_close(ed->window);
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
    if (ed->stylesheet) ca_css_destroy(ed->stylesheet);
    ca_instance_destroy(ed->instance);
    free(ed);
}
