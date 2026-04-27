#include "ed_icon_btn.h"

Ca_Button *ed_icon_btn(const EdIconBtnDesc *desc)
{
    if (!desc || !desc->icon) return NULL;

    Ca_Button *btn = ca_btn_begin(&(Ca_BtnDesc){
        .text       = desc->icon,
        .id         = desc->id,
        .style      = "toolbar-icon-btn",
        .on_click   = desc->on_click,
        .click_data = desc->click_data,
    });
    ca_btn_end();
    return btn;
}

void ed_icon_btn_set_active(Ca_Button *btn, bool active)
{
    if (!btn) return;
    ca_set_style(btn, active ? "toolbar-icon-btn active" : "toolbar-icon-btn");
}
