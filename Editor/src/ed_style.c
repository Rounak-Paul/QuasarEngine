/*
 * ed_style.c — Quasar Editor CSS bundle definitions.
 *
 * All style values are sourced from the design-system constants in
 * ed_style.h so that colors, font sizes, and structural dimensions
 * are changed in one place and propagate everywhere automatically.
 *
 * File-browser classes (.fb-*) are shared between the editor and
 * launcher stylesheets; they are defined once via the FB_CSS macro.
 */

#include "ed_style.h"

/* ------------------------------------------------------------------
   Shared file-browser CSS  (used in both g_editor_css and g_launcher_css)
   ------------------------------------------------------------------ */

#define FB_CSS \
    ".fb-root {" \
    "  width: 100%;" \
    "  overflow: hidden;" \
    "}" \
    ".fb-title-bar {" \
    "  background: " ED_COL_SURFACE ";" \
    "  height: " ED_H_TITLE_BAR "px;" \
    "  width: 100%;" \
    "  padding-left: 14px;" \
    "  padding-right: 6px;" \
    "  align-items: center;" \
    "}" \
    ".fb-title {" \
    "  color: " ED_COL_TEXT_BRIGHT ";" \
    "  font-size: " ED_FS "px;" \
    "  font-weight: bold;" \
    "  flex-grow: 1;" \
    "}" \
    ".fb-spacer-grow {" \
    "  flex-grow: 1;" \
    "}" \
    ".fb-close-btn {" \
    "  width: " ED_H_ROW_LG "px;" \
    "  height: " ED_H_ROW_LG "px;" \
    "  background: transparent;" \
    "  color: " ED_COL_TEXT_DIM ";" \
    "  font-size: " ED_FS "px;" \
    "  corner-radius: " ED_R_BASE ";" \
    "}" \
    ".fb-nav-bar {" \
    "  background: " ED_COL_BASE ";" \
    "  height: " ED_H_NAV_BAR "px;" \
    "  width: 100%;" \
    "  flex-shrink: 0;" \
    "  padding-left: 8px;" \
    "  padding-right: 8px;" \
    "  align-items: center;" \
    "  gap: 4px;" \
    "}" \
    ".fb-nav-btn {" \
    "  width: 28px;" \
    "  height: " ED_H_CRUMB_BAR "px;" \
    "  background: " ED_COL_SURFACE ";" \
    "  color: " ED_COL_TEXT_MUTED ";" \
    "  font-size: " ED_FS "px;" \
    "  corner-radius: " ED_R_BASE ";" \
    "}" \
    ".fb-path-input {" \
    "  flex-grow: 1;" \
    "  height: " ED_H_ROW_LG "px;" \
    "  background: " ED_COL_VOID ";" \
    "  color: " ED_COL_TEXT_BRIGHT ";" \
    "  font-size: " ED_FS "px;" \
    "  padding-left: 8px;" \
    "  corner-radius: " ED_R_BASE ";" \
    "}" \
    ".fb-col-header {" \
    "  width: 100%;" \
    "  height: " ED_H_TAB_BAR "px;" \
    "  flex-shrink: 0;" \
    "  padding-left: 14px;" \
    "  padding-right: 14px;" \
    "  align-items: center;" \
    "  background: " ED_COL_SURFACE ";" \
    "}" \
    ".fb-col-name {" \
    "  color: " ED_COL_TEXT_DIM ";" \
    "  font-size: " ED_FS "px;" \
    "  flex-grow: 1;" \
    "  text-align: left;" \
    "}" \
    ".fb-col-size {" \
    "  color: " ED_COL_TEXT_DIM ";" \
    "  font-size: " ED_FS "px;" \
    "  width: 80px;" \
    "  text-align: right;" \
    "}" \
    ".fb-file-list {" \
    "  flex-grow: 1;" \
    "  overflow-y: scroll;" \
    "  padding: 4px;" \
    "  gap: 2px;" \
    "  align-items: stretch;" \
    "  background: " ED_COL_BASE ";" \
    "}" \
    ".fb-entry {" \
    "  width: 100%;" \
    "  height: " ED_H_ROW_LG "px;" \
    "  background: transparent;" \
    "  color: " ED_COL_TEXT_MUTED ";" \
    "  font-size: " ED_FS "px;" \
    "  text-align: left;" \
    "  padding-left: 8px;" \
    "  align-items: flex-start;" \
    "  corner-radius: " ED_R_SM ";" \
    "}" \
    ".fb-entry-selected {" \
    "  background: " ED_COL_OVERLAY ";" \
    "}" \
    ".fb-entry-dir {" \
    "  color: " ED_COL_PRIMARY ";" \
    "}" \
    ".fb-empty {" \
    "  color: " ED_COL_TEXT_DIM ";" \
    "  font-size: " ED_FS "px;" \
    "  padding: 14px;" \
    "}" \
    ".fb-bottom {" \
    "  background: " ED_COL_SURFACE ";" \
    "  width: 100%;" \
    "  min-height: 70px;" \
    "  flex-shrink: 0;" \
    "  padding: 6px 10px;" \
    "  gap: 6px;" \
    "}" \
    ".fb-bottom-row {" \
    "  width: 100%;" \
    "  height: " ED_H_ROW_LG "px;" \
    "  align-items: center;" \
    "  gap: 8px;" \
    "}" \
    ".fb-label {" \
    "  color: " ED_COL_TEXT_DIM ";" \
    "  font-size: " ED_FS "px;" \
    "  width: 50px;" \
    "  text-align: right;" \
    "}" \
    ".fb-selected-name {" \
    "  color: " ED_COL_TEXT_BRIGHT ";" \
    "  font-size: " ED_FS "px;" \
    "  flex-grow: 1;" \
    "  text-align: left;" \
    "}" \
    ".fb-filter-select {" \
    "  width: 200px;" \
    "}" \
    ".fb-btn {" \
    "  width: 80px;" \
    "  height: " ED_H_ROW_LG "px;" \
    "  background: " ED_COL_OVERLAY ";" \
    "  color: " ED_COL_TEXT_MUTED ";" \
    "  font-size: " ED_FS "px;" \
    "  corner-radius: " ED_R_BASE ";" \
    "}" \
    ".fb-btn-primary {" \
    "  background: " ED_COL_PRIMARY ";" \
    "  color: " ED_COL_VOID ";" \
    "}"

/* ------------------------------------------------------------------
   Editor CSS
   ------------------------------------------------------------------ */

const char *g_editor_css =

    /* Root */
    ".editor-root {"
    "  background: " ED_COL_BASE ";"
    "  gap: 0px;"
    "  overflow: hidden;"
    "}"

    /* ---- Toolbar ---- */
    ".toolbar {"
    "  background: " ED_COL_ELEVATED ";"
    "  width: 100%;"
    "  height: " ED_H_CHROME "px;"
    "  align-items: center;"
    "  padding-left: 4px;"
    "  padding-right: 4px;"
    "  gap: 2px;"
    "  border-bottom-width: 1px;"
    "  border-bottom-color: " ED_COL_SEPARATOR ";"
    "}"

    ".toolbar-icon-btn {"
    "  width: " ED_H_CHROME "px;"
    "  height: " ED_H_CHROME "px;"
    "  align-items: center;"
    "  text-align: center;"
    "  font-size: " ED_FS "px;"
    "  color: " ED_COL_TEXT_MUTED ";"
    "  corner-radius: " ED_R_SM ";"
    "  background: transparent;"
    "}"

    ".toolbar-icon-btn.active {"
    "  color: " ED_COL_PRIMARY ";"
    "}"

    ".toolbar-separator {"
    "  width: 1px;"
    "  height: 14px;"
    "  background: " ED_COL_BORDER ";"
    "}"

    /* ---- Panels ---- */
    ".panel {"
    "  background: " ED_COL_BASE ";"
    "  overflow: hidden;"
    "}"

    ".panel-content-fill {"
    "  flex-grow: 1;"
    "  width: 100%;"
    "  overflow: hidden;"
    "}"

    ".panel-viewport {"
    "  background: " ED_COL_VOID ";"
    "}"

    ".panel-bottom {"
    "  background: " ED_COL_BASE ";"
    "  overflow: hidden;"
    "}"

    ".panel-tab-bar {"
    "  width: 100%;"
    "  height: " ED_H_CHROME "px;"
    "  background: " ED_COL_ELEVATED ";"
    "  align-items: center;"
    "  font-size: " ED_FS "px;"
    "  gap: 4px;"
    "  overflow: hidden;"
    "  flex-shrink: 0;"
    "  padding-left: 0px;"
    "  padding-right: 0px;"
    "  border-bottom-width: 1px;"
    "  border-bottom-color: " ED_COL_SEPARATOR ";"
    "}"

    ".panel-tab {"
    "  color: " ED_COL_TEXT_SEC ";"
    "  font-size: " ED_FS "px;"
    "  height: " ED_H_CHROME "px;"
    "  padding-left: 8px;"
    "  padding-right: 8px;"
    "  align-items: center;"
    "  background: transparent;"
    "}"

    ".panel-tab.active {"
    "  color: " ED_COL_TEXT_VIVID ";"
    "  background: " ED_COL_OVERLAY ";"
    "  corner-radius: " ED_R_SM ";"
    "}"

    ".console-scroll {"
    "  flex-grow: 1;"
    "  width: 100%;"
    "  overflow-y: scroll;"
    "  padding: 4px;"
    "  gap: 1px;"
    "  background: " ED_COL_VOID ";"
    "  align-items: flex-start;"
    "}"

    ".console-line {"
    "  width: 100%;"
    "  color: " ED_COL_TEXT_DIM ";"
    "  font-size: 12px;"
    "  text-align: left;"
    "}"

    ".assets-root {"
    "  flex-grow: 1;"
    "  width: 100%;"
    "  background: " ED_COL_VOID ";"
    "  overflow: hidden;"
    "}"

    ".assets-toolbar {"
    "  width: 100%;"
    "  height: 26px;"
    "  align-items: center;"
    "  gap: 3px;"
    "  padding-left: 4px;"
    "  padding-right: 4px;"
    "  background: " ED_COL_ELEVATED ";"
    "  flex-shrink: 0;"
    "  border-bottom-width: 1px;"
    "  border-bottom-color: " ED_COL_SEPARATOR ";"
    "}"

    ".assets-btn {"
    "  width: 22px;"
    "  height: 22px;"
    "  background: transparent;"
    "  color: " ED_COL_TEXT_MUTED ";"
    "  font-size: " ED_FS "px;"
    "  text-align: center;"
    "  corner-radius: " ED_R_SM "px;"
    "  flex-shrink: 0;"
    "}"

    ".assets-path {"
    "  color: " ED_COL_TEXT_MUTED ";"
    "  font-size: " ED_FS "px;"
    "  text-align: left;"
    "  flex-grow: 1;"
    "  flex-shrink: 1;"
    "  overflow: hidden;"
    "}"

    ".assets-filter-select {"
    "  width: 88px;"
    "  height: 20px;"
    "  background: " ED_COL_VOID ";"
    "  color: " ED_COL_TEXT_MUTED ";"
    "  font-size: " ED_FS "px;"
    "  padding-left: 4px;"
    "  padding-right: 14px;"
    "  corner-radius: " ED_R_SM ";"
    "  flex-shrink: 0;"
    "}"

    ".assets-search-input {"
    "  width: 90px;"
    "  height: 20px;"
    "  background: " ED_COL_VOID ";"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  font-size: " ED_FS "px;"
    "  padding-left: 6px;"
    "  flex-shrink: 1;"
    "}"

    ".assets-chip {"
    "  height: 20px;"
    "  padding-left: 5px;"
    "  padding-right: 5px;"
    "  background: transparent;"
    "  color: " ED_COL_TEXT_DIM ";"
    "  font-size: " ED_FS "px;"
    "  corner-radius: 0px;"
    "  flex-shrink: 0;"
    "}"

    ".assets-scroll {"
    "  flex-grow: 1;"
    "  width: 100%;"
    "  overflow-y: scroll;"
    "  padding: 2px 0px;"
    "  gap: 0px;"
    "  align-items: stretch;"
    "  background: " ED_COL_VOID ";"
    "}"

    ".assets-tree-pane {"
    "  width: 100%;"
    "  height: 100%;"
    "  overflow-y: scroll;"
    "  padding: 4px;"
    "  background: " ED_COL_BASE ";"
    "  border-right-width: 1px;"
    "  border-right-color: " ED_COL_BORDER ";"
    "}"

    ".assets-tree {"
    "  width: 100%;"
    "  direction: column;"
    "  gap: 1px;"
    "}"

    ".assets-tree-node {"
    "  height: 18px;"
    "  color: " ED_COL_TEXT_MUTED ";"
    "  font-size: " ED_FS "px;"
    "  background: transparent;"
    "  text-align: left;"
    "  padding-left: 4px;"
    "  padding-right: 4px;"
    "  corner-radius: 0px;"
    "}"

    ".assets-tree-node-selected {"
    "  background: " ED_COL_OVERLAY ";"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "}"

    ".assets-scroll-list {"
    "  flex-direction: column;"
    "  align-items: stretch;"
    "  gap: 0px;"
    "  padding: 2px 0px;"
    "}"

    ".assets-scroll-grid {"
    "  flex-direction: row;"
    "  flex-wrap: wrap;"
    "  align-items: flex-start;"
    "  gap: 8px;"
    "  padding: 8px;"
    "}"

    ".assets-entry {"
    "  width: 100%;"
    "  height: " ED_H_ROW_TIGHT "px;"
    "  background: transparent;"
    "  color: " ED_COL_TEXT_MUTED ";"
    "  font-size: " ED_FS "px;"
    "  text-align: left;"
    "  padding-left: 6px;"
    "  padding-right: 4px;"
    "  corner-radius: 0px;"
    "}"

    ".assets-entry-list {"
    "  width: 100%;"
    "  height: 18px;"
    "  padding-left: 6px;"
    "  padding-right: 6px;"
    "  align-items: center;"
    "  gap: 8px;"
    "}"

    ".assets-entry-grid {"
    "  width: 220px;"
    "  height: 28px;"
    "  text-align: left;"
    "  padding-left: 8px;"
    "  padding-right: 8px;"
    "  corner-radius: 2px;"
    "}"

    ".assets-entry-thumb {"
    "  width: 132px;"
    "  height: 124px;"
    "  flex-direction: column;"
    "  align-items: center;"
    "  justify-content: flex-start;"
    "  gap: 6px;"
    "  padding: 6px;"
    "  corner-radius: 2px;"
    "}"

    ".assets-entry-name {"
    "  flex-grow: 1;"
    "  font-size: " ED_FS "px;"
    "  text-align: left;"
    "}"

    ".assets-entry-meta {"
    "  width: 220px;"
    "  color: " ED_COL_TEXT_DIM ";"
    "  font-size: " ED_FS "px;"
    "  text-align: right;"
    "}"

    ".assets-thumb-image {"
    "  width: 72px;"
    "  height: 72px;"
    "  corner-radius: 2px;"
    "}"

    ".assets-thumb-icon {"
    "  width: 72px;"
    "  height: 72px;"
    "  font-size: 30px;"
    "  text-align: center;"
    "}"

    ".assets-thumb-name {"
    "  width: 100%;"
    "  color: " ED_COL_TEXT_MUTED ";"
    "  font-size: " ED_FS "px;"
    "  text-align: center;"
    "  text-wrap: nowrap;"
    "}"

    ".assets-entry-selected {"
    "  background: " ED_COL_OVERLAY ";"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "}"

    ".assets-empty {"
    "  color: " ED_COL_TEXT_DIM ";"
    "  font-size: " ED_FS "px;"
    "  padding: 8px;"
    "  width: 100%;"
    "  text-align: left;"
    "}"

    ".status-bar {"
    "  width: 100%;"
    "  height: 100%;"
    "  background: " ED_COL_ELEVATED ";"
    "  align-items: center;"
    "  padding-left: 10px;"
    "}"

    ".status-text {"
    "  color: " ED_COL_TEXT_DIM ";"
    "  font-size: " ED_FS "px;"
    "}"

    /* ---- File browser ---- */
    FB_CSS

    /* ---- Hierarchy panel ---- */
    ".hierarchy-root {"
    "  flex-grow: 1;"
    "  width: 100%;"
    "  overflow: hidden;"
    "}"

    ".hierarchy-toolbar {"
    "  background: " ED_COL_ELEVATED ";"
    "  width: 100%;"
    "  height: " ED_H_CHROME "px;"
    "  align-items: center;"
    "  padding-left: 4px;"
    "  padding-right: 4px;"
    "  gap: 2px;"
    "  border-bottom-width: 1px;"
    "  border-bottom-color: " ED_COL_SEPARATOR ";"
    "}"

    ".hierarchy-toolbar-spacer { flex-grow: 1; }"

    ".hierarchy-tree {"
    "  overflow-y: scroll;"
    "  padding-left: 6px;"
    "  padding-right: 4px;"
    "  padding-top: 2px;"
    "  gap: 0px;"
    "  flex-grow: 1;"
    "  align-items: flex-start;"
    "}"

    ".hierarchy-scene {"
    "  color: " ED_COL_PRIMARY ";"
    "  font-size: " ED_FS "px;"
    "  height: " ED_H_ROW_TIGHT "px;"
    "}"

    ".hierarchy-entity {"
    "  color: " ED_COL_TEXT_MUTED ";"
    "  font-size: " ED_FS "px;"
    "  height: " ED_H_ROW_TIGHT "px;"
    "}"

    ".hierarchy-proto-inner {"
    "  color: " ED_COL_PRIMARY ";"
    "  font-size: " ED_FS "px;"
    "  font-style: italic;"
    "  height: " ED_H_ROW_TIGHT "px;"
    "}"

    ".hierarchy-component {"
    "  color: " ED_COL_TEXT_DIM ";"
    "  font-size: " ED_FS "px;"
    "  height: " ED_H_ROW_TIGHT "px;"
    "}"

    ".hierarchy-add-btn {"
    "  width: 100%;"
    "  height: " ED_H_CRUMB_BAR "px;"
    "  background: transparent;"
    "  color: " ED_COL_TEXT_DIM ";"
    "  font-size: " ED_FS "px;"
    "}"

    ".hierarchy-selected {"
    "  background: " ED_COL_OVERLAY ";"
    "}"

    ".hierarchy-rename-input {"
    "  flex-grow: 1;"
    "  height: " ED_H_ROW_TIGHT "px;"
    "  font-size: " ED_FS "px;"
    "  padding-left: 4px;"
    "  padding-right: 4px;"
    "  background: " ED_COL_VOID ";"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  border-radius: 2px;"
    "}"

    /* ---- Inspector panel ---- */
    ".inspector-scroll {"
    "  overflow-y: scroll;"
    "  padding: 4px 0px;"
    "  gap: 1px;"
    "  flex-grow: 1;"
    "  width: 100%;"
    "  align-items: flex-start;"
    "}"

    ".inspector-placeholder {"
    "  color: " ED_COL_TEXT_DIM ";"
    "  font-size: " ED_FS "px;"
    "  padding: 14px 0px 0px 8px;"
    "  width: 100%;"
    "  text-align: left;"
    "}"

    ".inspector-header {"
    "  width: 100%;"
    "  gap: 4px;"
    "  padding: 6px 8px;"
    "  align-items: flex-start;"
    "}"

    ".inspector-entity-input {"
    "  width: 100%;"
    "  height: " ED_H_TAB_BAR "px;"
    "  background: " ED_COL_VOID ";"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  font-size: " ED_FS "px;"
    "  font-weight: bold;"
    "  padding-left: 6px;"
    "  corner-radius: " ED_R_SM ";"
    "}"

    ".inspector-meta-row {"
    "  width: 100%;"
    "  height: " ED_H_INPUT_SMALL "px;"
    "  gap: 4px;"
    "  align-items: center;"
    "  padding-left: 0px;"
    "}"

    ".inspector-tag-input {"
    "  height: " ED_H_INPUT_SMALL "px;"
    "  flex-grow: 1;"
    "  background: " ED_COL_VOID ";"
    "  color: " ED_COL_TEXT_MUTED ";"
    "  font-size: " ED_FS "px;"
    "  padding-left: 4px;"
    "  corner-radius: " ED_R_SM ";"
    "}"

    ".inspector-section-header {"
    "  background: " ED_COL_SURFACE ";"
    "  width: 100%;"
    "  height: " ED_H_TAB_BAR "px;"
    "  padding: 0px 8px;"
    "  margin-top: 4px;"
    "  align-items: center;"
    "  gap: 4px;"
    "  border-left-width: 2px;"
    "  border-left-color: " ED_COL_PRIMARY ";"
    "}"

    ".inspector-section-label {"
    "  font-size: " ED_FS "px;"
    "  font-weight: bold;"
    "  color: " ED_COL_TEXT_MEDIUM ";"
    "  flex-grow: 1;"
    "  text-align: left;"
    "}"

    ".inspector-field-row {"
    "  width: 100%;"
    "  padding: 2px 6px;"
    "  gap: 2px;"
    "  align-items: flex-start;"
    "}"

    ".inspector-field-name {"
    "  color: " ED_COL_TEXT_DIM ";"
    "  font-size: " ED_FS "px;"
    "  text-align: left;"
    "  width: 100%;"
    "}"

    ".inspector-field-overridden {"
    "  color: " ED_COL_WARNING ";"
    "}"

    ".inspector-vec-row {"
    "  width: 100%;"
    "  gap: 0px;"
    "  align-items: center;"
    "  flex-wrap: wrap;"
    "}"

    ".inspector-axis-x { color: " ED_COL_AXIS_X "; font-size: " ED_FS "px; font-weight: bold; width: 10px; }"
    ".inspector-axis-y { color: " ED_COL_AXIS_Y "; font-size: " ED_FS "px; font-weight: bold; width: 10px; }"
    ".inspector-axis-z { color: " ED_COL_AXIS_Z "; font-size: " ED_FS "px; font-weight: bold; width: 10px; }"
    ".inspector-axis-w { color: " ED_COL_AXIS_W "; font-size: " ED_FS "px; font-weight: bold; width: 10px; }"

    ".inspector-axis-group {"
    "  width: 68px;"
    "  padding-left: 4px;"
    "  gap: 2px;"
    "  align-items: center;"
    "}"

    ".inspector-vec-input {"
    "  width: 50px;"
    "  height: " ED_H_INPUT_MINI "px;"
    "  background: " ED_COL_VOID ";"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  font-size: " ED_FS "px;"
    "  padding-left: 4px;"
    "  corner-radius: " ED_R_SM ";"
    "}"

    ".inspector-scalar-input {"
    "  width: 100%;"
    "  height: " ED_H_INPUT_MINI "px;"
    "  background: " ED_COL_VOID ";"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  font-size: " ED_FS "px;"
    "  padding-left: 4px;"
    "  corner-radius: " ED_R_SM ";"
    "}"

    ".inspector-field-value {"
    "  color: " ED_COL_TEXT_MUTED ";"
    "  font-size: " ED_FS "px;"
    "}"

    ".inspector-id-label {"
    "  color: " ED_COL_TEXT_DIM ";"
    "  font-size: " ED_FS "px;"
    "  width: 100%;"
    "  text-align: left;"
    "  padding-left: 2px;"
    "}"

    ".inspector-tag-icon {"
    "  color: " ED_COL_SUCCESS ";"
    "  font-size: " ED_FS "px;"
    "  width: 16px;"
    "  flex-shrink: 0;"
    "}"

    /* ---- System / Memory panel ---- */

    ".sys-scroll {"
    "  overflow-y: scroll;"
    "  flex-grow: 1;"
    "  width: 100%;"
    "  padding: 6px 4px;"
    "  gap: 4px;"
    "  align-items: flex-start;"
    "}"

    /* Overview card */
    ".sys-card {"
    "  width: 100%;"
    "  background: " ED_COL_ELEVATED ";"
    "  corner-radius: " ED_R_BASE ";"
    "  padding: 6px 8px;"
    "  gap: 4px;"
    "}"

    ".sys-card-header {"
    "  width: 100%;"
    "  align-items: center;"
    "  gap: 4px;"
    "}"

    ".sys-section-title {"
    "  font-size: " ED_FS "px;"
    "  font-weight: bold;"
    "  color: " ED_COL_TEXT_MUTED ";"
    "  letter-spacing: 1px;"
    "}"

    ".sys-alloc-badge {"
    "  font-size: " ED_FS "px;"
    "  color: " ED_COL_TEXT_MUTED ";"
    "  background: " ED_COL_SURFACE ";"
    "  corner-radius: " ED_R_SM ";"
    "  padding: 0px 5px;"
    "}"

    ".sys-total-bar {"
    "  width: 100%;"
    "  corner-radius: " ED_R_SM ";"
    "}"

    /* Column header row */
    ".sys-row-header {"
    "  width: 100%;"
    "  padding: 3px 4px 1px 4px;"
    "  align-items: center;"
    "}"

    ".sys-col-cat {"
    "  font-size: " ED_FS "px;"
    "  color: " ED_COL_TEXT_MUTED ";"
    "  flex-grow: 1;"
    "  text-align: left;"
    "}"

    ".sys-col-bytes {"
    "  font-size: " ED_FS "px;"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  width: 60px;"
    "  text-align: right;"
    "}"

    ".sys-col-bytes-wide {"
    "  font-size: " ED_FS "px;"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  flex-grow: 1;"
    "  text-align: right;"
    "}"

    ".sys-col-cnt {"
    "  font-size: " ED_FS "px;"
    "  color: " ED_COL_TEXT_DIM ";"
    "  width: 44px;"
    "  text-align: right;"
    "}"

    /* Tag data rows */
    ".sys-row {"
    "  width: 100%;"
    "  padding: 2px 4px 1px 4px;"
    "  gap: 1px;"
    "}"

    ".sys-row-alt {"
    "  background: " ED_COL_BASE_ALT ";"
    "}"

    ".sys-row-top {"
    "  width: 100%;"
    "  align-items: center;"
    "  gap: 4px;"
    "}"

    ".sys-mini-bar {"
    "  width: 100%;"
    "  corner-radius: " ED_R_SM ";"
    "}"

    /* ---- Plugin Manager window ---- */
    ".pm-root {"
    "  width: 100%;"
    "  height: 100%;"
    "  background: " ED_COL_BASE ";"
    "}"

    ".pm-spacer { flex-grow: 1; }"

    ".pm-header {"
    "  width: 100%;"
    "  height: " ED_H_PANEL_HDR "px;"
    "  background: " ED_COL_ELEVATED_ALT ";"
    "  align-items: center;"
    "  padding: 0px 14px;"
    "  gap: 0px;"
    "}"

    ".pm-hdr {"
    "  color: " ED_COL_TEXT_SEC ";"
    "  font-size: " ED_FS "px;"
    "  font-weight: bold;"
    "}"

    ".pm-col-status  { width: 64px; }"
    ".pm-col-name    { width: 140px; }"
    ".pm-col-version { width: 60px; }"
    ".pm-col-author  { width: 100px; }"
    ".pm-col-actions { width: 80px; }"

    ".pm-separator {"
    "  width: 100%;"
    "  height: 1px;"
    "  background: " ED_COL_SEPARATOR ";"
    "}"

    ".pm-body {"
    "  width: 100%;"
    "  flex-grow: 1;"
    "  overflow-y: scroll;"
    "  gap: 0px;"
    "}"

    ".pm-empty {"
    "  color: " ED_COL_TEXT_DIM ";"
    "  font-size: " ED_FS "px;"
    "  text-align: center;"
    "  padding: 32px;"
    "}"

    ".pm-row {"
    "  width: 100%;"
    "  height: " ED_H_ROW_FORM "px;"
    "  align-items: center;"
    "  padding: 0px 14px;"
    "  gap: 0px;"
    "}"

    ".pm-row-alt {"
    "  background: " ED_COL_BASE_ALT ";"
    "}"

    ".pm-status {"
    "  width: 64px;"
    "  font-size: " ED_FS "px;"
    "  font-weight: bold;"
    "}"

    ".pm-status-active   { color: " ED_COL_SUCCESS "; }"
    ".pm-status-loading  { color: " ED_COL_WARNING "; }"
    ".pm-status-disabled { color: " ED_COL_TEXT_DIM "; }"

    ".pm-cell-name {"
    "  width: 140px;"
    "  color: " ED_COL_TEXT_VIVID ";"
    "  font-size: " ED_FS "px;"
    "}"

    ".pm-cell-version {"
    "  width: 60px;"
    "  color: " ED_COL_TEXT_SEC ";"
    "  font-size: " ED_FS "px;"
    "}"

    ".pm-cell-author {"
    "  width: 100px;"
    "  color: " ED_COL_TEXT_SEC ";"
    "  font-size: " ED_FS "px;"
    "}"

    ".pm-cell-actions {"
    "  width: 80px;"
    "  align-items: center;"
    "  gap: 6px;"
    "}"

    ".pm-reload-btn {"
    "  background: transparent;"
    "  color: " ED_COL_PRIMARY ";"
    "  font-size: " ED_FS "px;"
    "  width: " ED_H_CHROME "px;"
    "  height: " ED_H_CHROME "px;"
    "  align-items: center;"
    "  text-align: center;"
    "}"

    ".pm-toggle {"
    "  width: 30px;"
    "  height: " ED_H_ROW_TIGHT "px;"
    "}"

    /* ---- Renderer Settings modal ---- */
    ".renderer-settings-modal {"
    "  width: 340px;"
    "  height: auto;"
    "  background: " ED_COL_ELEVATED ";"
    "  corner-radius: " ED_R_LG ";"
    "  padding: 0px;"
    "  gap: 0px;"
    "}"

    ".renderer-settings-panel {"
    "  width: 100%;"
    "  padding: 0px;"
    "}"

    ".renderer-setting-row {"
    "  width: 100%;"
    "  padding: 8px 12px 4px 12px;"
    "  gap: 4px;"
    "}"

    ".renderer-setting-label {"
    "  color: " ED_COL_TEXT_MUTED ";"
    "  font-size: " ED_FS "px;"
    "}"

    ".renderer-section-label {"
    "  color: " ED_COL_TEXT_MEDIUM ";"
    "  font-size: " ED_FS "px;"
    "  padding: 8px 12px 2px 12px;"
    "}"

    /* ---- Renderer Stats modal ---- */
    ".renderer-stats-modal {"
    "  width: 260px;"
    "  height: auto;"
    "  background: " ED_COL_ELEVATED ";"
    "  corner-radius: " ED_R_LG ";"
    "  padding: 0px;"
    "  gap: 0px;"
    "}"

    ".renderer-stats-panel {"
    "  width: 100%;"
    "  padding: 0px;"
    "}"

    ".renderer-stat-row {"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  font-size: " ED_FS "px;"
    "  padding: 4px 12px;"
    "  font-family: monospace;"
    "}"

    /* ---- Breadcrumb navigation bar ---- */
    ".breadcrumb-bar {"
    "  background: " ED_COL_ELEVATED ";"
    "  width: 100%;"
    "  height: " ED_H_CRUMB_BAR "px;"
    "  align-items: center;"
    "  padding-left: 8px;"
    "  padding-right: 8px;"
    "  gap: 6px;"
    "}"

    ".breadcrumb-back {"
    "  height: " ED_H_CHROME "px;"
    "  background: transparent;"
    "  color: " ED_COL_PRIMARY ";"
    "  font-size: " ED_FS "px;"
    "  corner-radius: " ED_R_SM ";"
    "  padding-left: 6px;"
    "  padding-right: 6px;"
    "}"

    ".breadcrumb-text {"
    "  color: " ED_COL_TEXT_MUTED ";"
    "  font-size: " ED_FS "px;"
    "}"

    ".breadcrumb-separator {"
    "  color: " ED_COL_TEXT_DIM ";"
    "  font-size: " ED_FS "px;"
    "}"

    ".breadcrumb-active {"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  font-size: " ED_FS "px;"
    "  font-weight: bold;"
    "}"

    /* ---- Inspector edit button ---- */
    ".inspector-edit-btn {"
    "  height: " ED_H_INPUT_SMALL "px;"
    "  background: " ED_COL_OVERLAY ";"
    "  color: " ED_COL_PRIMARY ";"
    "  font-size: " ED_FS "px;"
    "  corner-radius: " ED_R_SM ";"
    "  padding-left: 8px;"
    "  padding-right: 8px;"
    "}"

    ".inspector-proto-edit-row {"
    "  width: 100%;"
    "  height: " ED_H_ROW_LG "px;"
    "  background: " ED_COL_OVERLAY ";"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  font-size: " ED_FS "px;"
    "  font-weight: bold;"
    "  text-align: center;"
    "  corner-radius: " ED_R_BASE ";"
    "  margin-top: 4px;"
    "  margin-bottom: 4px;"
    "}"
    ".inspector-proto-edit-row:hover {"
    "  background: " ED_COL_BORDER ";"
    "  color: " ED_COL_PRIMARY ";"
    "}"

    ".inspector-remove-btn {"
    "  width: " ED_H_INPUT_SMALL "px;"
    "  height: " ED_H_INPUT_SMALL "px;"
    "  background: transparent;"
    "  color: " ED_COL_TEXT_SEC ";"
    "  font-size: " ED_FS "px;"
    "  text-align: center;"
    "  corner-radius: " ED_R_SM ";"
    "  padding: 0px;"
    "}"

    ".inspector-add-comp-row {"
    "  width: 100%;"
    "  padding-top: 10px;"
    "  padding-bottom: 10px;"
    "  padding-left: 8px;"
    "  padding-right: 8px;"
    "  align-items: stretch;"
    "}"

    ".inspector-add-comp-select {"
    "  width: 100%;"
    "  height: " ED_H_TAB_BAR "px;"
    "  background: " ED_COL_OVERLAY ";"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  font-size: " ED_FS "px;"
    "  font-weight: bold;"
    "  text-align: center;"
    "  padding-left: 12px;"
    "  padding-right: 20px;"
    "  corner-radius: " ED_R_BASE ";"
    "}"

    ".inspector-select {"
    "  width: 100%;"
    "  height: " ED_H_INPUT_SMALL "px;"
    "  background: " ED_COL_VOID ";"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  font-size: " ED_FS "px;"
    "  padding-left: 6px;"
    "  padding-right: 18px;"
    "  corner-radius: " ED_R_SM ";"
    "}"

    /* ---- Material editor (inline in MeshComp inspector section) ---- */

    ".mat-subsection-label {"
    "  color: " ED_COL_TEXT_MEDIUM ";"
    "  font-size: " ED_FS "px;"
    "  font-weight: bold;"
    "  width: 100%;"
    "  padding: 4px 8px 2px 8px;"
    "}"

    ".mat-mesh-path-label {"
    "  color: " ED_COL_TEXT_MUTED ";"
    "  font-size: " ED_FS "px;"
    "  width: 100%;"
    "  text-align: left;"
    "  padding-left: 2px;"
    "}"

    ".mat-tex-select {"
    "  width: 100%;"
    "  height: " ED_H_INPUT_SMALL "px;"
    "  background: " ED_COL_VOID ";"
    "  color: " ED_COL_TEXT_MUTED ";"
    "  font-size: " ED_FS "px;"
    "  padding-left: 6px;"
    "  padding-right: 18px;"
    "  corner-radius: " ED_R_SM ";"
    "}"

    /* ---- Import Asset dialog ---- */
    ".import-root {"
    "  background: " ED_COL_ELEVATED ";"
    "  width: 100%;"
    "  height: 100%;"
    "}"

    ".import-body {"
    "  flex-grow: 1;"
    "  width: 100%;"
    "  padding: 12px;"
    "  gap: 10px;"
    "}"

    ".import-source {"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  font-size: " ED_FS "px;"
    "  font-weight: bold;"
    "}"

    ".import-cols {"
    "  flex-grow: 1;"
    "  width: 100%;"
    "  gap: 10px;"
    "  align-items: flex-start;"
    "}"

    ".import-col {"
    "  flex-grow: 1;"
    "  background: " ED_COL_SURFACE ";"
    "  padding: 8px;"
    "  corner-radius: " ED_R_SM ";"
    "  gap: 4px;"
    "  overflow-y: scroll;"
    "  align-items: flex-start;"
    "}"

    ".import-col-title {"
    "  color: " ED_COL_PRIMARY ";"
    "  font-size: " ED_FS "px;"
    "  font-weight: bold;"
    "  padding-bottom: 4px;"
    "}"

    ".import-row {"
    "  width: 100%;"
    "  align-items: center;"
    "  gap: 8px;"
    "}"

    ".import-item {"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  font-size: " ED_FS "px;"
    "  padding-top: 1px;"
    "  padding-bottom: 1px;"
    "}"

    ".import-flags {"
    "  width: 100%;"
    "  gap: 18px;"
    "  align-items: center;"
    "  padding-top: 4px;"
    "  padding-bottom: 4px;"
    "}"

    ".import-cb {"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  font-size: " ED_FS "px;"
    "  flex-grow: 1;"
    "}"

    ".import-cb-flag {"
    "  color: " ED_COL_TEXT_MUTED ";"
    "  font-size: " ED_FS "px;"
    "}"

    ".import-footer {"
    "  width: 100%;"
    "  height: " ED_H_TITLE_BAR "px;"
    "  align-items: center;"
    "  justify-content: flex-end;"
    "  gap: 8px;"
    "  padding-top: 6px;"
    "}"

    ".import-btn {"
    "  height: " ED_H_CRUMB_BAR "px;"
    "  background: " ED_COL_OVERLAY ";"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  font-size: " ED_FS "px;"
    "  corner-radius: " ED_R_SM ";"
    "  padding-left: 14px;"
    "  padding-right: 14px;"
    "}"

    ".import-btn-primary {"
    "  background: " ED_COL_PRIMARY ";"
    "  color: " ED_COL_VOID ";"
    "  font-weight: bold;"
    "}"

    ".import-progress {"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  font-size: " ED_FS "px;"
    "  padding: 24px;"
    "}"

    /* ---- Tooltips ---- */
    "tooltip {"
    "  font-size: 10px;"
    "}"
;

/* ------------------------------------------------------------------
   Launcher CSS
   ------------------------------------------------------------------ */

const char *g_launcher_css =

    /* Root */
    ".launcher-root {"
    "  background: " ED_COL_BASE ";"
    "  gap: 0px;"
    "  overflow: hidden;"
    "}"

    /* ---- Left sidebar ---- */
    ".launcher-sidebar {"
    "  width: 128px;"
    "  background: " ED_COL_ELEVATED ";"
    "  padding-top: 0px;"
    "  gap: 0px;"
    "  overflow: hidden;"
    "}"

    ".launcher-sidebar-header {"
    "  width: 100%;"
    "  padding: 24px 18px 18px 18px;"
    "  gap: 4px;"
    "  background: " ED_COL_VOID ";"
    "}"

    ".launcher-title {"
    "  color: " ED_COL_PRIMARY ";"
    "  font-size: " ED_FS "px;"
    "  font-weight: bold;"
    "  text-align: left;"
    "}"

    ".launcher-version {"
    "  color: " ED_COL_TEXT_DIM ";"
    "  font-size: " ED_FS "px;"
    "  text-align: left;"
    "}"

    ".launcher-tab {"
    "  width: 100%;"
    "  height: " ED_H_ITEM_MD "px;"
    "  background: transparent;"
    "  font-size: " ED_FS "px;"
    "  align-items: center;"
    "  justify-content: flex-start;"
    "  padding-left: 18px;"
    "  corner-radius: 0px;"
    "}"

    ".launcher-tab-label {"
    "  color: " ED_COL_TEXT_DIM ";"
    "  font-size: " ED_FS "px;"
    "  text-align: left;"
    "}"

    ".launcher-tab-label-active {"
    "  color: " ED_COL_PRIMARY ";"
    "}"

    ".launcher-tab-active {"
    "  background: " ED_COL_OVERLAY ";"
    "}"

    /* ---- Right content area ---- */
    ".launcher-content {"
    "  flex-grow: 1;"
    "  background: " ED_COL_BASE ";"
    "  overflow: hidden;"
    "}"

    ".launcher-page {"
    "  flex-grow: 1;"
    "  width: 100%;"
    "  overflow: hidden;"
    "}"

    ".launcher-page-body {"
    "  flex-grow: 1;"
    "  width: 100%;"
    "  overflow: hidden;"
    "  padding: 12px;"
    "  gap: 8px;"
    "}"

    /* ---- Projects page ---- */
    ".launcher-page-header {"
    "  width: 100%;"
    "  height: " ED_H_PAGE_HDR "px;"
    "  padding: 0px 16px;"
    "  align-items: center;"
    "  gap: 10px;"
    "  background: " ED_COL_ELEVATED ";"
    "}"

    ".launcher-page-title {"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  font-size: " ED_FS "px;"
    "  font-weight: bold;"
    "  flex-grow: 1;"
    "  text-align: left;"
    "}"

    ".launcher-list {"
    "  flex-grow: 1;"
    "  width: 100%;"
    "  overflow-y: scroll;"
    "  padding: 0px;"
    "  gap: 4px;"
    "  align-items: flex-start;"
    "}"

    ".launcher-entry {"
    "  width: 100%;"
    "  height: " ED_H_CARD "px;"
    "  background: " ED_COL_ELEVATED ";"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  font-size: " ED_FS "px;"
    "  text-align: left;"
    "  padding-left: 14px;"
    "  corner-radius: " ED_R_BASE ";"
    "}"

    ".launcher-entry-row {"
    "  width: 100%;"
    "  height: " ED_H_CARD "px;"
    "  gap: 8px;"
    "  align-items: center;"
    "}"

    ".launcher-entry-open {"
    "  flex-grow: 1;"
    "  height: " ED_H_CARD "px;"
    "  background: " ED_COL_ELEVATED ";"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  font-size: " ED_FS "px;"
    "  text-align: left;"
    "  padding-left: 14px;"
    "  corner-radius: " ED_R_BASE ";"
    "}"

    ".launcher-entry-action {"
    "  width: 30px;"
    "  height: 30px;"
    "  background: " ED_COL_SURFACE ";"
    "  color: " ED_COL_TEXT_DIM ";"
    "  font-size: 13px;"
    "  corner-radius: " ED_R_BASE ";"
    "}"

    ".launcher-entry-name {"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  font-size: " ED_FS "px;"
    "  text-align: left;"
    "}"

    ".launcher-entry-path {"
    "  color: " ED_COL_TEXT_DIM ";"
    "  font-size: " ED_FS "px;"
    "  text-align: left;"
    "}"

    ".launcher-empty {"
    "  color: " ED_COL_TEXT_DIM ";"
    "  font-size: " ED_FS "px;"
    "  padding: 20px;"
    "}"

    /* ---- New Project form ---- */
    ".launcher-form {"
    "  flex-grow: 1;"
    "  width: 100%;"
    "  overflow-y: scroll;"
    "  padding: 0px;"
    "  gap: 14px;"
    "  align-items: flex-start;"
    "}"

    ".launcher-form-row {"
    "  width: 100%;"
    "  height: " ED_H_FORM_ROW "px;"
    "  align-items: center;"
    "  gap: 10px;"
    "}"

    ".launcher-form-label {"
    "  color: " ED_COL_TEXT_MUTED ";"
    "  font-size: " ED_FS "px;"
    "  width: 60px;"
    "  text-align: right;"
    "}"

    ".launcher-input {"
    "  flex-grow: 1;"
    "  height: " ED_H_ROW_FORM "px;"
    "  background: " ED_COL_VOID ";"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  font-size: " ED_FS "px;"
    "  padding-left: 10px;"
    "  corner-radius: " ED_R_BASE ";"
    "}"

    ".launcher-path-display {"
    "  flex-grow: 1;"
    "  color: " ED_COL_TEXT_DIM ";"
    "  font-size: " ED_FS "px;"
    "  text-align: left;"
    "}"

    ".launcher-btn {"
    "  width: 90px;"
    "  height: " ED_H_ROW_FORM "px;"
    "  background: " ED_COL_SURFACE ";"
    "  color: " ED_COL_TEXT_MUTED ";"
    "  font-size: " ED_FS "px;"
    "  corner-radius: " ED_R_BASE ";"
    "}"

    ".launcher-btn-sm {"
    "  width: 70px;"
    "  height: " ED_H_ROW_FORM "px;"
    "  background: " ED_COL_SURFACE ";"
    "  color: " ED_COL_TEXT_MUTED ";"
    "  font-size: " ED_FS "px;"
    "  corner-radius: " ED_R_BASE ";"
    "}"

    ".launcher-btn-primary {"
    "  background: " ED_COL_PRIMARY ";"
    "  color: " ED_COL_VOID ";"
    "}"

    ".launcher-btn-danger {"
    "  background: #2d1420;"
    "  color: " ED_COL_DANGER ";"
    "}"

    ".launcher-confirm-modal {"
    "  width: 100%;"
    "  height: 100%;"
    "  justify-content: center;"
    "  align-items: center;"
    "}"

    ".launcher-confirm-card {"
    "  width: 360px;"
    "  background: " ED_COL_ELEVATED ";"
    "  border-color: " ED_COL_BORDER ";"
    "  border-width: 1px;"
    "  corner-radius: " ED_R_LG ";"
    "  padding: 14px;"
    "  gap: 12px;"
    "}"

    ".launcher-confirm-title {"
    "  color: " ED_COL_TEXT_BRIGHT ";"
    "  font-size: " ED_FS "px;"
    "  font-weight: bold;"
    "  text-align: left;"
    "}"

    ".launcher-confirm-text {"
    "  color: " ED_COL_TEXT_DIM ";"
    "  font-size: " ED_FS "px;"
    "  text-align: left;"
    "}"

    ".launcher-confirm-actions {"
    "  width: 100%;"
    "  justify-content: flex-end;"
    "  gap: 8px;"
    "}"

    /* ---- File browser ---- */
    FB_CSS

    /* ---- Tooltips ---- */
    "tooltip {"
    "  font-size: 10px;"
    "}"
;
