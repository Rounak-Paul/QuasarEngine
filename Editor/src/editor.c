#include "editor.h"
#include "ui/ed_menu_bar.h"
#include "ui/ed_toolbar.h"
#include "ui/ed_layout.h"
#include "ui/ed_status_bar.h"
#include "ui/ed_inspector.h"

#include "ui/ed_file_browser.h"

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
};

/* ---- Editor CSS theme (Catppuccin Mocha) ---- */

static const char *g_editor_css =

    /* Root — Catppuccin Base */
    ".editor-root {"
    "  background: #1e1e2e;"
    "  gap: 0px;"
    "  overflow: hidden;"
    "}"

    /* Menu bar — Mantle */
    ".menu-bar {"
    "  background: #181825;"
    "  width: 100%;"
    "  height: 24px;"
    "  border-width: 1px;"
    "  border-color: #313244;"
    "}"

    /* Menu bar items — Text color */
    ".menu-bar-item {"
    "  padding-left: 10px;"
    "  padding-right: 10px;"
    "  align-items: center;"
    "  color: #cdd6f4;"
    "  font-size: 15px;"
    "}"

    /* Toolbar — Mantle */
    ".toolbar {"
    "  background: #181825;"
    "  width: 100%;"
    "  height: 28px;"
    "  align-items: center;"
    "  padding-left: 6px;"
    "  padding-right: 6px;"
    "  border-width: 1px;"
    "  border-color: #313244;"
    "}"

    /* Panels — Base */
    ".panel {"
    "  background: #1e1e2e;"
    "  overflow: hidden;"
    "}"

    ".panel-viewport {"
    "  background: #11111b;"
    "}"

    ".panel-bottom {"
    "  background: #1e1e2e;"
    "}"

    /* Console — scrollable log area */
    ".console-scroll {"
    "  overflow-y: scroll;"
    "  padding: 4px;"
    "  flex-grow: 1;"
    "  align-items: flex-start;"
    "}"

    ".console-line {"
    "  font-size: 15px;"
    "  height: 17px;"
    "  width: 100%;"
    "  text-align: left;"
    "}"

    /* Panel tab bars — Mantle */
    ".panel-tab-bar {"
    "  background: #181825;"
    "  height: 26px;"
    "  width: 100%;"
    "  align-items: center;"
    "  padding-left: 6px;"
    "  gap: 2px;"
    "  border-width: 1px;"
    "  border-color: #313244;"
    "}"

    /* Panel tabs — Overlay0 dim, active = Mauve */
    ".panel-tab {"
    "  color: #6c7086;"
    "  font-size: 15px;"
    "  padding-left: 10px;"
    "  padding-right: 10px;"
    "}"

    ".active {"
    "  color: #cba6f7;"
    "}"

    /* Status bar — Crust (deepest chrome) */
    ".status-bar {"
    "  background: #11111b;"
    "  width: 100%;"
    "  height: 22px;"
    "  align-items: center;"
    "  padding-left: 10px;"
    "}"

    ".status-text {"
    "  color: #6c7086;"
    "  font-size: 14px;"
    "}"

    /* ---- File browser (Catppuccin Mocha) ---- */

    ".fb-root {"
    "  width: 100%;"
    "  height: 100%;"
    "}"

    /* Title bar — Mantle with Mauve accent title */
    ".fb-title-bar {"
    "  background: #181825;"
    "  height: 38px;"
    "  width: 100%;"
    "  padding-left: 14px;"
    "  padding-right: 6px;"
    "  align-items: center;"
    "  border-width: 1px;"
    "  border-color: #313244;"
    "}"

    ".fb-title {"
    "  color: #cba6f7;"
    "  font-size: 16px;"
    "  flex-grow: 1;"
    "}"

    ".fb-spacer-grow {"
    "  flex-grow: 1;"
    "}"

    ".fb-close-btn {"
    "  width: 28px;"
    "  height: 28px;"
    "  background: transparent;"
    "  color: #6c7086;"
    "  font-size: 16px;"
    "  corner-radius: 4px;"
    "}"

    /* Nav bar — Base */
    ".fb-nav-bar {"
    "  background: #1e1e2e;"
    "  height: 42px;"
    "  width: 100%;"
    "  padding-left: 8px;"
    "  padding-right: 8px;"
    "  align-items: center;"
    "  gap: 4px;"
    "  border-width: 1px;"
    "  border-color: #313244;"
    "}"

    /* Nav buttons — Surface0 with Text color */
    ".fb-nav-btn {"
    "  width: 30px;"
    "  height: 26px;"
    "  background: #313244;"
    "  color: #cdd6f4;"
    "  font-size: 16px;"
    "  corner-radius: 4px;"
    "}"

    /* Path input — Crust with Text */
    ".fb-path-input {"
    "  flex-grow: 1;"
    "  height: 28px;"
    "  background: #11111b;"
    "  color: #cdd6f4;"
    "  font-size: 15px;"
    "  padding-left: 8px;"
    "  corner-radius: 4px;"
    "  border-width: 1px;"
    "  border-color: #313244;"
    "}"

    /* Column header — Mantle */
    ".fb-col-header {"
    "  width: 100%;"
    "  height: 24px;"
    "  padding-left: 14px;"
    "  padding-right: 14px;"
    "  align-items: center;"
    "  background: #181825;"
    "  border-width: 1px;"
    "  border-color: #313244;"
    "}"

    ".fb-col-name {"
    "  color: #6c7086;"
    "  font-size: 14px;"
    "  flex-grow: 1;"
    "  text-align: left;"
    "}"

    ".fb-col-size {"
    "  color: #6c7086;"
    "  font-size: 14px;"
    "  width: 80px;"
    "  text-align: right;"
    "}"

    /* File list — scrollable content area */
    ".fb-file-list {"
    "  flex-grow: 1;"
    "  overflow-y: scroll;"
    "  padding: 4px;"
    "  gap: 2px;"
    "  align-items: flex-start;"
    "  background: #1e1e2e;"
    "}"

    /* File entries — Text default, dir = Blue */
    ".fb-entry {"
    "  width: 100%;"
    "  height: 28px;"
    "  background: transparent;"
    "  color: #cdd6f4;"
    "  font-size: 15px;"
    "  text-align: left;"
    "  padding-left: 8px;"
    "  corner-radius: 4px;"
    "}"

    ".fb-entry-selected {"
    "  background: #313244;"
    "}"

    ".fb-entry-dir {"
    "  color: #89b4fa;"
    "}"

    ".fb-empty {"
    "  color: #6c7086;"
    "  font-size: 15px;"
    "  padding: 14px;"
    "}"

    /* Bottom bar — Mantle (fixed-height, doesn't grow) */
    ".fb-bottom {"
    "  background: #181825;"
    "  width: 100%;"
    "  padding: 8px 10px;"
    "  gap: 6px;"
    "  border-width: 1px;"
    "  border-color: #313244;"
    "}"

    ".fb-bottom-row {"
    "  width: 100%;"
    "  height: 28px;"
    "  align-items: center;"
    "  gap: 8px;"
    "}"

    ".fb-label {"
    "  color: #a6adc8;"
    "  font-size: 15px;"
    "  width: 50px;"
    "  text-align: right;"
    "}"

    ".fb-selected-name {"
    "  color: #cdd6f4;"
    "  font-size: 15px;"
    "  flex-grow: 1;"
    "  text-align: left;"
    "}"

    ".fb-filter-select {"
    "  width: 200px;"
    "}"

    /* Cancel button — Surface0 */
    ".fb-btn {"
    "  width: 90px;"
    "  height: 28px;"
    "  background: #313244;"
    "  color: #cdd6f4;"
    "  font-size: 15px;"
    "  corner-radius: 4px;"
    "}"

    /* Open/Confirm button — Mauve (primary accent) */
    ".fb-btn-primary {"
    "  background: #cba6f7;"
    "  color: #1e1e2e;"
    "}"

    /* ---- Hierarchy panel (Catppuccin) ---- */

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
    "  color: #cba6f7;"
    "  font-size: 15px;"
    "}"

    ".hierarchy-entity {"
    "  color: #89b4fa;"
    "  font-size: 15px;"
    "}"

    ".hierarchy-component {"
    "  color: #6c7086;"
    "  font-size: 14px;"
    "}"

    ".hierarchy-add-btn {"
    "  width: 100%;"
    "  height: 26px;"
    "  background: transparent;"
    "  color: #6c7086;"
    "  font-size: 15px;"
    "}"

    /* ---- Inspector panel (Catppuccin) ---- */

    ".inspector-scroll {"
    "  overflow-y: scroll;"
    "  padding: 4px 0px;"
    "  gap: 1px;"
    "  flex-grow: 1;"
    "  width: 100%;"
    "  align-items: flex-start;"
    "}"

    ".inspector-placeholder {"
    "  color: #6c7086;"
    "  font-size: 15px;"
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
    "  height: 24px;"
    "  background: #11111b;"
    "  color: #cdd6f4;"
    "  font-size: 16px;"
    "  padding-left: 6px;"
    "  corner-radius: 4px;"
    "  border-width: 1px;"
    "  border-color: #313244;"
    "}"

    ".inspector-meta-row {"
    "  width: 100%;"
    "  height: 20px;"
    "  gap: 4px;"
    "  align-items: center;"
    "  padding-left: 0px;"
    "}"

    ".inspector-tag-input {"
    "  height: 20px;"
    "  flex-grow: 1;"
    "  background: #11111b;"
    "  color: #a6adc8;"
    "  font-size: 14px;"
    "  padding-left: 4px;"
    "  corner-radius: 4px;"
    "  border-width: 1px;"
    "  border-color: #313244;"
    "}"

    ".inspector-section-header {"
    "  background: #181825;"
    "  width: 100%;"
    "  height: 22px;"
    "  padding: 0px 8px;"
    "  margin-top: 4px;"
    "  align-items: center;"
    "  border-width: 1px;"
    "  border-color: #313244;"
    "}"

    ".inspector-section-label {"
    "  font-size: 14px;"
    "  color: #a6adc8;"
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
    "  color: #6c7086;"
    "  font-size: 14px;"
    "  text-align: left;"
    "  width: 100%;"
    "}"

    ".inspector-vec-row {"
    "  width: 100%;"
    "  gap: 0px;"
    "  align-items: center;"
    "  flex-wrap: wrap;"
    "}"

    /* Axis label colors — Red/Green/Blue/Mauve */
    ".inspector-axis-x { color: #f38ba8; font-size: 14px; width: 10px; }"
    ".inspector-axis-y { color: #a6e3a1; font-size: 14px; width: 10px; }"
    ".inspector-axis-z { color: #89b4fa; font-size: 14px; width: 10px; }"
    ".inspector-axis-w { color: #cba6f7; font-size: 14px; width: 10px; }"

    ".inspector-axis-group {"
    "  width: 68px;"
    "  padding-left: 4px;"
    "  gap: 2px;"
    "  align-items: center;"
    "}"

    ".inspector-vec-input {"
    "  width: 50px;"
    "  height: 18px;"
    "  background: #11111b;"
    "  color: #cdd6f4;"
    "  font-size: 14px;"
    "  padding-left: 4px;"
    "  corner-radius: 3px;"
    "  border-width: 1px;"
    "  border-color: #313244;"
    "}"

    ".inspector-scalar-input {"
    "  width: 100%;"
    "  height: 18px;"
    "  background: #11111b;"
    "  color: #cdd6f4;"
    "  font-size: 14px;"
    "  padding-left: 4px;"
    "  corner-radius: 3px;"
    "  border-width: 1px;"
    "  border-color: #313244;"
    "}"

    ".inspector-field-value {"
    "  color: #a6adc8;"
    "  font-size: 14px;"
    "}"

    ".inspector-id-label {"
    "  color: #6c7086;"
    "  font-size: 14px;"
    "  width: 100%;"
    "  text-align: left;"
    "  padding-left: 2px;"
    "}"

    ".inspector-tag-icon {"
    "  color: #a6e3a1;"
    "  font-size: 15px;"
    "  width: 16px;"
    "  flex-shrink: 0;"
    "}"

    ".hierarchy-selected {"
    "  background: #313244;"
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
        .clear_color = {{ 0.0f, 0.0f, 0.0f, 1.0f }},
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

    /* ---- Load scene from project ---- */
    if (ed->project) {
        char scene_path[512];
        snprintf(scene_path, sizeof(scene_path), "%s/scenes/default.qscene",
                 qs_project_path(ed->project));
        editor_load_scene(ed, scene_path);
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
    qs_forward_shutdown();
    qs_engine_destroy(ed->engine);
    qs_project_destroy(ed->project);
    free(ed);
}
