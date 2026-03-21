#include "ed_status_bar.h"

void ed_status_bar(Ca_Window *window, void *editor)
{
    (void)window;
    (void)editor;

    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .style     = "status-bar",
    });

    ca_text(&(Ca_TextDesc){ .text = "Quasar Editor", .style = "status-text" });

    ca_div_end();
}
