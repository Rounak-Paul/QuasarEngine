#include "editor.h"
#include "ed_camera.h"
#include "ed_gizmo.h"
#include "ed_pick.h"
#include "ed_commands.h"
#include "ed_thumbnail.h"
#include "qs_math.h"
#include "ui/ed_menu_bar.h"
#include "ui/ed_toolbar.h"
#include "ui/ed_layout.h"
#include "ed_style.h"
#include "ui/ed_inspector.h"
#include "ui/ed_hierarchy.h"
#include "ui/ed_plugin_manager.h"

#include "ui/ed_file_browser.h"
#include "ui/ed_import_dialog.h"

#include "quasar.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EDITOR_PROTO_STACK_DEPTH 8

/* ---- Forward decls used early in editor_create() ---- */
static void kb_save_scene(void *u);
static void kb_save_proj (void *u);
static void kb_undo      (void *u);
static void kb_redo      (void *u);

struct Editor {
    Qs_Engine     *engine;
    Qs_Project    *project;
    Qs_Renderer   *scene_renderer;
    Ca_Viewport   *scene_viewport;
    Qs_Entity      selected_entity;
    EditorCamera   cam;

    /* ---- Prototype editor mode ---- */
    EditorMode    mode;
    char          proto_path[1024];   ///< Path to the .qproto being edited (top of stack).
    /* When entering prototype mode, the current active scene + camera are
       pushed.  Closing the prototype pops and restores. */
    Qs_Scene     *proto_stack_scene[EDITOR_PROTO_STACK_DEPTH];
    EditorCamera  proto_stack_cam[EDITOR_PROTO_STACK_DEPTH];
    uint32_t      proto_stack_depth;

    /* Editor-only preview light injected into the prototype scene so the
       inner geometry is visible while editing.  Destroyed before save so
       it is never baked into the .qproto file. */
    Qs_Entity     proto_preview_light;

    /* ---- Prototype-instance override editing context ----
       When the user expands a prototype instance in the hierarchy and
       selects one of its inner entities, these fields point to that
       inner-scene context so the inspector can route field edits
       through the PrototypeComp override system rather than mutating
       the source .qproto file. */
    Qs_Entity     proto_owner;          ///< QS_ENTITY_INVALID when inactive.
    Qs_Scene     *proto_inner_scene;

    /* ---- Persistence ---- */
    char          current_scene_path[1024]; ///< Path of the loaded outer scene.
};

/* CSS is defined in ed_style.c via g_editor_css (see ed_style.h). */

/* ---- Breadcrumb bar for prototype editor mode ---- */
static Ca_Div   *s_breadcrumb_bar;
static Ca_Label *s_breadcrumb_scene_label;
static Ca_Label *s_breadcrumb_proto_label;

static void on_breadcrumb_back(Ca_Button *btn, void *data)
{
    (void)btn;
    Editor *ed = (Editor *)data;
    editor_close_prototype(ed);
}

static void editor_build_ui(Editor *ed)
{
    Ca_Window *window = qs_engine_window(ed->engine);

    ca_ui_begin(window, &(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "editor-root",
    });

    ed_toolbar(window, ed);

    /* Breadcrumb bar — visible only in prototype editor mode */
    s_breadcrumb_bar = ca_div_begin(&(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .style     = "breadcrumb-bar",
        .id        = "breadcrumb-bar",
        .hidden    = true,
    });
    {
        ca_btn_begin(&(Ca_BtnDesc){
            .text       = "\xEF\x81\x88 Back",  /* U+F048 step-backward */
            .id         = "breadcrumb-back",
            .style      = "breadcrumb-back",
            .on_click   = on_breadcrumb_back,
            .click_data = ed,
        });
        ca_btn_end();
        s_breadcrumb_scene_label = ca_text(&(Ca_TextDesc){
            .text  = "",
            .id    = "breadcrumb-scene",
            .style = "breadcrumb-text",
        });
        ca_text(&(Ca_TextDesc){
            .text  = ">",
            .id    = "breadcrumb-sep",
            .style = "breadcrumb-separator",
        });
        s_breadcrumb_proto_label = ca_text(&(Ca_TextDesc){
            .text  = "",
            .id    = "breadcrumb-proto",
            .style = "breadcrumb-active",
        });
    }
    ca_div_end();

    ed_layout(window, ed);

    ca_ui_end();

    /* Status bar is a system-managed Causality control, not part of the
       editor content layout tree. */
    ca_window_set_status_bar(window, ed_status_bar, ed, ED_H_TAB_BAR_F);
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

/* Fires synchronously after the plugin has been successfully reloaded
   or enabled.  Recreate the scene renderer and rebuild the toolbar. */
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
        if (ed->scene_renderer && ed->scene_viewport) {
            qs_renderer_bind(ed->scene_renderer, (Qs_Viewport *)ed->scene_viewport);
        }
        if (ed->scene_renderer) {
            ed_pick_attach(ed->scene_renderer);
            ed_gizmo_attach(ed->scene_renderer);
        }
    }
    ed_toolbar_rebuild();
    ed_menu_bar_invalidate();
    return false;
}

/* Fires after a plugin is fully disabled and unloaded.
   Only rebuild the toolbar and menu — do NOT recreate the renderer since
   the backend that provides it may be the one being disabled. */
static bool on_plugin_disable_end(const Qs_Event *e, void *userdata)
{
    (void)e; (void)userdata;
    ed_toolbar_rebuild();
    ed_menu_bar_invalidate();
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
    /* Editor shortcuts win over input system polling so e.g. Ctrl+S
       fires reliably while a viewport-bound key like W is also held. */
    if (ed_keybinds_dispatch(event->key.key, event->key.action, event->key.mods))
        return;
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

    /* Determine whether the mouse is currently inside the scene viewport so
       the camera ignores scroll events from other panels. */
    bool viewport_hovered = false;
    if (ed->scene_viewport) {
        float vx, vy, vw, vh;
        ca_viewport_screen_rect(ed->scene_viewport, &vx, &vy, &vw, &vh);
        float mx, my;
        qs_input_mouse_pos(&mx, &my);
        viewport_hovered = (mx >= vx && mx <= vx + vw && my >= vy && my <= vy + vh);
    }

    ed_camera_update(&ed->cam, ed->scene_renderer, qs_engine_dt(ed->engine),
                     viewport_hovered);
    ed_gizmo_update(ed, qs_engine_dt(ed->engine));

    /* Submit scene renderables and lights for this frame */
    Qs_Scene *scene = qs_scene_active();
    if (scene && ed->scene_renderer) {
        qs_renderer_clear_renderables(ed->scene_renderer);
        qs_renderer_clear_lights(ed->scene_renderer);

        /* Recursive submission walks the scene + every PrototypeComp's
           inner scene, composing world transforms.  Lights are submitted
           only at the top level. */
        qs_scene_submit_renderables(scene, ed->engine, ed->scene_renderer, NULL);
    }

    ed_menu_bar_sync();
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

/* Resolve the base directory of the scene file for relative asset paths. */
static void scene_dir(const char *scene_path, char *out, size_t out_size)
{
    const char *last = NULL;
    for (const char *p = scene_path; *p; p++)
        if (*p == '/' || *p == '\\') last = p;
    if (!last) {
        snprintf(out, out_size, ".");
    } else {
        size_t len = (size_t)(last - scene_path);
        if (len >= out_size) len = out_size - 1;
        memcpy(out, scene_path, len);
        out[len] = '\0';
    }
}

static bool editor_load_scene(Editor *ed, const char *path)
{
    /* Scene name will be populated from the file by qs_scene_load. */
    Qs_Scene *scene = qs_scene_create(ed->engine, &(Qs_SceneDesc){
        .name = "Scene",
    });
    if (!scene) return false;
    qs_scene_set_active(scene);

    if (!qs_scene_load(scene, ed->engine, path)) {
        QS_LOG_ERROR("Failed to load scene: %s", path);
        return false;
    }

    /* Track the source path so Save Scene knows where to write. */
    snprintf(ed->current_scene_path, sizeof(ed->current_scene_path), "%s", path);

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

    QS_LOG_INFO("Scene loaded: %s", path);
    return true;
}

Editor *editor_create(const EditorDesc *desc)
{
    Editor *ed = qs_calloc(1, sizeof(Editor), QS_MEM_EDITOR);
    if (!ed) return NULL;
    ed->selected_entity     = QS_ENTITY_INVALID;
    ed->proto_owner         = QS_ENTITY_INVALID;
    ed->proto_inner_scene   = NULL;
    ed->proto_preview_light = QS_ENTITY_INVALID;

    /* Open project */
    if (desc->project_path) {
        ed->project = qs_project_open(desc->project_path);
        if (!ed->project) {
            qs_free(ed);
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
        .font_size_px  = ED_FONT_SIZE_PX,
    });
    if (!ed->engine) {
        qs_free(ed);
        return NULL;
    }

    ca_window_set_scale(qs_engine_window(ed->engine), ED_UI_SCALE);
    qs_engine_set_stylesheet(ed->engine, g_editor_css);
    if (ed->project)
        qs_engine_set_project(ed->engine, ed->project);

    ed_gizmo_init(ed->engine);
    ed_pick_init(ed->engine);

    ed->scene_renderer = qs_renderer_create(ed->engine, &(Qs_RendererDesc){
        .name        = "scene",
        .clear_color = { 0.0f, 0.0f, 0.0f, 1.0f },
        .depth_test  = true,
    });

    if (ed->scene_renderer) {
        ed_pick_attach(ed->scene_renderer);
        ed_gizmo_attach(ed->scene_renderer);
    }

    /* Position camera for a good view of the test scene */
    if (ed->scene_renderer) {
        Qs_Camera *cam = qs_renderer_camera(ed->scene_renderer);
        if (cam) {
            cam->position[0] =  5.0f;
            cam->position[1] =  4.0f;
            cam->position[2] =  8.0f;
            cam->target[0]   =  0.0f;
            cam->target[1]   =  0.5f;
            cam->target[2]   =  0.0f;
        }
    }

    /* ---- Load scene from project ---- */
    if (ed->project) {
        const char *rel = qs_project_startup_scene(ed->project);
        if (rel) {
            char scene_path[1024];
            qs_project_resolve(ed->project, rel, scene_path, sizeof(scene_path));
            editor_load_scene(ed, scene_path);
        } else {
            QS_LOG_WARN("No startup scene set in project file.");
        }
    }

    ed_plugin_manager_init(ed);
    ed_toolbar_init(ed);

    editor_build_ui(ed);

    ca_window_set_title(qs_engine_window(ed->engine), "Quasar Editor");
    ed_menu_bar_init(qs_engine_window(ed->engine), ed);

    ed_file_browser_init(ca_window_instance(qs_engine_window(ed->engine)));
    ed_import_dialog_init(ed);

    qs_engine_set_event_handler(ed->engine, CA_EVENT_KEY,          on_key_event,    ed);
    qs_engine_set_event_handler(ed->engine, CA_EVENT_MOUSE_BUTTON, on_mouse_button, ed);
    qs_engine_set_event_handler(ed->engine, CA_EVENT_MOUSE_MOVE,   on_mouse_move,   ed);
    qs_engine_set_event_handler(ed->engine, CA_EVENT_MOUSE_SCROLL, on_mouse_scroll, ed);
    qs_engine_set_on_frame(ed->engine, on_frame, ed);
    qs_log_set_listener(on_log, ed);

    /* ---- Editor command stack + global keybinds ---- */
    ed_undo_init();
    ed_keybinds_init();
#ifdef __APPLE__
    /* On macOS, both Cmd (SUPER) and Ctrl are common save/undo modifiers. */
    int primary_mod = QS_MOD_SUPER;
#else
    int primary_mod = QS_MOD_CONTROL;
#endif
    ed_keybinds_register(QS_KEY_S, primary_mod,                      kb_save_scene, ed, "Ctrl+S");
    ed_keybinds_register(QS_KEY_S, primary_mod | QS_MOD_SHIFT,       kb_save_proj,  ed, "Ctrl+Shift+S");
    ed_keybinds_register(QS_KEY_Z, primary_mod,                      kb_undo,       ed, "Ctrl+Z");
    ed_keybinds_register(QS_KEY_Y, primary_mod,                      kb_redo,       ed, "Ctrl+Y");
    ed_keybinds_register(QS_KEY_Z, primary_mod | QS_MOD_SHIFT,       kb_redo,       ed, "Ctrl+Shift+Z");
    /* Cross-platform fallbacks: also accept literal Ctrl on macOS. */
#ifdef __APPLE__
    ed_keybinds_register(QS_KEY_S, QS_MOD_CONTROL,                   kb_save_scene, ed, "Ctrl+S");
    ed_keybinds_register(QS_KEY_S, QS_MOD_CONTROL | QS_MOD_SHIFT,    kb_save_proj,  ed, "Ctrl+Shift+S");
    ed_keybinds_register(QS_KEY_Z, QS_MOD_CONTROL,                   kb_undo,       ed, "Ctrl+Z");
    ed_keybinds_register(QS_KEY_Y, QS_MOD_CONTROL,                   kb_redo,       ed, "Ctrl+Y");
    ed_keybinds_register(QS_KEY_Z, QS_MOD_CONTROL | QS_MOD_SHIFT,    kb_redo,       ed, "Ctrl+Shift+Z");
#endif

    /* Subscribe to plugin reload events to refresh the scene renderer */
    Qs_EventBus *bus = qs_engine_event_bus(ed->engine);
    qs_event_subscribe(bus, QS_EVENT_PLUGIN_RELOAD_BEGIN,  on_plugin_reload_begin, ed);
    qs_event_subscribe(bus, QS_EVENT_PLUGIN_RELOAD_END,    on_plugin_reload_end,   ed);
    qs_event_subscribe(bus, QS_EVENT_PLUGIN_DISABLE_BEGIN, on_plugin_reload_begin, ed);
    qs_event_subscribe(bus, QS_EVENT_PLUGIN_DISABLE_END,   on_plugin_disable_end,  ed);
    qs_event_subscribe(bus, QS_EVENT_PLUGIN_ENABLE_END,    on_plugin_reload_end,   ed);

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

Qs_Project *editor_project(const Editor *ed)
{
    return ed ? ed->project : NULL;
}

const char *editor_current_scene_path(const Editor *ed)
{
    return ed ? ed->current_scene_path : "";
}

bool editor_save_scene(Editor *ed)
{
    if (!ed) return false;
    Qs_Scene *scene = qs_scene_active();
    if (!scene) {
        QS_LOG_WARN("Save Scene: no active scene");
        return false;
    }
    /* In prototype mode, the active scene is the loaded .qproto. */
    const char *path = (ed->mode == ED_MODE_PROTOTYPE && ed->proto_path[0])
                           ? ed->proto_path
                           : ed->current_scene_path;
    if (!path || !*path) {
        QS_LOG_WARN("Save Scene: no source path known for active scene");
        return false;
    }
    if (!qs_scene_save(scene, path)) {
        QS_LOG_ERROR("Save Scene failed: %s", path);
        return false;
    }
    QS_LOG_INFO("Saved scene: %s", path);
    return true;
}

bool editor_save_project(Editor *ed)
{
    if (!ed) return false;
    bool ok = true;
    if (ed->project) {
        if (!qs_project_save(ed->project)) {
            QS_LOG_ERROR("Save Project failed");
            ok = false;
        } else {
            QS_LOG_INFO("Saved project: %s", qs_project_path(ed->project));
        }
    }
    /* Also flush the active scene so Save Project is a one-step save-all. */
    if (!editor_save_scene(ed)) ok = false;
    return ok;
}

bool editor_undo(Editor *ed)
{
    (void)ed;
    return ed_undo();
}

bool editor_redo(Editor *ed)
{
    (void)ed;
    return ed_redo();
}

void editor_focus_all(Editor *ed)
{
    if (!ed) return;
    Qs_Scene *scene = qs_scene_active();
    if (!scene) return;

    float min_x = 1e30f, min_y = 1e30f, min_z = 1e30f;
    float max_x = -1e30f, max_y = -1e30f, max_z = -1e30f;
    int count = 0;

    for (Qs_Entity e = qs_scene_first(scene, qs_transform_type());
         e != QS_ENTITY_INVALID;
         e = qs_scene_next(scene, qs_transform_type(), e))
    {
        float m[16];
        qs_scene_world_matrix(scene, e, m);
        /* column-major: translation is at [12],[13],[14] */
        float x = m[12], y = m[13], z = m[14];
        if (x < min_x) min_x = x;  if (x > max_x) max_x = x;
        if (y < min_y) min_y = y;  if (y > max_y) max_y = y;
        if (z < min_z) min_z = z;  if (z > max_z) max_z = z;
        count++;
    }

    if (count == 0) return;

    float center[3] = {
        (min_x + max_x) * 0.5f,
        (min_y + max_y) * 0.5f,
        (min_z + max_z) * 0.5f,
    };
    float dx = (max_x - min_x) * 0.5f;
    float dy = (max_y - min_y) * 0.5f;
    float dz = (max_z - min_z) * 0.5f;
    float radius = qs_v3_len((float[]){dx, dy, dz});
    if (radius < 1.0f) radius = 1.0f;

    ed_camera_focus(&ed->cam, center, radius);
}

/* ---- Keybind action thunks ---- */

static void kb_save_scene(void   *u) { editor_save_scene  ((Editor *)u); }
static void kb_save_proj (void   *u) { editor_save_project((Editor *)u); }
static void kb_undo      (void   *u) { editor_undo        ((Editor *)u); }
static void kb_redo      (void   *u) { editor_redo        ((Editor *)u); }


Ca_Viewport *editor_scene_viewport(const Editor *ed)
{
    return ed ? ed->scene_viewport : NULL;
}

Qs_Entity editor_selected_entity(const Editor *ed)
{
    return ed ? ed->selected_entity : QS_ENTITY_INVALID;
}

void editor_set_selected_entity(Editor *ed, Qs_Entity entity)
{
    if (!ed) return;
    ed->selected_entity   = entity;
    ed->proto_owner       = QS_ENTITY_INVALID;
    ed->proto_inner_scene = NULL;
}

void editor_set_proto_selection(Editor *ed,
                                Qs_Entity owner,
                                Qs_Scene *inner_scene,
                                Qs_Entity inner_entity)
{
    if (!ed) return;
    if (owner == QS_ENTITY_INVALID || !inner_scene) {
        editor_set_selected_entity(ed, inner_entity);
        return;
    }
    ed->selected_entity   = inner_entity;
    ed->proto_owner       = owner;
    ed->proto_inner_scene = inner_scene;
}

Qs_Entity editor_proto_owner(const Editor *ed)
{
    return ed ? ed->proto_owner : QS_ENTITY_INVALID;
}

Qs_Scene *editor_proto_inner_scene(const Editor *ed)
{
    return ed ? ed->proto_inner_scene : NULL;
}

EditorMode editor_mode(const Editor *editor)
{
    return editor ? editor->mode : ED_MODE_SCENE;
}

const char *editor_current_proto_path(const Editor *editor)
{
    if (!editor || editor->mode != ED_MODE_PROTOTYPE) return "";
    /* The stored proto_path is absolute (qs_scene_save needs it that way).
       Cycle detection compares against project-relative paths from the
       prototype dropdown / from PrototypeComp paths embedded in .qproto
       JSON, so we convert here.  Static buffer is fine — only one
       prototype is "current" at a time and this accessor is only read
       on the UI thread. */
    static char rel[1024];
    qs_project_make_relative(editor->project, editor->proto_path,
                             rel, sizeof(rel));
    return rel[0] ? rel : editor->proto_path;
}

bool editor_open_prototype(Editor *editor, const char *proto_path)
{
    if (!editor || !proto_path) return false;
    if (editor->proto_stack_depth >= EDITOR_PROTO_STACK_DEPTH) {
        QS_LOG_ERROR("Prototype edit stack full (max depth %d)",
                     EDITOR_PROTO_STACK_DEPTH);
        return false;
    }

    /* Reject re-entry into a prototype that's already being edited.
       Without this guard, opening A → A would push the same .qproto
       repeatedly, exhaust the edit stack, and (worse) recursively load
       the inner Qs_Scene for each entry. */
    if (editor->mode == ED_MODE_PROTOTYPE &&
        editor->proto_path[0] &&
        strcmp(editor->proto_path, proto_path) == 0)
    {
        QS_LOG_WARN("Prototype '%s' is already open for edit at this level", proto_path);
        return false;
    }
    /* Also reject if the candidate appears anywhere up the open chain
       (would-be cycle: editing A, then drilling A→B→A). */
    {
        const char *root = editor->project ? qs_project_path(editor->project) : NULL;
        for (uint32_t i = 0; i < editor->proto_stack_depth; i++) {
            (void)i;
        }
        if (qs_prototype_would_create_cycle(editor->project, editor->proto_path[0] ? editor->proto_path : "",
                                            proto_path) && editor->proto_path[0])
        {
            QS_LOG_ERROR("Refusing to open '%s': would create cyclic prototype edit (root '%s')",
                         proto_path, editor->proto_path);
            (void)root;
            return false;
        }
    }

    /* Push current scene + camera onto the stack */
    editor->proto_stack_scene[editor->proto_stack_depth] = qs_scene_active();
    editor->proto_stack_cam  [editor->proto_stack_depth] = editor->cam;
    editor->proto_stack_depth++;

    /* Load the .qproto file into a fresh scene and activate it.
       qs_scene_load handles parent restoration + asset cache resolution. */
    Qs_Scene *proto_scene = qs_scene_create(editor->engine, &(Qs_SceneDesc){
        .name = "Prototype",
    });
    if (!proto_scene ||
        !qs_scene_load(proto_scene, editor->engine, proto_path))
    {
        if (proto_scene) qs_scene_destroy(proto_scene);
        editor->proto_stack_depth--;
        QS_LOG_ERROR("Failed to load prototype: %s", proto_path);
        return false;
    }
    qs_scene_set_active(proto_scene);

    snprintf(editor->proto_path, sizeof(editor->proto_path), "%s", proto_path);
    editor->selected_entity   = QS_ENTITY_INVALID;
    editor->proto_owner       = QS_ENTITY_INVALID;
    editor->proto_inner_scene = NULL;

    /* Inject an editor-only preview directional light if the prototype
       has no lights of its own.  This is purely a viewport convenience —
       it is destroyed before the prototype is saved so the .qproto file
       never gains a stray light entity. */
    editor->proto_preview_light = QS_ENTITY_INVALID;
    if (qs_scene_first(proto_scene, qs_light_comp_type()) == QS_ENTITY_INVALID) {
        Qs_Entity sun = qs_entity_create(proto_scene, "(Editor Preview Light)");
        if (sun != QS_ENTITY_INVALID) {
            Qs_LightComp *lc = qs_entity_add(proto_scene, sun, qs_light_comp_type());
            if (lc) {
                lc->color[0]  = 1.0f;
                lc->color[1]  = 0.95f;
                lc->color[2]  = 0.9f;
                lc->intensity = 3.0f;
                editor->proto_preview_light = sun;
            } else {
                qs_entity_destroy(proto_scene, sun);
            }
        }
    }

    /* Reset camera for prototype view */
    ed_camera_init(&editor->cam);
    if (editor->scene_renderer) {
        Qs_Camera *cam = qs_renderer_camera(editor->scene_renderer);
        if (cam) {
            cam->position[0] =  5.0f;
            cam->position[1] =  4.0f;
            cam->position[2] =  8.0f;
            cam->target[0]   =  0.0f;
            cam->target[1]   =  0.5f;
            cam->target[2]   =  0.0f;
        }
    }

    /* Update breadcrumb bar */
    if (s_breadcrumb_bar) {
        Qs_Scene *prev =
            editor->proto_stack_scene[editor->proto_stack_depth - 1];
        const char *scene_name_str = prev ? qs_scene_name(prev) : "Scene";
        ca_set_text(s_breadcrumb_scene_label, scene_name_str);
        ca_set_text(s_breadcrumb_proto_label, qs_scene_name(proto_scene));
        ca_set_hidden(s_breadcrumb_bar, false);
    }

    editor->mode = ED_MODE_PROTOTYPE;
    QS_LOG_INFO("Editing prototype: %s", proto_path);
    return true;
}

void editor_close_prototype(Editor *editor)
{
    if (!editor || editor->proto_stack_depth == 0) return;

    /* Save the current prototype scene back to its source path. */
    Qs_Scene *proto_scene = qs_scene_active();
    /* Strip the editor-only preview light first so it never lands in
       the saved .qproto. */
    if (proto_scene && editor->proto_preview_light != QS_ENTITY_INVALID) {
        if (qs_entity_valid(proto_scene, editor->proto_preview_light))
            qs_entity_destroy(proto_scene, editor->proto_preview_light);
        editor->proto_preview_light = QS_ENTITY_INVALID;
    }
    if (proto_scene && editor->proto_path[0])
        qs_scene_save(proto_scene, editor->proto_path);
    if (proto_scene)
        qs_scene_destroy(proto_scene);

    /* Pop the previous scene + camera */
    editor->proto_stack_depth--;
    Qs_Scene *prev = editor->proto_stack_scene[editor->proto_stack_depth];
    qs_scene_set_active(prev);
    editor->cam = editor->proto_stack_cam[editor->proto_stack_depth];

    editor->selected_entity   = QS_ENTITY_INVALID;
    editor->proto_owner       = QS_ENTITY_INVALID;
    editor->proto_inner_scene = NULL;
    editor->proto_path[0]     = '\0';

    if (editor->proto_stack_depth == 0) {
        if (s_breadcrumb_bar) ca_set_hidden(s_breadcrumb_bar, true);
        editor->mode = ED_MODE_SCENE;
    }
    QS_LOG_INFO("Returned to %s", prev ? qs_scene_name(prev) : "(none)");
}

void editor_destroy(Editor *ed)
{
    if (!ed) return;
    /* Pop and discard any prototype edit stack (don't auto-save). */
    while (ed->proto_stack_depth > 0) {
        Qs_Scene *cur = qs_scene_active();
        if (cur) qs_scene_destroy(cur);
        ed->proto_stack_depth--;
        qs_scene_set_active(ed->proto_stack_scene[ed->proto_stack_depth]);
    }
    ed_pick_shutdown(ed->engine);
    ed_gizmo_shutdown(ed->engine);
    ed_undo_shutdown();
    ed_keybinds_shutdown();
    ed_thumbnail_shutdown();
    qs_engine_destroy(ed->engine);
    qs_project_destroy(ed->project);
    qs_free(ed);
}
