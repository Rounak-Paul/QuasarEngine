#ifndef EDITOR_H
#define EDITOR_H

#include "quasar.h"

typedef struct Editor Editor;

typedef struct EditorDesc {
    const char *title;
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

/// Destroys the editor and all owned resources.
void editor_destroy(Editor *editor);

#endif
