#include "ed_toolbar.h"

void ed_toolbar(Ca_Window *window, void *editor)
{
    (void)window;
    (void)editor;

    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .style     = "toolbar",
    });
    ca_div_end();
}
