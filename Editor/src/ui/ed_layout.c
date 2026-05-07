#include "ed_layout.h"
#include "ed_hierarchy.h"
#include "ed_inspector.h"
#include "editor.h"
#include "ca_theme.h"

#include <stdio.h>
#include <string.h>

/* ---- Console ---- */
#define CONSOLE_MAX_LINES 100

static Ca_Window *s_console_window;
static Ca_Label  *s_console_lines[CONSOLE_MAX_LINES];
static uint32_t   s_prev_log_count;
static bool        s_needs_scroll;

static void panel_tabs(const char **labels, int count, int active)
{
    ca_tabs(&(Ca_TabBarDesc){
        .labels        = labels,
        .count         = count,
        .active        = active,
        .style         = "panel-tab-bar",
        .active_text   = CA_THEME_TEXT_BRIGHT,
        .inactive_text = CA_THEME_TEXT_DIM,
        .active_bg     = CA_THEME_BG_OVERLAY,
        .inactive_bg   = CA_THEME_TRANSPARENT,
        .tab_padding_x = 0.0f,
        .tabs_fill     = (count == 1),
        .tabs_left_align = true,
    });
}

static uint32_t log_level_color(Qs_LogLevel level)
{
    switch (level) {
    case QS_LOG_DEBUG: return CA_THEME_TEXT_DIM;
    case QS_LOG_TRACE: return CA_THEME_TEXT_MUTED;
    case QS_LOG_INFO:  return CA_THEME_SUCCESS;
    case QS_LOG_WARN:  return CA_THEME_WARNING;
    case QS_LOG_ERROR: return CA_THEME_DANGER;
    case QS_LOG_FATAL: return CA_THEME_FATAL;
    default:           return CA_THEME_TEXT_DIM;
    }
}

void ed_layout(Ca_Window *window, void *editor)
{
    (void)editor;

    s_console_window = window;

    /* Vertical split: top three-panel area | bottom panel (full width) */
    ca_split_begin(&(Ca_SplitDesc){
        .direction       = CA_VERTICAL,
        .ratio           = 0.72f,
        .min_ratio       = 0.40f,
        .max_ratio       = 0.90f,
        .bar_size        = 1.0f,
        .bar_color       = CA_THEME_BG_VOID,
        .bar_hover_color = CA_THEME_ACCENT,
    });
    {
        /* ---- Top: three-column horizontal split ---- */
        ca_split_begin(&(Ca_SplitDesc){
            .direction       = CA_HORIZONTAL,
            .ratio           = 0.15f,
            .min_ratio       = 0.10f,
            .max_ratio       = 0.30f,
            .bar_size        = 1.0f,
            .bar_color       = CA_THEME_BG_VOID,
            .bar_hover_color = CA_THEME_ACCENT,
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
            {
                static const char *tabs[] = { "Hierarchy" };
                panel_tabs(tabs, 1, 0);
            }
            ca_div_end();
            ed_hierarchy(editor);
            ca_div_end();

            /* Center + Right split */
            ca_split_begin(&(Ca_SplitDesc){
                .direction       = CA_HORIZONTAL,
                .ratio           = 0.75f,
                .min_ratio       = 0.40f,
                .max_ratio       = 0.88f,
                .bar_size        = 1.0f,
                .bar_color       = CA_THEME_BG_VOID,
                .bar_hover_color = CA_THEME_ACCENT,
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
                {
                    static const char *tabs[] = { "Scene" };
                    panel_tabs(tabs, 1, 0);
                }
                ca_div_end();
                {
                    Ca_Viewport *vp = ca_viewport(&(Ca_ViewportDesc){ 0 });
                    qs_renderer_bind(editor_scene_renderer(editor), (Qs_Viewport *)vp);
                    editor_set_scene_viewport(editor, vp);
                }
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
                {
                    static const char *tabs[] = { "Inspector" };
                    panel_tabs(tabs, 1, 0);
                }
                ca_div_end();
                ed_inspector(editor);
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
            panel_tabs(bottom_tabs, 2, 0);

            /* Console content — scrollable log lines */
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_VERTICAL,
                .style     = "console-scroll",
                .id        = "console",
            });
            for (uint32_t i = 0; i < CONSOLE_MAX_LINES; i++) {
                s_console_lines[i] = ca_text(&(Ca_TextDesc){
                    .text   = "",
                    .style  = "console-line",
                    .hidden = true,
                });
            }
            ca_div_end();
        }
        ca_div_end();
    }
    ca_split_end();
}

void ed_console_update(void *editor)
{
    (void)editor;

    uint32_t count = 0;
    const Qs_LogEntry *entries = qs_log_entries(&count);

    /* Scroll to bottom on the NEXT frame so content_h is up to date */
    if (s_needs_scroll) {
        s_needs_scroll = false;
        ca_scroll_to_bottom(s_console_window, "console");
    }

    if (count == s_prev_log_count) return;
    s_prev_log_count = count;
    s_needs_scroll   = true;   /* scroll after next layout pass */

    /* Show the most recent entries that fit in the label pool */
    uint32_t start   = count > CONSOLE_MAX_LINES ? count - CONSOLE_MAX_LINES : 0;
    uint32_t visible = count - start;

    char line_buf[512];
    for (uint32_t i = 0; i < CONSOLE_MAX_LINES; i++) {
        if (i < visible) {
            const Qs_LogEntry *e = &entries[start + i];
            int hrs = (int)(e->timestamp / 3600.0);
            int min = (int)(e->timestamp / 60.0) % 60;
            int sec = (int)e->timestamp % 60;
            int ms  = (int)((e->timestamp - (int)e->timestamp) * 1000.0);

            snprintf(line_buf, sizeof(line_buf),
                     "[%02d:%02d:%02d.%03d] [%s] %s",
                     hrs, min, sec, ms,
                     qs_log_level_str(e->level), e->message);

            ca_set_text(s_console_lines[i], line_buf);
            ca_set_color(s_console_lines[i], log_level_color(e->level));
            ca_set_hidden(s_console_lines[i], false);
        } else {
            ca_set_hidden(s_console_lines[i], true);
        }
    }
}

/* ================================================================
   STATUS BAR
   ================================================================ */

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

/* ================================================================
   TOOLBAR ICON BUTTON
   ================================================================ */

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

    if (desc->tooltip)
        ca_tooltip(&(Ca_TooltipDesc){ .text = desc->tooltip });

    return btn;
}

void ed_icon_btn_set_active(Ca_Button *btn, bool active)
{
    if (!btn) return;
    ca_set_style(btn, active ? "toolbar-icon-btn active" : "toolbar-icon-btn");
}
