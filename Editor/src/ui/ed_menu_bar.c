#include "ed_menu_bar.h"
#include "ed_file_browser.h"
#include "ed_plugin_manager.h"
#include "editor.h"

#include <stdio.h>
#include <string.h>

/* ================================================================
   EDITOR MENU BAR — static File menu + dynamic extension menus
   ================================================================

   ed_menu_bar_sync() is called every frame.  It tracks a simple
   fingerprint of the currently registered menu extensions and only
   calls ca_window_set_title_bar_menus() when the fingerprint
   changes — leaving the causality menu bar widget undisturbed (so
   any open dropdown stays open) between changes.
   ================================================================ */

#define MAX_MENU_EXTENSIONS 16

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

static void action_exit(void *user_data)
{
    Editor *ed = (Editor *)user_data;
    editor_request_exit(ed);
}

static void action_manage_plugins(void *user_data)
{
    (void)user_data;
    ed_plugin_manager_open();
}

/* ---- Per-extension item storage for the current sync ---- */

typedef struct {
    char             label[128];
    Ca_MenuItemDesc  items[QS_MENU_MAX_ITEMS];
    int              item_count;
} MenuExtSlot;

static MenuExtSlot s_ext_slots[MAX_MENU_EXTENSIONS];

static int s_menu_fingerprint = -1;

static int menu_fingerprint(const Qs_Engine *engine)
{
    if (!engine) return 0;
    uint32_t n = qs_engine_ext_count(engine, QS_EXT_EDITOR_MENU);
    int fp = (int)n + 1;
    for (uint32_t i = 0; i < n; i++) {
        const Qs_MenuExt *ext = qs_engine_ext_interface(engine, QS_EXT_EDITOR_MENU, i);
        if (ext && ext->label)
            fp = fp * 31 + (int)(i + 1);
    }
    return fp;
}

/* ------------------------------------------------------------------ */

void ed_menu_bar_sync(Ca_Window *window, void *editor)
{
    if (!window) return;

    Editor    *ed     = (Editor *)editor;
    Qs_Engine *engine = editor_engine(ed);

    int fp = menu_fingerprint(engine);
    if (fp == s_menu_fingerprint) return;
    s_menu_fingerprint = fp;

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
        { .label = "Open File...",   .action = action_open_file,   .action_data = editor },
        { .label = "Open Folder...", .action = action_open_folder, .action_data = editor },
        { .label = "Exit",           .action = action_exit,        .action_data = editor },
    };

    /* ---- Push all menus to the title bar ---- */
    Ca_MenuDesc menus[2] = {
        { .label = "File",    .items = file_items,     .item_count = 3 },
        { .label = "Plugins", .items = plugins_items,  .item_count = plugins_item_count },
    };

    ca_window_set_title_bar_menus(window, menus, 2);
}

