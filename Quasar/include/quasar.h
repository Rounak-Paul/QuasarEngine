#ifndef QUASAR_H
#define QUASAR_H

#include <stdint.h>
#include "causality.h"
#include "qs_gpu.h"
#include "qs_log.h"
#include "qs_event.h"
#include "qs_job.h"
#include "qs_system.h"
#include "qs_renderer.h"
#include "qs_texture.h"
#include "qs_mesh.h"
#include "qs_material.h"
#include "qs_light.h"
#include "qs_scene.h"
#include "qs_reflect.h"
#include "qs_project.h"
#include "qs_input.h"
#include "qs_dylib.h"
#include "qs_plugin.h"
#include "qs_asset.h"
#include "qs_ext.h"

typedef struct Qs_Engine Qs_Engine;

typedef struct Qs_EngineDesc {
    const char* app_name;
    uint32_t    version_major;
    uint32_t    version_minor;
    uint32_t    version_patch;
    int         window_width;    ///< Initial window width  (default: 1280).
    int         window_height;   ///< Initial window height (default: 720).
    float       font_size_px;    ///< UI font size in logical pixels (default: 14).
    /// Directory to scan for plugins. NULL = auto-detect as <exe_dir>/plugins.
    const char* plugin_dir;
} Qs_EngineDesc;

/// Creates a new Quasar Engine instance with a window and all built-in systems.
Qs_Engine* qs_engine_create(const Qs_EngineDesc* desc);

/// Destroys the engine, all systems, and the underlying window/GPU context.
void qs_engine_destroy(Qs_Engine* engine);

/// Runs the engine main loop. Blocks until the window is closed.
int qs_engine_run(Qs_Engine* engine);

/// Returns the engine's main window for UI building.
Ca_Window* qs_engine_window(Qs_Engine* engine);

/// Returns the engine's GPU context for renderer backends and low-level access.
Qs_GpuContext* qs_engine_gpu(Qs_Engine* engine);

/// Returns the engine's event bus.
Qs_EventBus* qs_engine_event_bus(Qs_Engine* engine);

/// Returns the engine's job system.
Qs_JobSystem* qs_engine_job_system(Qs_Engine* engine);

/// Returns the current frame delta time in seconds.
float qs_engine_dt(const Qs_Engine* engine);

/// Per-frame callback type.
typedef void (*Qs_FrameFn)(Qs_Engine *engine, void *user_data);

/// Sets a per-frame callback invoked after engine systems update each tick.
void qs_engine_set_on_frame(Qs_Engine* engine, Qs_FrameFn fn, void* user_data);

/// Parses and applies a CSS stylesheet to the engine's UI.
void qs_engine_set_stylesheet(Qs_Engine* engine, const char* css);

/// Sets an event handler on the engine's input system.
void qs_engine_set_event_handler(Qs_Engine* engine, Ca_EventType type,
                                  Ca_EventFn fn, void* user_data);

/// Requests the engine to close its window and exit the main loop.
void qs_engine_request_exit(Qs_Engine* engine);

/// Returns the engine's system manager (for use by plugins registering systems).
Qs_SystemManager* qs_engine_systems(Qs_Engine* engine);

/// Returns the engine's plugin manager.
Qs_PluginManager* qs_engine_plugin_manager(Qs_Engine* engine);

/// Sets the active project handle on the engine.  Used by qs_scene to
/// resolve project-relative asset paths (e.g. PrototypeComp.path) against
/// the project root.  The engine does NOT take ownership.
void qs_engine_set_project(Qs_Engine* engine, Qs_Project* project);

/// Returns the active project handle, or NULL if none is set.
Qs_Project* qs_engine_project(Qs_Engine* engine);

/// Wakes the event loop from another thread.
void qs_engine_wake(void);

/// Returns the engine version string.
const char* qs_version_string(void);

#endif
