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
/* Console font: 0.8 × ED_FS = 9.6 ≈ 10px */
#define ED_FS_CONSOLE    "10"

/* Runtime logical sizes for APIs that take float values (not CSS strings). */
#define ED_H_CHROME_F    20.0f
#define ED_H_TAB_BAR_F   20.0f

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
#define ED_R_SM    "4"
#define ED_R_BASE  "6"
#define ED_R_LG    "10"

/* ------------------------------------------------------------------
   Color palette — Catppuccin Mocha
   https://catppuccin.com/palette

   Backgrounds follow the official Mocha surface hierarchy:
     Crust → Mantle → Base → Surface 0 → Surface 1 → Surface 2
   Text uses the Mocha text/subtext/overlay scale.
   Accents are the named Mocha accent colors verbatim.
   ------------------------------------------------------------------ */

/* Backgrounds — Crust (deepest) up through Surface 2 */
#define ED_COL_VOID          "#11111b"   /* Crust  — inputs, viewport wells  */
#define ED_COL_BASE          "#181825"   /* Mantle — primary panel surface   */
#define ED_COL_BASE_ALT      "#1b1b29"   /* between Mantle/Base — alt rows   */
#define ED_COL_ELEVATED      "#1e1e2e"   /* Base   — toolbars, panel chrome  */
#define ED_COL_ELEVATED_ALT  "#252535"   /* Base+  — heavy panel headers     */
#define ED_COL_SURFACE       "#313244"   /* Surface 0 — tab bars, headers    */
#define ED_COL_SEPARATOR     "#45475a"   /* Surface 1 — rule lines           */
#define ED_COL_OVERLAY       "#585b70"   /* Surface 2 — hover, selected      */
#define ED_COL_BORDER        "#6c7086"   /* Overlay 0 — input borders        */

/* Text — Text → Subtext 1 → Subtext 0 → Overlay 2 → Overlay 1 → Overlay 0 */
#define ED_COL_TEXT_VIVID    "#cdd6f4"   /* Text      — peak brightness      */
#define ED_COL_TEXT_BRIGHT   "#bac2de"   /* Subtext 1 — standard text        */
#define ED_COL_TEXT_MEDIUM   "#a6adc8"   /* Subtext 0 — mid-weight labels    */
#define ED_COL_TEXT_MUTED    "#9399b2"   /* Overlay 2 — secondary hints      */
#define ED_COL_TEXT_SEC      "#7f849c"   /* Overlay 1 — column headers       */
#define ED_COL_TEXT_DIM      "#6c7086"   /* Overlay 0 — disabled/placeholder */

/* Accents — Mocha named colors */
#define ED_COL_PRIMARY       "#cba6f7"   /* Mauve  — active tabs, actions    */
#define ED_COL_DANGER        "#f38ba8"   /* Red    — errors, destructive     */
#define ED_COL_SUCCESS       "#a6e3a1"   /* Green  — play, confirm           */
#define ED_COL_WARNING       "#f9e2af"   /* Yellow — warnings                */
#define ED_COL_ORANGE        "#fab387"   /* Peach  — scene/mesh icons        */

/* XYZ axis — Red / Green / Blue / Mauve */
#define ED_COL_AXIS_X        "#f38ba8"   /* Red   */
#define ED_COL_AXIS_Y        "#a6e3a1"   /* Green */
#define ED_COL_AXIS_Z        "#89b4fa"   /* Blue  */
#define ED_COL_AXIS_W        "#cba6f7"   /* Mauve */

/* ------------------------------------------------------------------
   CSS bundles  (defined in ed_style.c)
   ------------------------------------------------------------------ */

extern const char *g_editor_css;
extern const char *g_launcher_css;

#endif /* ED_STYLE_H */

