#ifndef ED_ICON_BTN_H
#define ED_ICON_BTN_H

#include "quasar.h"

/// Descriptor for a toolbar icon button.
typedef struct EdIconBtnDesc {
    const char       *icon;       ///< UTF-8 icon glyph to display (Codicon / NF icons).
    const char       *id;         ///< CSS id for keying (must be stable across frames).
    const char       *tooltip;    ///< Tooltip text shown on hover (NULL = none).
    Ca_ClickFn        on_click;   ///< Click callback. NULL = no callback.
    void             *click_data; ///< Passed to on_click as user_data.
} EdIconBtnDesc;

/// Creates a toolbar icon button styled with .toolbar-icon-btn.
/// Returns the Ca_Button handle so callers can update its style at runtime.
Ca_Button *ed_icon_btn(const EdIconBtnDesc *desc);

/// Toggle the active/toggled visual state on a toolbar icon button.
void ed_icon_btn_set_active(Ca_Button *btn, bool active);

#endif /* ED_ICON_BTN_H */
