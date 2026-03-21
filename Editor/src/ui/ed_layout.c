#include "ed_layout.h"

#define SPLIT_BAR_COLOR       ca_color(0.14f, 0.14f, 0.20f, 1.0f)
#define SPLIT_BAR_HOVER_COLOR ca_color(0.30f, 0.50f, 0.85f, 1.0f)

void ed_layout(Ca_Window *window, void *editor)
{
    (void)window;
    (void)editor;

    /* Vertical split: top three-panel area | bottom panel (full width) */
    ca_split_begin(&(Ca_SplitDesc){
        .direction       = CA_VERTICAL,
        .ratio           = 0.72f,
        .min_ratio       = 0.40f,
        .max_ratio       = 0.90f,
        .bar_size        = 1.0f,
        .bar_color       = SPLIT_BAR_COLOR,
        .bar_hover_color = SPLIT_BAR_HOVER_COLOR,
    });
    {
        /* ---- Top: three-column horizontal split ---- */
        ca_split_begin(&(Ca_SplitDesc){
            .direction       = CA_HORIZONTAL,
            .ratio           = 0.15f,
            .min_ratio       = 0.10f,
            .max_ratio       = 0.30f,
            .bar_size        = 1.0f,
            .bar_color       = SPLIT_BAR_COLOR,
            .bar_hover_color = SPLIT_BAR_HOVER_COLOR,
        });
        {
            /* Left panel — Hierarchy */
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_VERTICAL,
                .style     = "panel",
            });
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_HORIZONTAL,
                .style     = "panel-tab-bar",
            });
            ca_text(&(Ca_TextDesc){ .text = "Hierarchy", .style = "panel-tab active" });
            ca_div_end();
            ca_div_end();

            /* Center + Right split */
            ca_split_begin(&(Ca_SplitDesc){
                .direction       = CA_HORIZONTAL,
                .ratio           = 0.75f,
                .min_ratio       = 0.40f,
                .max_ratio       = 0.88f,
                .bar_size        = 1.0f,
                .bar_color       = SPLIT_BAR_COLOR,
                .bar_hover_color = SPLIT_BAR_HOVER_COLOR,
            });
            {
                /* Center — Scene viewport */
                ca_div_begin(&(Ca_DivDesc){
                    .direction = CA_VERTICAL,
                    .style     = "panel panel-viewport",
                });
                ca_div_begin(&(Ca_DivDesc){
                    .direction = CA_HORIZONTAL,
                    .style     = "panel-tab-bar",
                });
                ca_text(&(Ca_TextDesc){ .text = "Scene", .style = "panel-tab active" });
                ca_div_end();
                ca_div_end();

                /* Right panel — Inspector */
                ca_div_begin(&(Ca_DivDesc){
                    .direction = CA_VERTICAL,
                    .style     = "panel",
                });
                ca_div_begin(&(Ca_DivDesc){
                    .direction = CA_HORIZONTAL,
                    .style     = "panel-tab-bar",
                });
                ca_text(&(Ca_TextDesc){ .text = "Inspector", .style = "panel-tab active" });
                ca_div_end();
                ca_div_end();
            }
            ca_split_end();
        }
        ca_split_end();

        /* ---- Bottom: Console / Assets (full width) ---- */
        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_VERTICAL,
            .style     = "panel panel-bottom",
        });
        {
            static const char *bottom_tabs[] = { "Console", "Assets" };
            ca_tabs(&(Ca_TabBarDesc){
                .labels        = bottom_tabs,
                .count         = 2,
                .active        = 0,
                .style         = "panel-tab-bar",
                .active_text   = ca_color(0.69f, 0.69f, 0.80f, 1.0f),
                .inactive_text = ca_color(0.33f, 0.33f, 0.40f, 1.0f),
                .active_bg     = ca_color(0.09f, 0.09f, 0.18f, 1.0f),
                .inactive_bg   = ca_color(0.07f, 0.07f, 0.13f, 0.0f),
            });
        }
        ca_div_end();
    }
    ca_split_end();
}
