#include "ed_menu_bar.h"
#include "ed_file_browser.h"
#include "ed_import_dialog.h"
#include "ed_plugin_manager.h"
#include "ed_settings.h"
#include "ed_project_settings.h"
#include "ed_renderer_settings.h"
#include "editor.h"

#include <stdio.h>
#include <string.h>

/* ================================================================
   EDITOR MENU BAR — static File menu + dynamic extension menus
   ================================================================

   ed_menu_bar_init() sets up static state and does the first build.
   ed_menu_bar_invalidate() is called on plugin events to schedule
   a rebuild.  ed_menu_bar_sync() runs once per frame and only
   rebuilds when the dirty flag is set.
   ================================================================ */

#define MAX_MENU_EXTENSIONS 16

static Ca_Window *s_window;
static void      *s_editor;
static bool       s_dirty;

static void action_open_file(void *user_data)
{
    (void)user_data;
    static const EdFBFilter filters[] = {
        { "Scene Files (*.qscene)",     ".qscene" },
        { "Project Files (*.quasar)",   ".quasar" },
        { "JSON Files (*.json)",        ".json" },
    };
    ed_file_browser_open(&(EdFBDesc){
        .mode         = ED_FB_OPEN_FILE,
        .title        = "\xEF\x81\xBC  Open File",
        .filters      = filters,
        .filter_count = 3,
    });
}

static void action_open_folder(void *user_data)
{
    (void)user_data;
    ed_file_browser_open(&(EdFBDesc){
        .mode  = ED_FB_OPEN_FOLDER,
        .title = "\xEF\x81\xBC  Open Folder",
    });
}

static void on_import_path_selected(const char *path, void *user_data)
{
    (void)user_data;
    if (path && *path) ed_import_dialog_open(path);
}

static void action_import_asset(void *user_data)
{
    (void)user_data;
    static const EdFBFilter filters[] = {
        { "glTF / GLB (*.gltf, *.glb)", ".gltf,.glb" },
    };
    ed_file_browser_open(&(EdFBDesc){
        .mode         = ED_FB_OPEN_FILE,
        .title        = "Import Asset",
        .filters      = filters,
        .filter_count = 1,
        .on_confirm   = on_import_path_selected,
    });
}

static void action_exit(void *user_data)
{
    Editor *ed = (Editor *)user_data;
    editor_request_exit(ed);
}

static void action_save_scene(void *user_data)
{
    Editor *ed = (Editor *)user_data;
    editor_save_scene(ed);
}

static void action_save_project(void *user_data)
{
    Editor *ed = (Editor *)user_data;
    editor_save_project(ed);
}

static void action_undo(void *user_data)
{
    Editor *ed = (Editor *)user_data;
    editor_undo(ed);
}

static void action_redo(void *user_data)
{
    Editor *ed = (Editor *)user_data;
    editor_redo(ed);
}

static void action_manage_plugins(void *user_data)
{
    (void)user_data;
    ed_plugin_manager_open();
}

static void action_open_settings(void *user_data)
{
    (void)user_data;
    ed_settings_open();
}

static void action_open_renderer_settings(void *user_data)
{
    ed_renderer_settings_open(user_data);
}

static void action_open_project_settings(void *user_data)
{
    (void)user_data;
    ed_project_settings_open();
}

/* ---- Per-extension item storage for the current sync ---- */

typedef struct {
    char             label[128];
    Ca_MenuItemDesc  items[QS_MENU_MAX_ITEMS];
    int              item_count;
} MenuExtSlot;

static MenuExtSlot s_ext_slots[MAX_MENU_EXTENSIONS];

/* ------------------------------------------------------------------ */

static void menu_bar_rebuild(void)
{
    if (!s_window) return;

    Editor    *ed     = (Editor *)s_editor;
    Qs_Engine *engine = editor_engine(ed);

    /* ---- Build sub-menus from menu extensions ---- */
    int ext_menu_count = 0;
    memset(s_ext_slots, 0, sizeof(s_ext_slots));

    if (engine) {
        uint32_t n = qs_engine_ext_count(engine, QS_EXT_EDITOR_MENU);
        for (uint32_t i = 0; i < n && ext_menu_count < MAX_MENU_EXTENSIONS; i++) {
            const Qs_MenuExt *ext  = qs_engine_ext_interface(engine, QS_EXT_EDITOR_MENU, i);
            void             *data = qs_engine_ext_data(engine, QS_EXT_EDITOR_MENU, i);
            if (!ext || !ext->get_items) continue;

            MenuExtSlot *slot = &s_ext_slots[ext_menu_count];
            const char *label = ext->label ? ext->label : "(extension)";
            snprintf(slot->label, sizeof(slot->label), "%s", label);
            slot->item_count = 0;
            ext->get_items(data, engine, slot->items, &slot->item_count);
            if (slot->item_count > 0)
                ext_menu_count++;
        }
    }

    /* ---- Static "Manage Plugins..." item ---- */
    Ca_MenuItemDesc manage_item = {
        .label       = "Manage Plugins...",
        .action      = action_manage_plugins,
        .action_data = NULL,
    };

    /* ---- Assemble top-level Plugins menu items ---- */
    #define PLUGINS_FLAT_MAX (2 + MAX_MENU_EXTENSIONS)
    Ca_MenuItemDesc plugins_items[PLUGINS_FLAT_MAX];
    int             plugins_item_count = 0;

    plugins_items[plugins_item_count++] = manage_item;

    if (ext_menu_count > 0) {
        Ca_MenuItemDesc sep = { .separator = true };
        plugins_items[plugins_item_count++] = sep;
    }

    for (int p = 0; p < ext_menu_count; p++) {
        MenuExtSlot *slot = &s_ext_slots[p];
        Ca_MenuItemDesc plugin_item = {
            .label          = slot->label,
            .action         = NULL,
            .action_data    = NULL,
            .separator      = false,
            .sub_items      = slot->items,
            .sub_item_count = slot->item_count,
        };
        plugins_items[plugins_item_count++] = plugin_item;
    }
    #undef PLUGINS_FLAT_MAX

    /* ---- Static File menu ---- */
    Ca_MenuItemDesc file_items[] = {
        { .label = "Open File...",        .action = action_open_file,    .action_data = s_editor },
        { .label = "Open Folder...",      .action = action_open_folder,  .action_data = s_editor },
        { .label = "Import Asset...",     .action = action_import_asset, .action_data = s_editor },
        { .separator = true },
        { .label = "Save Scene\t\xE2\x8C\x83S",         .action = action_save_scene,   .action_data = s_editor },
        { .label = "Save Project\t\xE2\x87\xA7\xE2\x8C\x83S", .action = action_save_project, .action_data = s_editor },
        { .separator = true },
        { .label = "Exit",                .action = action_exit,         .action_data = s_editor },
    };

    /* ---- Static Edit menu ---- */
    Ca_MenuItemDesc edit_items[] = {
        { .label = "Undo\t\xE2\x8C\x83Z", .action = action_undo, .action_data = s_editor },
        { .label = "Redo\t\xE2\x8C\x83Y", .action = action_redo, .action_data = s_editor },
    };

    /* ---- Push all menus to the title bar ---- */
    Ca_MenuDesc menus[4] = {
        { .label = "File",     .items = file_items,    .item_count = (int)(sizeof(file_items)/sizeof(file_items[0])) },
        { .label = "Edit",     .items = edit_items,    .item_count = (int)(sizeof(edit_items)/sizeof(edit_items[0])) },
        { .label = "Plugins",  .items = plugins_items, .item_count = plugins_item_count },
        { .label = "Settings", .items = (Ca_MenuItemDesc[]){
            { .label = "Editor Settings",   .action = action_open_settings,         .action_data = NULL },
            { .label = "Project Settings",  .action = action_open_project_settings, .action_data = NULL },
          }, .item_count = 2 },
    };

    ca_window_set_title_bar_menus(s_window, menus, 4);
}

void ed_menu_bar_init(Ca_Window *window, void *editor)
{
    s_window = window;
    s_editor = editor;
    s_dirty  = false;
    menu_bar_rebuild();
}

void ed_menu_bar_invalidate(void)
{
    s_dirty = true;
}

void ed_menu_bar_sync(void)
{
    if (!s_dirty) return;
    s_dirty = false;
    menu_bar_rebuild();
}

