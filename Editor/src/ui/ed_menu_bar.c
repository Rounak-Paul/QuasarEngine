#include "ed_menu_bar.h"
#include "ed_file_browser.h"
#include "ed_plugin_manager.h"
#include "editor.h"

#include <stdio.h>
#include <string.h>

/* ================================================================
   EDITOR MENU BAR — static File menu + dynamic Plugins menu
   ================================================================

   ed_menu_bar() builds the host div ONCE during editor_build_ui.
   ed_menu_bar_update() clears + rebuilds the menu bar every frame
   so the Plugins menu reflects the current set of loaded plugins.
   Each loaded plugin may contribute up to ED_PLUGIN_MENU_MAX_ITEMS
   items via its on_editor_menu callback.
   ================================================================ */

/* Max plugin menus we'll include as top-level Plugins sub-menus */
#define MAX_PLUGIN_MENUS  16

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

/* ---- Per-plugin item storage for the current frame ---- */

typedef struct {
    /* plugin name (for the sub-menu label) */
    char             label[128];
    /* items filled by on_editor_menu */
    Ca_MenuItemDesc  items[ED_PLUGIN_MENU_MAX_ITEMS];
    int              item_count;
} PluginMenuSlot;

static PluginMenuSlot s_plugin_slots[MAX_PLUGIN_MENUS];

/* Fingerprint of the last-built plugin menu state.
   Rebuild only when this changes — so the active dropdown
   is never destroyed mid-interaction. */
static int s_menu_fingerprint = -1;  /* -1 forces first-frame build */

static int plugin_fingerprint(Qs_PluginManager *pm)
{
    if (!pm) return 0;
    int fp = (int)qs_plugin_count(pm) + 1;
    uint32_t total = qs_plugin_count(pm);
    for (uint32_t i = 0; i < total; i++) {
        const Qs_PluginState *state = qs_plugin_state_at(pm, i);
        if (state && qs_plugin_state_loaded(state))
            fp = fp * 31 + (int)(i + 1);
    }
    return fp;
}

/* ------------------------------------------------------------------ */

Ca_Div *ed_menu_bar(Ca_Window *window, void *editor)
{
    (void)window; (void)editor;
    Ca_Div *host = ca_div_begin(&(Ca_DivDesc){
        .style = "menu-bar-host",
    });
    ca_div_end();
    return host;
}

void ed_menu_bar_update(Ca_Div *host, Ca_Window *window, void *editor)
{
    (void)window;
    if (!host) return;

    Editor           *ed = (Editor *)editor;
    Qs_Engine        *engine = editor_engine(ed);
    Qs_PluginManager *pm     = engine ? qs_engine_plugin_manager(engine) : NULL;

    /* Skip the rebuild if plugin state hasn't changed.
       This preserves active_menu across frames so dropdowns stay open. */
    int fp = plugin_fingerprint(pm);
    if (fp == s_menu_fingerprint) return;
    s_menu_fingerprint = fp;

    /* ---- Build plugin sub-menus from loaded plugins ---- */
    int plugin_menu_count = 0;
    memset(s_plugin_slots, 0, sizeof(s_plugin_slots));

    if (pm) {
        uint32_t total = qs_plugin_count(pm);
        for (uint32_t i = 0; i < total && plugin_menu_count < MAX_PLUGIN_MENUS; i++) {
            const Qs_PluginState *state = qs_plugin_state_at(pm, i);
            if (!state || !qs_plugin_state_loaded(state)) continue;
            const Qs_PluginDesc *desc = qs_plugin_state_desc(state);
            if (!desc || !desc->on_editor_menu) continue;

            PluginMenuSlot *slot = &s_plugin_slots[plugin_menu_count];
            const char *name = desc->name ? desc->name : qs_plugin_state_id(state);
            snprintf(slot->label, sizeof(slot->label), "%s",
                     name ? name : "(plugin)");
            slot->item_count = 0;
            desc->on_editor_menu(engine,
                                  slot->items, &slot->item_count);
            if (slot->item_count > 0)
                plugin_menu_count++;
        }
    }

    /* ---- Static "Manage Plugins..." item ---- */
    Ca_MenuItemDesc manage_item = {
        .label       = "Manage Plugins...",
        .action      = action_manage_plugins,
        .action_data = NULL,
    };

    /* ---- Assemble top-level Plugins menu ---- */
    /* manage + separator + one entry per plugin (plugin items are sub-menus) */

    #define PLUGINS_FLAT_MAX (2 + MAX_PLUGIN_MENUS)
    Ca_MenuItemDesc plugins_items[PLUGINS_FLAT_MAX];
    int             plugins_item_count = 0;

    plugins_items[plugins_item_count++] = manage_item;

    /* Separator after "Manage Plugins..." (only when plugins are present) */
    if (plugin_menu_count > 0) {
        Ca_MenuItemDesc sep = { .separator = true };
        plugins_items[plugins_item_count++] = sep;
    }

    for (int p = 0; p < plugin_menu_count; p++) {
        PluginMenuSlot *slot = &s_plugin_slots[p];
        /* Top-level item = plugin name; sub_items = its menu items */
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

    /* ---- Assemble all menus ---- */
    Ca_MenuDesc menus[2] = {
        { .label = "File",    .items = file_items,     .item_count = 3 },
        { .label = "Plugins", .items = plugins_items,  .item_count = plugins_item_count },
    };

    ca_div_clear(host);
    ca_menu_bar(&(Ca_MenuBarDesc){
        .menus      = menus,
        .menu_count = 2,
        .style      = "menu-bar",
    });
    ca_div_end();
}

