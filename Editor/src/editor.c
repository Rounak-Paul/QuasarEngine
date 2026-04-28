#include "editor.h"
#include "ed_camera.h"
#include "ed_gizmo.h"
#include "ed_pick.h"
#include "qs_math.h"
#include "ui/ed_menu_bar.h"
#include "ui/ed_toolbar.h"
#include "ui/ed_layout.h"
#include "ui/ed_status_bar.h"
#include "ui/ed_inspector.h"
#include "ui/ed_hierarchy.h"
#include "ui/ed_plugin_manager.h"

#include "ui/ed_file_browser.h"
#include "ui/ed_import_dialog.h"

#include "quasar.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EDITOR_PROTO_STACK_DEPTH 8

struct Editor {
    Qs_Engine     *engine;
    Qs_Project    *project;
    Qs_Renderer   *scene_renderer;
    Ca_Viewport   *scene_viewport;
    Qs_Entity      selected_entity;
    EditorCamera   cam;

    /* ---- Prototype editor mode ---- */
    EditorMode    mode;
    char          proto_path[1024];   ///< Path to the .qproto being edited (top of stack).
    /* When entering prototype mode, the current active scene + camera are
       pushed.  Closing the prototype pops and restores. */
    Qs_Scene     *proto_stack_scene[EDITOR_PROTO_STACK_DEPTH];
    EditorCamera  proto_stack_cam[EDITOR_PROTO_STACK_DEPTH];
    uint32_t      proto_stack_depth;

    /* Editor-only preview light injected into the prototype scene so the
       inner geometry is visible while editing.  Destroyed before save so
       it is never baked into the .qproto file. */
    Qs_Entity     proto_preview_light;

    /* ---- Prototype-instance override editing context ----
       When the user expands a prototype instance in the hierarchy and
       selects one of its inner entities, these fields point to that
       inner-scene context so the inspector can route field edits
       through the PrototypeComp override system rather than mutating
       the source .qproto file. */
    Qs_Entity     proto_owner;          ///< QS_ENTITY_INVALID when inactive.
    Qs_Scene     *proto_inner_scene;
};

/* ---- Editor CSS theme (Quasar Dark) ---- */
/*
 * Color palette — professional dark game engine aesthetic
 *
 * Background hierarchy (darkest → lightest):
 *   #0d0d0f  void        (deepest inset, inputs, viewport bg)
 *   #111114  base        (primary panel background)
 *   #16161a  elevated    (panel chrome, toolbars)
 *   #1c1c22  surface     (tab bars, headers, section dividers)
 *   #242430  overlay     (hover states, selected items)
 *   #2e2e3e  border      (dividers, input borders)
 *
 * Accent colors:
 *   #6e8aff  primary     (active tabs, primary buttons — blue-violet)
 *   #c8d0ff  text-bright (labels, interactive text)
 *   #8890b0  text-muted  (secondary labels, hints)
 *   #4a4e6a  text-dim    (disabled, placeholder)
 *   #ff6b6b  danger      (errors, destructive)
 *   #6bffb8  success     (play, confirm)
 *   #ffd166  warning     (warnings)
 *   #ff8c42  orange      (scene objects, mesh icons)
 *
 * Axis colors (XYZ):
 *   #ff5370  x-axis      (red)
 *   #80ff80  y-axis      (green)
 *   #5b9cff  z-axis      (blue)
 *   #bf80ff  w-axis      (purple)
 */

static const char *g_editor_css =

    /* Root */
    ".editor-root {"
    "  background: #111114;"
    "  gap: 0px;"
    "  overflow: hidden;"
    "}"

    /* ---- Toolbar ---- */
    ".toolbar {"
    "  background: #16161a;"
    "  width: 100%;"
    "  height: 20px;"
    "  align-items: center;"
    "  padding-left: 4px;"
    "  padding-right: 4px;"
    "  gap: 2px;"
    "}"

    ".toolbar-icon-btn {"
    "  width: 20px;"
    "  height: 20px;"
    "  align-items: center;"
    "  text-align: center;"
    "  font-size: 14px;"
    "  color: #8890b0;"
    "  corner-radius: 3px;"
    "  background: transparent;"
    "}"

    ".toolbar-icon-btn.active {"
    "  color: #6e8aff;"
    "}"

    ".toolbar-separator {"
    "  width: 1px;"
    "  height: 14px;"
    "  background: #2e2e3e;"
    "}"

    /* ---- Panels ---- */
    ".panel {"
    "  background: #111114;"
    "  overflow: hidden;"
    "}"

    ".panel-viewport {"
    "  background: #0d0d0f;"
    "}"

    ".panel-bottom {"
    "  background: #111114;"
    "}"

    /* ---- Console ---- */
    ".console-scroll {"
    "  overflow-y: scroll;"
    "  padding: 4px;"
    "  flex-grow: 1;"
    "  align-items: flex-start;"
    "}"

    ".console-line {"
    "  font-size: 12px;"
    "  height: 16px;"
    "  width: 100%;"
    "  text-align: left;"
    "  color: #8890b0;"
    "}"

    /* ---- Panel tab bars ---- */
    ".panel-tab-bar {"
    "  background: #1c1c22;"
    "  height: 22px;"
    "  width: 100%;"
    "  padding-left: 4px;"
    "  gap: 2px;"
    "  font-size: 12px;"
    "}"

    ".panel-tab {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  height: 22px;"
    "  padding-left: 8px;"
    "  padding-right: 8px;"
    "}"

    ".active {"
    "  color: #6e8aff;"
    "}"

    /* ---- Status bar ---- */
    ".status-bar {"
    "  background: #0d0d0f;"
    "  width: 100%;"
    "  height: 20px;"
    "  align-items: center;"
    "  padding-left: 10px;"
    "}"

    ".status-text {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "}"

    /* ---- File browser ---- */
    ".fb-root {"
    "  width: 100%;"
    "  overflow: hidden;"
    "}"

    ".fb-title-bar {"
    "  background: #1c1c22;"
    "  height: 36px;"
    "  width: 100%;"
    "  padding-left: 14px;"
    "  padding-right: 6px;"
    "  align-items: center;"
    "}"

    ".fb-title {"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "  flex-grow: 1;"
    "}"

    ".fb-spacer-grow {"
    "  flex-grow: 1;"
    "}"

    ".fb-close-btn {"
    "  width: 26px;"
    "  height: 26px;"
    "  background: transparent;"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  corner-radius: 4px;"
    "}"

    ".fb-nav-bar {"
    "  background: #111114;"
    "  height: 38px;"
    "  width: 100%;"
    "  flex-shrink: 0;"
    "  padding-left: 8px;"
    "  padding-right: 8px;"
    "  align-items: center;"
    "  gap: 4px;"
    "}"

    ".fb-nav-btn {"
    "  width: 28px;"
    "  height: 24px;"
    "  background: #1c1c22;"
    "  color: #8890b0;"
    "  font-size: 12px;"
    "  corner-radius: 4px;"
    "}"

    ".fb-path-input {"
    "  flex-grow: 1;"
    "  height: 26px;"
    "  background: #0d0d0f;"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  padding-left: 8px;"
    "  corner-radius: 4px;"
    "}"

    ".fb-col-header {"
    "  width: 100%;"
    "  height: 22px;"
    "  flex-shrink: 0;"
    "  padding-left: 14px;"
    "  padding-right: 14px;"
    "  align-items: center;"
    "  background: #1c1c22;"
    "}"

    ".fb-col-name {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  flex-grow: 1;"
    "  text-align: left;"
    "}"

    ".fb-col-size {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  width: 80px;"
    "  text-align: right;"
    "}"

    ".fb-file-list {"
    "  flex-grow: 1;"
    "  overflow-y: scroll;"
    "  padding: 4px;"
    "  gap: 2px;"
    "  align-items: flex-start;"
    "  background: #111114;"
    "}"

    ".fb-entry {"
    "  width: 100%;"
    "  height: 26px;"
    "  background: transparent;"
    "  color: #8890b0;"
    "  font-size: 12px;"
    "  text-align: left;"
    "  padding-left: 8px;"
    "  corner-radius: 3px;"
    "}"

    ".fb-entry-selected {"
    "  background: #242430;"
    "}"

    ".fb-entry-dir {"
    "  color: #6e8aff;"
    "}"

    ".fb-empty {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  padding: 14px;"
    "}"

    ".fb-bottom {"
    "  background: #1c1c22;"
    "  width: 100%;"
    "  min-height: 70px;"
    "  flex-shrink: 0;"
    "  padding: 6px 10px;"
    "  gap: 6px;"
    "}"

    ".fb-bottom-row {"
    "  width: 100%;"
    "  height: 26px;"
    "  align-items: center;"
    "  gap: 8px;"
    "}"

    ".fb-label {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  width: 50px;"
    "  text-align: right;"
    "}"

    ".fb-selected-name {"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  flex-grow: 1;"
    "  text-align: left;"
    "}"

    ".fb-filter-select {"
    "  width: 200px;"
    "}"

    ".fb-btn {"
    "  width: 80px;"
    "  height: 26px;"
    "  background: #242430;"
    "  color: #8890b0;"
    "  font-size: 12px;"
    "  corner-radius: 4px;"
    "}"

    ".fb-btn-primary {"
    "  background: #6e8aff;"
    "  color: #0d0d0f;"
    "}"

    /* ---- Hierarchy panel ---- */

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
    "  color: #6e8aff;"
    "  font-size: 12px;"
    "  height: 16px;"
    "}"

    ".hierarchy-entity {"
    "  color: #8890b0;"
    "  font-size: 12px;"
    "  height: 16px;"
    "}"

    /* Inner-scene entity exposed by an expanded prototype instance.
       Edits route through the override system, so we tint these
       slightly differently to distinguish them from genuine scene
       entities. */
    ".hierarchy-proto-inner {"
    "  color: #6e8aff;"
    "  font-size: 12px;"
    "  font-style: italic;"
    "  height: 16px;"
    "}"

    ".hierarchy-component {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  height: 16px;"
    "}"

    ".hierarchy-add-btn {"
    "  width: 100%;"
    "  height: 24px;"
    "  background: transparent;"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
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
    "  color: #4a4e6a;"
    "  font-size: 12px;"
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
    "  height: 22px;"
    "  background: #0d0d0f;"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "  padding-left: 6px;"
    "  corner-radius: 3px;"
    "}"

    ".inspector-meta-row {"
    "  width: 100%;"
    "  height: 18px;"
    "  gap: 4px;"
    "  align-items: center;"
    "  padding-left: 0px;"
    "}"

    ".inspector-tag-input {"
    "  height: 18px;"
    "  flex-grow: 1;"
    "  background: #0d0d0f;"
    "  color: #8890b0;"
    "  font-size: 12px;"
    "  padding-left: 4px;"
    "  corner-radius: 3px;"
    "}"

    ".inspector-section-header {"
    "  background: #1c1c22;"
    "  width: 100%;"
    "  height: 22px;"
    "  padding: 0px 8px;"
    "  margin-top: 4px;"
    "  align-items: center;"
    "  gap: 4px;"
    "}"

    ".inspector-section-label {"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "  color: #8890b0;"
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
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  text-align: left;"
    "  width: 100%;"
    "}"

    /* Highlights a field that has been overridden on a prototype instance. */
    ".inspector-field-overridden {"
    "  color: #ffd166;"
    "}"

    ".inspector-vec-row {"
    "  width: 100%;"
    "  gap: 0px;"
    "  align-items: center;"
    "  flex-wrap: wrap;"
    "}"

    ".inspector-axis-x { color: #ff5370; font-size: 12px; font-weight: bold; width: 10px; }"
    ".inspector-axis-y { color: #80ff80; font-size: 12px; font-weight: bold; width: 10px; }"
    ".inspector-axis-z { color: #5b9cff; font-size: 12px; font-weight: bold; width: 10px; }"
    ".inspector-axis-w { color: #bf80ff; font-size: 12px; font-weight: bold; width: 10px; }"

    ".inspector-axis-group {"
    "  width: 68px;"
    "  padding-left: 4px;"
    "  gap: 2px;"
    "  align-items: center;"
    "}"

    ".inspector-vec-input {"
    "  width: 50px;"
    "  height: 17px;"
    "  background: #0d0d0f;"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  padding-left: 4px;"
    "  corner-radius: 3px;"
    "}"

    ".inspector-scalar-input {"
    "  width: 100%;"
    "  height: 17px;"
    "  background: #0d0d0f;"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  padding-left: 4px;"
    "  corner-radius: 3px;"
    "}"

    ".inspector-field-value {"
    "  color: #8890b0;"
    "  font-size: 12px;"
    "}"

    ".inspector-id-label {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  width: 100%;"
    "  text-align: left;"
    "  padding-left: 2px;"
    "}"

    ".inspector-tag-icon {"
    "  color: #6bffb8;"
    "  font-size: 12px;"
    "  width: 16px;"
    "  flex-shrink: 0;"
    "}"

    ".hierarchy-selected {"
    "  background: #242430;"
    "}"


    /* ---- Plugin Manager window ---- */
    ".pm-root {"
    "  width: 100%;"
    "  height: 100%;"
    "  background: #111114;"
    "}"

    ".pm-spacer { flex-grow: 1; }"

    ".pm-header {"
    "  width: 100%;"
    "  height: 32px;"
    "  background: #16161c;"
    "  align-items: center;"
    "  padding: 0px 14px;"
    "  gap: 0px;"
    "}"

    ".pm-hdr {"
    "  color: #5a5e7a;"
    "  font-size: 10px;"
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
    "  background: #1e2030;"
    "}"

    ".pm-body {"
    "  width: 100%;"
    "  flex-grow: 1;"
    "  overflow-y: scroll;"
    "  gap: 0px;"
    "}"

    ".pm-empty {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  text-align: center;"
    "  padding: 32px;"
    "}"

    ".pm-row {"
    "  width: 100%;"
    "  height: 28px;"
    "  align-items: center;"
    "  padding: 0px 14px;"
    "  gap: 0px;"
    "}"

    ".pm-row-alt {"
    "  background: #13131a;"
    "}"

    ".pm-status {"
    "  width: 64px;"
    "  font-size: 10px;"
    "  font-weight: bold;"
    "}"

    ".pm-status-active   { color: #6bffb8; }"
    ".pm-status-loading  { color: #ffd166; }"
    ".pm-status-disabled { color: #4a4e6a; }"

    ".pm-cell-name {"
    "  width: 140px;"
    "  color: #e0e4ff;"
    "  font-size: 12px;"
    "}"

    ".pm-cell-version {"
    "  width: 60px;"
    "  color: #5a5e7a;"
    "  font-size: 11px;"
    "}"

    ".pm-cell-author {"
    "  width: 100px;"
    "  color: #5a5e7a;"
    "  font-size: 11px;"
    "}"

    ".pm-cell-actions {"
    "  width: 80px;"
    "  align-items: center;"
    "  gap: 6px;"
    "}"

    ".pm-reload-btn {"
    "  background: transparent;"
    "  color: #6e8aff;"
    "  font-size: 12px;"
    "  width: 20px;"
    "  height: 20px;"
    "  align-items: center;"
    "  text-align: center;"
    "}"

    ".pm-toggle {"
    "  width: 30px;"
    "  height: 16px;"
    "}"

    /* ---- Renderer Settings modal ---- */
    ".renderer-settings-modal {"
    "  width: 340px;"
    "  height: auto;"
    "  background: #16161a;"
    "  corner-radius: 8px;"
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
    "  color: #8890b0;"
    "  font-size: 12px;"
    "}"

    ".renderer-section-label {"
    "  color: #a0a8c8;"
    "  font-size: 11px;"
    "  padding: 8px 12px 2px 12px;"
    "}"

    /* ---- Renderer Stats modal ---- */
    ".renderer-stats-modal {"
    "  width: 260px;"
    "  height: auto;"
    "  background: #16161a;"
    "  corner-radius: 8px;"
    "  padding: 0px;"
    "  gap: 0px;"
    "}"

    ".renderer-stats-panel {"
    "  width: 100%;"
    "  padding: 0px;"
    "}"

    ".renderer-stat-row {"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  padding: 4px 12px;"
    "  font-family: monospace;"
    "}"

    /* ---- Breadcrumb navigation bar ---- */
    ".breadcrumb-bar {"
    "  background: #16161a;"
    "  width: 100%;"
    "  height: 24px;"
    "  align-items: center;"
    "  padding-left: 8px;"
    "  padding-right: 8px;"
    "  gap: 6px;"
    "}"

    ".breadcrumb-back {"
    "  height: 20px;"
    "  background: transparent;"
    "  color: #6e8aff;"
    "  font-size: 12px;"
    "  corner-radius: 3px;"
    "  padding-left: 6px;"
    "  padding-right: 6px;"
    "}"

    ".breadcrumb-text {"
    "  color: #8890b0;"
    "  font-size: 12px;"
    "}"

    ".breadcrumb-separator {"
    "  color: #4a4e6a;"
    "  font-size: 10px;"
    "}"

    ".breadcrumb-active {"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "}"

    /* ---- Inspector edit button ---- */
    ".inspector-edit-btn {"
    "  height: 18px;"
    "  background: #242430;"
    "  color: #6e8aff;"
    "  font-size: 11px;"
    "  corner-radius: 3px;"
    "  padding-left: 8px;"
    "  padding-right: 8px;"
    "}"

    /* Full-width "Edit Prototype" action row beneath the Prototype
       component header — opens the prototype in its own editor window. */
    ".inspector-proto-edit-row {"
    "  width: 100%;"
    "  height: 26px;"
    "  background: #242430;"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "  text-align: center;"
    "  corner-radius: 4px;"
    "  margin-top: 4px;"
    "  margin-bottom: 4px;"
    "}"
    ".inspector-proto-edit-row:hover {"
    "  background: #2e2e3e;"
    "  color: #6e8aff;"
    "}"

    /* ---- Inspector remove-component button (× in section header) ---- */
    ".inspector-remove-btn {"
    "  width: 18px;"
    "  height: 18px;"
    "  background: transparent;"
    "  color: #5a5e7a;"
    "  font-size: 10px;"
    "  text-align: center;"
    "  corner-radius: 3px;"
    "  padding: 0px;"
    "}"

    /* ---- Inspector: Add Component dropdown row ---- */
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
    "  height: 22px;"
    "  background: #242430;"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "  text-align: center;"
    "  padding-left: 12px;"
    "  padding-right: 20px;"
    "  corner-radius: 4px;"
    "}"

    ".inspector-select {"
    "  width: 100%;"
    "  height: 18px;"
    "  background: #0d0d0f;"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  padding-left: 6px;"
    "  padding-right: 18px;"
    "  corner-radius: 3px;"
    "}"

    /* ---- Import Asset dialog ---- */
    ".import-root {"
    "  background: #16161a;"
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
    "  color: #c8d0ff;"
    "  font-size: 12px;"
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
    "  background: #1c1c22;"
    "  padding: 8px;"
    "  corner-radius: 3px;"
    "  gap: 4px;"
    "  overflow-y: scroll;"
    "  align-items: flex-start;"
    "}"

    ".import-col-title {"
    "  color: #6e8aff;"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "  padding-bottom: 4px;"
    "}"

    ".import-row {"
    "  width: 100%;"
    "  align-items: center;"
    "  gap: 8px;"
    "}"

    ".import-item {"
    "  color: #c8d0ff;"
    "  font-size: 11px;"
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
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  flex-grow: 1;"
    "}"

    ".import-cb-flag {"
    "  color: #8890b0;"
    "  font-size: 11px;"
    "}"

    ".import-footer {"
    "  width: 100%;"
    "  height: 36px;"
    "  align-items: center;"
    "  justify-content: flex-end;"
    "  gap: 8px;"
    "  padding-top: 6px;"
    "}"

    ".import-btn {"
    "  height: 24px;"
    "  background: #242430;"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  corner-radius: 3px;"
    "  padding-left: 14px;"
    "  padding-right: 14px;"
    "}"

    ".import-btn-primary {"
    "  background: #6e8aff;"
    "  color: #0d0d0f;"
    "  font-weight: bold;"
    "}"

    ".import-progress {"
    "  color: #c8d0ff;"
    "  font-size: 14px;"
    "  padding: 24px;"
    "}"
;

/* ---- Breadcrumb bar for prototype editor mode ---- */
static Ca_Div   *s_breadcrumb_bar;
static Ca_Label *s_breadcrumb_scene_label;
static Ca_Label *s_breadcrumb_proto_label;

static void on_breadcrumb_back(Ca_Button *btn, void *data)
{
    (void)btn;
    Editor *ed = (Editor *)data;
    editor_close_prototype(ed);
}

static void editor_build_ui(Editor *ed)
{
    Ca_Window *window = qs_engine_window(ed->engine);

    ca_ui_begin(window, &(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "editor-root",
    });

    ed_toolbar(window, ed);

    /* Breadcrumb bar — visible only in prototype editor mode */
    s_breadcrumb_bar = ca_div_begin(&(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .style     = "breadcrumb-bar",
        .id        = "breadcrumb-bar",
        .hidden    = true,
    });
    {
        ca_btn_begin(&(Ca_BtnDesc){
            .text       = "\xEF\x81\x88 Back",  /* U+F048 step-backward */
            .id         = "breadcrumb-back",
            .style      = "breadcrumb-back",
            .on_click   = on_breadcrumb_back,
            .click_data = ed,
        });
        ca_btn_end();
        s_breadcrumb_scene_label = ca_text(&(Ca_TextDesc){
            .text  = "",
            .id    = "breadcrumb-scene",
            .style = "breadcrumb-text",
        });
        ca_text(&(Ca_TextDesc){
            .text  = ">",
            .id    = "breadcrumb-sep",
            .style = "breadcrumb-separator",
        });
        s_breadcrumb_proto_label = ca_text(&(Ca_TextDesc){
            .text  = "",
            .id    = "breadcrumb-proto",
            .style = "breadcrumb-active",
        });
    }
    ca_div_end();

    ed_layout(window, ed);
    ed_status_bar(window, ed);

    ca_ui_end();
}

static void on_mouse_button(const Ca_Event *event, void *userdata)
{
    (void)userdata;
    qs_input_mouse_button_event((Qs_MouseButton)event->mouse_button.button,
                                event->mouse_button.action);
}

/* Fires synchronously before the plugin library is unloaded.
   Destroy any scene renderer that references the plugin's backend so
   the backend and its Vulkan context can be cleanly shut down. */
static bool on_plugin_reload_begin(const Qs_Event *e, void *userdata)
{
    (void)e;
    Editor *ed = (Editor *)userdata;
    if (ed->scene_renderer) {
        qs_renderer_destroy(ed->scene_renderer);
        ed->scene_renderer = NULL;
    }
    return false;
}

/* Fires synchronously after the plugin has been successfully reloaded
   or enabled.  Recreate the scene renderer and rebuild the toolbar. */
static bool on_plugin_reload_end(const Qs_Event *e, void *userdata)
{
    (void)e;
    Editor *ed = (Editor *)userdata;
    if (!ed->scene_renderer) {
        ed->scene_renderer = qs_renderer_create(ed->engine, &(Qs_RendererDesc){
            .name        = "scene",
            .clear_color = { 0.0f, 0.0f, 0.0f, 1.0f },
            .depth_test  = true,
        });
        if (ed->scene_renderer && ed->scene_viewport) {
            qs_renderer_bind(ed->scene_renderer, (Qs_Viewport *)ed->scene_viewport);
        }
        if (ed->scene_renderer) {
            ed_pick_attach(ed->scene_renderer);
            ed_gizmo_attach(ed->scene_renderer);
        }
    }
    ed_toolbar_rebuild();
    ed_menu_bar_invalidate();
    return false;
}

/* Fires after a plugin is fully disabled and unloaded.
   Only rebuild the toolbar and menu — do NOT recreate the renderer since
   the backend that provides it may be the one being disabled. */
static bool on_plugin_disable_end(const Qs_Event *e, void *userdata)
{
    (void)e; (void)userdata;
    ed_toolbar_rebuild();
    ed_menu_bar_invalidate();
    return false;
}

static void on_mouse_move(const Ca_Event *event, void *userdata)
{
    (void)userdata;
    qs_input_mouse_pos_event(event->mouse_pos.x, event->mouse_pos.y);
}

static void on_mouse_scroll(const Ca_Event *event, void *userdata)
{
    (void)userdata;
    qs_input_mouse_scroll_event(event->mouse_scroll.dx, event->mouse_scroll.dy);
}

static void on_key_event(const Ca_Event *event, void *userdata)
{
    (void)userdata;
    qs_input_key_event((Qs_Key)event->key.key,
                       (Qs_KeyAction)event->key.action,
                       event->key.mods);
}

static void on_frame(Qs_Engine *engine, void *userdata)
{
    (void)engine;
    Editor *ed = userdata;
    if (ed->scene_viewport)
        ca_viewport_request_redraw(ed->scene_viewport);
    ed_camera_update(&ed->cam, ed->scene_renderer, qs_engine_dt(ed->engine));
    ed_gizmo_update(ed, qs_engine_dt(ed->engine));

    /* Submit scene renderables and lights for this frame */
    Qs_Scene *scene = qs_scene_active();
    if (scene && ed->scene_renderer) {
        qs_renderer_clear_renderables(ed->scene_renderer);
        qs_renderer_clear_lights(ed->scene_renderer);

        /* Recursive submission walks the scene + every PrototypeComp's
           inner scene, composing world transforms.  Lights are submitted
           only at the top level. */
        qs_scene_submit_renderables(scene, ed->engine, ed->scene_renderer, NULL);
    }

    ed_menu_bar_sync();
    ed_hierarchy_update(ed);
    ed_console_update(ed);
    ed_inspector_update(ed);
    ed_file_browser_update();
}

static void on_log(void *userdata)
{
    (void)userdata;
    qs_engine_wake();
}

/* ---- Scene file loading ---- */

/* Resolve the base directory of the scene file for relative asset paths. */
static void scene_dir(const char *scene_path, char *out, size_t out_size)
{
    const char *last = NULL;
    for (const char *p = scene_path; *p; p++)
        if (*p == '/' || *p == '\\') last = p;
    if (!last) {
        snprintf(out, out_size, ".");
    } else {
        size_t len = (size_t)(last - scene_path);
        if (len >= out_size) len = out_size - 1;
        memcpy(out, scene_path, len);
        out[len] = '\0';
    }
}

static bool editor_load_scene(Editor *ed, const char *path)
{
    /* Scene name will be populated from the file by qs_scene_load. */
    Qs_Scene *scene = qs_scene_create(ed->engine, &(Qs_SceneDesc){
        .name = "Scene",
    });
    if (!scene) return false;
    qs_scene_set_active(scene);

    if (!qs_scene_load(scene, ed->engine, path)) {
        QS_LOG_ERROR("Failed to load scene: %s", path);
        return false;
    }

    /* Add a default directional sun light if the scene has no lights */
    if (qs_scene_first(scene, qs_light_comp_type()) == QS_ENTITY_INVALID) {
        Qs_Entity sun = qs_entity_create(scene, "Sun");
        Qs_LightComp *lc = qs_entity_add(scene, sun, qs_light_comp_type());
        if (lc) {
            lc->color[0]     = 1.0f;
            lc->color[1]     = 0.95f;
            lc->color[2]     = 0.9f;
            lc->intensity    = 3.0f;
        }
    }

    QS_LOG_INFO("Scene loaded: %s", path);
    return true;
}

Editor *editor_create(const EditorDesc *desc)
{
    Editor *ed = calloc(1, sizeof(Editor));
    if (!ed) return NULL;
    ed->selected_entity     = QS_ENTITY_INVALID;
    ed->proto_owner         = QS_ENTITY_INVALID;
    ed->proto_inner_scene   = NULL;
    ed->proto_preview_light = QS_ENTITY_INVALID;

    /* Open project */
    if (desc->project_path) {
        ed->project = qs_project_open(desc->project_path);
        if (!ed->project) {
            free(ed);
            return NULL;
        }
    }

    /* Build window title: "Quasar Editor — ProjectName" */
    char title[256];
    if (ed->project)
        snprintf(title, sizeof(title), "%s \xE2\x80\x94 %s",
                 desc->title ? desc->title : "Quasar Editor",
                 qs_project_name(ed->project));
    else
        snprintf(title, sizeof(title), "%s",
                 desc->title ? desc->title : "Quasar Editor");

    ed->engine = qs_engine_create(&(Qs_EngineDesc){
        .app_name      = title,
        .version_major = 0,
        .version_minor = 1,
        .version_patch = 0,
        .window_width  = desc->width,
        .window_height = desc->height,
    });
    if (!ed->engine) {
        free(ed);
        return NULL;
    }

    qs_engine_set_stylesheet(ed->engine, g_editor_css);
    if (ed->project)
        qs_engine_set_project(ed->engine, ed->project);

    ed_gizmo_init(ed->engine);
    ed_pick_init(ed->engine);

    ed->scene_renderer = qs_renderer_create(ed->engine, &(Qs_RendererDesc){
        .name        = "scene",
        .clear_color = { 0.0f, 0.0f, 0.0f, 1.0f },
        .depth_test  = true,
    });

    if (ed->scene_renderer) {
        ed_pick_attach(ed->scene_renderer);
        ed_gizmo_attach(ed->scene_renderer);
    }

    /* Position camera for a good view of the test scene */
    if (ed->scene_renderer) {
        Qs_Camera *cam = qs_renderer_camera(ed->scene_renderer);
        if (cam) {
            cam->position[0] =  5.0f;
            cam->position[1] =  4.0f;
            cam->position[2] =  8.0f;
            cam->target[0]   =  0.0f;
            cam->target[1]   =  0.5f;
            cam->target[2]   =  0.0f;
        }
    }

    /* ---- Load scene from project ---- */
    if (ed->project) {
        char scene_path[512];
        snprintf(scene_path, sizeof(scene_path), "%s/scenes/default.qscene",
                 qs_project_path(ed->project));
        editor_load_scene(ed, scene_path);
    }

    ed_plugin_manager_init(ed);
    ed_toolbar_init(ed);

    editor_build_ui(ed);

    ca_window_set_title(qs_engine_window(ed->engine), "Quasar Editor");
    ed_menu_bar_init(qs_engine_window(ed->engine), ed);

    ed_file_browser_init(ca_window_instance(qs_engine_window(ed->engine)));
    ed_import_dialog_init(ed);

    qs_engine_set_event_handler(ed->engine, CA_EVENT_KEY,          on_key_event,    ed);
    qs_engine_set_event_handler(ed->engine, CA_EVENT_MOUSE_BUTTON, on_mouse_button, ed);
    qs_engine_set_event_handler(ed->engine, CA_EVENT_MOUSE_MOVE,   on_mouse_move,   ed);
    qs_engine_set_event_handler(ed->engine, CA_EVENT_MOUSE_SCROLL, on_mouse_scroll, ed);
    qs_engine_set_on_frame(ed->engine, on_frame, ed);
    qs_log_set_listener(on_log, ed);

    /* Subscribe to plugin reload events to refresh the scene renderer */
    Qs_EventBus *bus = qs_engine_event_bus(ed->engine);
    qs_event_subscribe(bus, QS_EVENT_PLUGIN_RELOAD_BEGIN,  on_plugin_reload_begin, ed);
    qs_event_subscribe(bus, QS_EVENT_PLUGIN_RELOAD_END,    on_plugin_reload_end,   ed);
    qs_event_subscribe(bus, QS_EVENT_PLUGIN_DISABLE_BEGIN, on_plugin_reload_begin, ed);
    qs_event_subscribe(bus, QS_EVENT_PLUGIN_DISABLE_END,   on_plugin_disable_end,  ed);
    qs_event_subscribe(bus, QS_EVENT_PLUGIN_ENABLE_END,    on_plugin_reload_end,   ed);

    ed_camera_init(&ed->cam);

    return ed;
}

int editor_run(Editor *ed)
{
    if (!ed) return 1;
    return qs_engine_run(ed->engine);
}

void editor_request_exit(Editor *ed)
{
    if (ed) qs_engine_request_exit(ed->engine);
}

Qs_Renderer *editor_scene_renderer(Editor *ed)
{
    return ed ? ed->scene_renderer : NULL;
}

void editor_set_scene_viewport(Editor *ed, Ca_Viewport *viewport)
{
    if (ed) ed->scene_viewport = viewport;
}

Qs_Engine *editor_engine(Editor *ed)
{
    return ed ? ed->engine : NULL;
}

Qs_Project *editor_project(const Editor *ed)
{
    return ed ? ed->project : NULL;
}

Ca_Viewport *editor_scene_viewport(const Editor *ed)
{
    return ed ? ed->scene_viewport : NULL;
}

Qs_Entity editor_selected_entity(const Editor *ed)
{
    return ed ? ed->selected_entity : QS_ENTITY_INVALID;
}

void editor_set_selected_entity(Editor *ed, Qs_Entity entity)
{
    if (!ed) return;
    ed->selected_entity   = entity;
    ed->proto_owner       = QS_ENTITY_INVALID;
    ed->proto_inner_scene = NULL;
}

void editor_set_proto_selection(Editor *ed,
                                Qs_Entity owner,
                                Qs_Scene *inner_scene,
                                Qs_Entity inner_entity)
{
    if (!ed) return;
    if (owner == QS_ENTITY_INVALID || !inner_scene) {
        editor_set_selected_entity(ed, inner_entity);
        return;
    }
    ed->selected_entity   = inner_entity;
    ed->proto_owner       = owner;
    ed->proto_inner_scene = inner_scene;
}

Qs_Entity editor_proto_owner(const Editor *ed)
{
    return ed ? ed->proto_owner : QS_ENTITY_INVALID;
}

Qs_Scene *editor_proto_inner_scene(const Editor *ed)
{
    return ed ? ed->proto_inner_scene : NULL;
}

EditorMode editor_mode(const Editor *editor)
{
    return editor ? editor->mode : ED_MODE_SCENE;
}

bool editor_open_prototype(Editor *editor, const char *proto_path)
{
    if (!editor || !proto_path) return false;
    if (editor->proto_stack_depth >= EDITOR_PROTO_STACK_DEPTH) {
        QS_LOG_ERROR("Prototype edit stack full (max depth %d)",
                     EDITOR_PROTO_STACK_DEPTH);
        return false;
    }

    /* Push current scene + camera onto the stack */
    editor->proto_stack_scene[editor->proto_stack_depth] = qs_scene_active();
    editor->proto_stack_cam  [editor->proto_stack_depth] = editor->cam;
    editor->proto_stack_depth++;

    /* Load the .qproto file into a fresh scene and activate it.
       qs_scene_load handles parent restoration + asset cache resolution. */
    Qs_Scene *proto_scene = qs_scene_create(editor->engine, &(Qs_SceneDesc){
        .name = "Prototype",
    });
    if (!proto_scene ||
        !qs_scene_load(proto_scene, editor->engine, proto_path))
    {
        if (proto_scene) qs_scene_destroy(proto_scene);
        editor->proto_stack_depth--;
        QS_LOG_ERROR("Failed to load prototype: %s", proto_path);
        return false;
    }
    qs_scene_set_active(proto_scene);

    snprintf(editor->proto_path, sizeof(editor->proto_path), "%s", proto_path);
    editor->selected_entity   = QS_ENTITY_INVALID;
    editor->proto_owner       = QS_ENTITY_INVALID;
    editor->proto_inner_scene = NULL;

    /* Inject an editor-only preview directional light if the prototype
       has no lights of its own.  This is purely a viewport convenience —
       it is destroyed before the prototype is saved so the .qproto file
       never gains a stray light entity. */
    editor->proto_preview_light = QS_ENTITY_INVALID;
    if (qs_scene_first(proto_scene, qs_light_comp_type()) == QS_ENTITY_INVALID) {
        Qs_Entity sun = qs_entity_create(proto_scene, "(Editor Preview Light)");
        if (sun != QS_ENTITY_INVALID) {
            Qs_LightComp *lc = qs_entity_add(proto_scene, sun, qs_light_comp_type());
            if (lc) {
                lc->color[0]  = 1.0f;
                lc->color[1]  = 0.95f;
                lc->color[2]  = 0.9f;
                lc->intensity = 3.0f;
                editor->proto_preview_light = sun;
            } else {
                qs_entity_destroy(proto_scene, sun);
            }
        }
    }

    /* Reset camera for prototype view */
    ed_camera_init(&editor->cam);
    if (editor->scene_renderer) {
        Qs_Camera *cam = qs_renderer_camera(editor->scene_renderer);
        if (cam) {
            cam->position[0] =  5.0f;
            cam->position[1] =  4.0f;
            cam->position[2] =  8.0f;
            cam->target[0]   =  0.0f;
            cam->target[1]   =  0.5f;
            cam->target[2]   =  0.0f;
        }
    }

    /* Update breadcrumb bar */
    if (s_breadcrumb_bar) {
        Qs_Scene *prev =
            editor->proto_stack_scene[editor->proto_stack_depth - 1];
        const char *scene_name_str = prev ? qs_scene_name(prev) : "Scene";
        ca_set_text(s_breadcrumb_scene_label, scene_name_str);
        ca_set_text(s_breadcrumb_proto_label, qs_scene_name(proto_scene));
        ca_set_hidden(s_breadcrumb_bar, false);
    }

    editor->mode = ED_MODE_PROTOTYPE;
    QS_LOG_INFO("Editing prototype: %s", proto_path);
    return true;
}

void editor_close_prototype(Editor *editor)
{
    if (!editor || editor->proto_stack_depth == 0) return;

    /* Save the current prototype scene back to its source path. */
    Qs_Scene *proto_scene = qs_scene_active();
    /* Strip the editor-only preview light first so it never lands in
       the saved .qproto. */
    if (proto_scene && editor->proto_preview_light != QS_ENTITY_INVALID) {
        if (qs_entity_valid(proto_scene, editor->proto_preview_light))
            qs_entity_destroy(proto_scene, editor->proto_preview_light);
        editor->proto_preview_light = QS_ENTITY_INVALID;
    }
    if (proto_scene && editor->proto_path[0])
        qs_scene_save(proto_scene, editor->proto_path);
    if (proto_scene)
        qs_scene_destroy(proto_scene);

    /* Pop the previous scene + camera */
    editor->proto_stack_depth--;
    Qs_Scene *prev = editor->proto_stack_scene[editor->proto_stack_depth];
    qs_scene_set_active(prev);
    editor->cam = editor->proto_stack_cam[editor->proto_stack_depth];

    editor->selected_entity   = QS_ENTITY_INVALID;
    editor->proto_owner       = QS_ENTITY_INVALID;
    editor->proto_inner_scene = NULL;
    editor->proto_path[0]     = '\0';

    if (editor->proto_stack_depth == 0) {
        if (s_breadcrumb_bar) ca_set_hidden(s_breadcrumb_bar, true);
        editor->mode = ED_MODE_SCENE;
    }
    QS_LOG_INFO("Returned to %s", prev ? qs_scene_name(prev) : "(none)");
}

void editor_destroy(Editor *ed)
{
    if (!ed) return;
    /* Pop and discard any prototype edit stack (don't auto-save). */
    while (ed->proto_stack_depth > 0) {
        Qs_Scene *cur = qs_scene_active();
        if (cur) qs_scene_destroy(cur);
        ed->proto_stack_depth--;
        qs_scene_set_active(ed->proto_stack_scene[ed->proto_stack_depth]);
    }
    ed_pick_shutdown(ed->engine);
    ed_gizmo_shutdown(ed->engine);
    qs_engine_destroy(ed->engine);
    qs_project_destroy(ed->project);
    free(ed);
}
