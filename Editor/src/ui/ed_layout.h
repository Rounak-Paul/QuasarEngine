#ifndef ED_LAYOUT_H
#define ED_LAYOUT_H

#include "quasar.h"

/* ---- Main editor shell ---- */

void ed_layout(Ca_Window *window, void *editor);

/// Refreshes the console panel with latest log entries.
void ed_console_update(void *editor);

/// Refreshes the bottom status bar with live editor metrics.
void ed_status_bar_update(void *editor);

/// Bottom status bar.
void ed_status_bar(Ca_Window *window, void *editor);

/* ---- Icon glyphs (Font Awesome / Codicon code points) ---- */

#define ICON_SCENE      "\xEF\x82\xAC"   /* U+F0AC globe     */
#define ICON_ENTITY     "\xEF\x84\x91"   /* U+F111 circle    */
#define ICON_MESH       "\xEF\x86\xB2"   /* U+F1B2 cube      */
#define ICON_LIGHT      "\xEF\x83\xAB"   /* U+F0EB lightbulb */
#define ICON_COMPONENT  "\xEF\x80\x93"   /* U+F013 cog       */
#define ICON_TRANSFORM  "\xEF\x82\xB2"   /* U+F0B2 arrows    */
#define ICON_ID         "\xEF\x8A\x92"   /* U+F292 hashtag   */
#define ICON_TAG        "\xEF\x81\x84"   /* U+F044 pencil    */
#define ICON_PROTOTYPE  "\xEF\x86\xB3"   /* U+F1B3 cubes     */
#define ICON_TRASH      "\xEF\x87\xB8"   /* U+F1F8 trash     */
#define ICON_PLUS       "\xEF\x81\xA7"   /* U+F067 plus         */
#define ICON_CHEVRON_D  "\xEF\x81\xB8"   /* U+F078 chevron-down */
#define ICON_COMPRESS   "\xEF\x81\xA6"   /* U+F066 compress / collapse-all */
#define ICON_EYE        "\xEF\x81\xAE"   /* U+F06E eye          */
#define ICON_EYE_SLASH  "\xEF\x81\xB0"   /* U+F070 eye-slash    */

/* ---- Toolbar icon buttons ---- */

typedef struct EdIconBtnDesc {
    const char       *icon;       ///< UTF-8 icon glyph.
    const char       *id;         ///< CSS id (must be stable across frames).
    const char       *tooltip;    ///< Tooltip on hover (NULL = none).
    Ca_ClickFn        on_click;
    void             *click_data;
} EdIconBtnDesc;

/// Creates a toolbar icon button styled with .toolbar-icon-btn.
Ca_Button *ed_icon_btn(const EdIconBtnDesc *desc);

/// Toggle the active/toggled visual state on a toolbar icon button.
void ed_icon_btn_set_active(Ca_Button *btn, bool active);

#endif
