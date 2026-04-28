#ifndef EDITOR_H
#define EDITOR_H

#include "quasar.h"

typedef struct Editor Editor;

/// Editor operating mode.
typedef enum EditorMode {
    ED_MODE_SCENE,        ///< Editing the scene.
    ED_MODE_PROTOTYPE,    ///< Editing a prototype (.qproto) in isolation.
} EditorMode;

typedef struct EditorDesc {
    const char *title;
    const char *project_path;  ///< Project directory path (from launcher).
    int         width;
    int         height;
} EditorDesc;

/// Creates the editor application: engine, window, and UI.
Editor *editor_create(const EditorDesc *desc);

/// Runs the editor main loop. Blocks until the window is closed.
int editor_run(Editor *editor);

/// Requests the editor to close its window and exit.
void editor_request_exit(Editor *editor);

/// Returns the editor's scene renderer.
Qs_Renderer *editor_scene_renderer(Editor *editor);

/// Sets the editor's scene viewport.
void editor_set_scene_viewport(Editor *editor, Ca_Viewport *viewport);

/// Returns the editor's engine instance.
Qs_Engine *editor_engine(Editor *editor);

/// Returns the editor's project.
Qs_Project *editor_project(const Editor *editor);

/// Returns the path of the currently loaded scene file, or "" if none.
const char *editor_current_scene_path(const Editor *editor);

/// Saves the active scene to its source path on disk.  When in prototype
/// edit mode, saves to the .qproto path instead.
bool editor_save_scene(Editor *editor);

/// Saves the project file (.quasar) and the active scene.
bool editor_save_project(Editor *editor);

/// Performs an undo step against the editor's command stack.
bool editor_undo(Editor *editor);

/// Performs a redo step against the editor's command stack.
bool editor_redo(Editor *editor);

/// Returns the editor's scene viewport.
Ca_Viewport *editor_scene_viewport(const Editor *editor);

/// Returns the currently selected entity, or QS_ENTITY_INVALID.
Qs_Entity editor_selected_entity(const Editor *editor);

/// Sets the selected entity.
void editor_set_selected_entity(Editor *editor, Qs_Entity entity);

/// Selects an entity inside a prototype instance for editing through
/// the override system.  `owner` is the outer-scene entity that holds
/// the PrototypeComp; `inner_scene` and `inner_entity` identify the
/// entity inside the loaded inner scene.  Pass QS_ENTITY_INVALID for
/// `owner` to clear (equivalent to `editor_set_selected_entity`).
void editor_set_proto_selection(Editor *editor,
                                Qs_Entity owner,
                                Qs_Scene *inner_scene,
                                Qs_Entity inner_entity);

/// Returns the outer-scene entity owning the active prototype-override
/// editing context, or QS_ENTITY_INVALID when not editing into a prototype.
Qs_Entity editor_proto_owner(const Editor *editor);

/// Returns the inner scene currently being edited via overrides, or NULL.
Qs_Scene *editor_proto_inner_scene(const Editor *editor);

/// Returns the current editor mode.
EditorMode editor_mode(const Editor *editor);

/// Opens a prototype for isolated editing.  Saves the current scene context
/// and creates a temporary scene for the prototype.
bool editor_open_prototype(Editor *editor, const char *proto_path);

/// Closes the prototype editor and restores the previous scene.
void editor_close_prototype(Editor *editor);

/// Destroys the editor and all owned resources.
void editor_destroy(Editor *editor);

#endif
