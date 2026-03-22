#include "ed_menu_bar.h"
#include "ed_file_browser.h"
#include "editor.h"

static void action_open_file(void *user_data)
{
    (void)user_data;
    static const EdFBFilter filters[] = {
        { "Scene Files (*.qscene)", ".qscene" },
        { "Project Files (*.quasar)", ".quasar" },
        { "JSON Files (*.json)", ".json" },
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

void ed_menu_bar(Ca_Window *window, void *editor)
{
    (void)window;

    Ca_MenuItemDesc file_items[] = {
        { .label = "Open File...",   .action = action_open_file,   .action_data = editor },
        { .label = "Open Folder...", .action = action_open_folder, .action_data = editor },
        { .label = "Exit",           .action = action_exit,        .action_data = editor },
    };

    Ca_MenuDesc menus[] = {
        { .label = "File", .items = file_items, .item_count = 3 },
    };

    ca_menu_bar(&(Ca_MenuBarDesc){
        .menus            = menus,
        .menu_count       = 1,
        .style            = "menu-bar",
        .header_highlight = ca_color(0.12f, 0.12f, 0.22f, 1.0f),
        .dropdown_bg      = ca_color(0.08f, 0.08f, 0.14f, 0.98f),
        .dropdown_border  = ca_color(0.16f, 0.16f, 0.26f, 1.0f),
        .dropdown_hover   = ca_color(0.16f, 0.16f, 0.28f, 1.0f),
        .dropdown_text    = ca_color(0.85f, 0.85f, 0.85f, 1.0f),
        .text_color       = ca_color(0.80f, 0.80f, 0.82f, 1.0f),
    });
}
