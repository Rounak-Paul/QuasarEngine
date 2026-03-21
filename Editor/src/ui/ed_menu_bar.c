#include "ed_menu_bar.h"
#include "editor.h"

static void action_exit(void *user_data)
{
    Editor *ed = (Editor *)user_data;
    editor_request_exit(ed);
}

void ed_menu_bar(Ca_Window *window, void *editor)
{
    (void)window;

    Ca_MenuItemDesc file_items[] = {
        { .label = "Exit", .action = action_exit, .action_data = editor },
    };

    Ca_MenuDesc menus[] = {
        { .label = "File", .items = file_items, .item_count = 1 },
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
