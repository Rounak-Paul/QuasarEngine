#include "ed_icon_btn.h"

void ed_icon_btn(const EdIconBtnDesc *desc)
{
    if (!desc || !desc->icon) return;

    const char *style = desc->active ? "toolbar-icon-btn active" : "toolbar-icon-btn";

    ca_btn(&(Ca_BtnDesc){
        .text       = desc->icon,
        .id         = desc->id,
        .style      = style,
        .on_click   = desc->on_click,
        .click_data = desc->click_data,
    });
}
