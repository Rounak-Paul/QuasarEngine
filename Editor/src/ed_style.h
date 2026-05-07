/*
 * ed_style.h — Quasar Editor design-system constants.
 *
 * Single source of truth for every size, color, and structural dimension
 * used in editor and launcher CSS.  The compiled CSS strings
 * (g_editor_css, g_launcher_css) are defined in ed_style.c.
 *
 * ── How to rescale the whole UI ─────────────────────────────────────
 * Change ED_UI_SCALE to any float.
 *
 * ED_UI_SCALE is passed to ca_window_set_scale() after every window is
 * created.  That is Causality's true UI zoom — it multiplies ALL widget
 * sizes, paddings, gaps, and text rendering uniformly, like browser zoom.
 *
 * ED_FONT_SIZE_PX is the base font atlas size passed to
 * qs_engine_create / ca_instance_create.  It is a fixed logical pixel
 * value independent of scale; ca_window_set_scale handles the zoom.
 *
 * CSS string tokens (ED_FS, ED_H_*, ED_R_*) are base pixel values at
 * scale 1.0.  They are compile-time string literals — no runtime math
 * needed because ca_window_set_scale multiplies them at render time.
 * ────────────────────────────────────────────────────────────────────
 */

#ifndef ED_STYLE_H
#define ED_STYLE_H

/* ------------------------------------------------------------------
   Master scale  —  change this single value to rescale the whole UI.
   Passed to ca_window_set_scale() on every window after creation.
   Any positive float is valid: 1.0 = 100 %, 1.5 = 150 %, 2.0 = 200 %
   ------------------------------------------------------------------ */

#define ED_UI_SCALE     1.4f

/* ------------------------------------------------------------------
   Font size
   ------------------------------------------------------------------ */

/* Base font atlas size in logical pixels. Independent of ED_UI_SCALE —
   the scale is handled by ca_window_set_scale, not the font atlas. */
#define ED_FONT_PX      16

/* Passed to qs_engine_create / ca_instance_create as font_size_px. */
#define ED_FONT_SIZE_PX ((float)ED_FONT_PX)

/* ------------------------------------------------------------------
   CSS tokens  (base values at scale 1.0)
   These are fixed string literals embedded in the CSS bundles.
   Causality scales the rendered output via font_size_px above.
   ------------------------------------------------------------------ */

/* Unified font size */
#define ED_FS            "12"

/* Runtime logical sizes for APIs that take float values (not CSS strings). */
#define ED_H_CHROME_F    20.0f
#define ED_H_TAB_BAR_F   22.0f

/* Heights (logical pixels) */
#define ED_H_ROW_TIGHT   "16"   /* tight list rows: hierarchy, console     */
#define ED_H_INPUT_MINI  "17"   /* compact vec/scalar inspector inputs     */
#define ED_H_INPUT_SMALL "18"   /* small tag inputs, remove buttons        */
#define ED_H_CHROME      "20"   /* toolbar, status bar                     */
#define ED_H_TAB_BAR     "22"   /* panel tab bars, column headers          */
#define ED_H_CRUMB_BAR   "24"   /* breadcrumb bar, nav buttons             */
#define ED_H_ROW_LG      "26"   /* file-browser entries, standard inputs   */
#define ED_H_ROW_FORM    "28"   /* form inputs, launcher buttons           */
#define ED_H_FORM_ROW    "30"   /* launcher form rows                      */
#define ED_H_PANEL_HDR   "32"   /* heavy panel headers (plugin manager)    */
#define ED_H_ITEM_MD     "34"   /* launcher tabs, form action bars         */
#define ED_H_TITLE_BAR   "36"   /* file-browser title bar, import footer   */
#define ED_H_NAV_BAR     "38"   /* file-browser navigation bar             */
#define ED_H_PAGE_HDR    "44"   /* launcher page header                    */
#define ED_H_CARD        "48"   /* launcher project entry card             */

/* Corner radii */
#define ED_R_SM    "3"
#define ED_R_BASE  "4"
#define ED_R_LG    "8"

/* ------------------------------------------------------------------
   Color palette  (CSS hex string tokens)
   ------------------------------------------------------------------ */

/* Background hierarchy — darkest → lightest */
#define ED_COL_VOID          "#0d0d0f"   /* deepest inset, inputs, viewport  */
#define ED_COL_BASE          "#111114"   /* primary panel background         */
#define ED_COL_BASE_ALT      "#13131a"   /* alternate-row tint               */
#define ED_COL_ELEVATED      "#16161a"   /* panel chrome, toolbars           */
#define ED_COL_ELEVATED_ALT  "#16161c"   /* heavy panel headers (warm tint)  */
#define ED_COL_SURFACE       "#1c1c22"   /* tab bars, headers, dividers      */
#define ED_COL_SEPARATOR     "#1e2030"   /* deep rule lines                  */
#define ED_COL_OVERLAY       "#242430"   /* hover states, selected items     */
#define ED_COL_BORDER        "#2e2e3e"   /* input borders, thin dividers     */

/* Text */
#define ED_COL_TEXT_VIVID    "#e0e4ff"   /* very bright labels               */
#define ED_COL_TEXT_BRIGHT   "#c8d0ff"   /* standard interactive text        */
#define ED_COL_TEXT_MEDIUM   "#a0a8c8"   /* mid-weight labels                */
#define ED_COL_TEXT_MUTED    "#8890b0"   /* secondary labels, hints          */
#define ED_COL_TEXT_SEC      "#5a5e7a"   /* column headers, remove buttons   */
#define ED_COL_TEXT_DIM      "#4a4e6a"   /* disabled, placeholder            */

/* Accent */
#define ED_COL_PRIMARY       "#6e8aff"   /* active tabs, primary buttons     */
#define ED_COL_DANGER        "#ff6b6b"   /* errors, destructive actions      */
#define ED_COL_SUCCESS       "#6bffb8"   /* play, confirm                    */
#define ED_COL_WARNING       "#ffd166"   /* warnings, overridden fields      */
#define ED_COL_ORANGE        "#ff8c42"   /* scene objects, mesh icons        */

/* XYZ axis */
#define ED_COL_AXIS_X        "#ff5370"
#define ED_COL_AXIS_Y        "#80ff80"
#define ED_COL_AXIS_Z        "#5b9cff"
#define ED_COL_AXIS_W        "#bf80ff"

/* ------------------------------------------------------------------
   CSS bundles  (defined in ed_style.c)
   ------------------------------------------------------------------ */

extern const char *g_editor_css;
extern const char *g_launcher_css;

#endif /* ED_STYLE_H */

