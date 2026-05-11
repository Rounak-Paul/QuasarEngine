#ifndef QS_PLUGIN_H
#define QS_PLUGIN_H

#include <stdint.h>
#include <stdbool.h>
#include "qs_api.h"

typedef struct Qs_Engine        Qs_Engine;
typedef struct Qs_PluginManager Qs_PluginManager;
typedef struct Qs_PluginState   Qs_PluginState;

/* ================================================================
   PLUGIN ABI
   ================================================================ */

/// Current API version. Plugins must set api_version to this value.
/// A plugin with a mismatched version is rejected at load time.
#define QS_PLUGIN_API_VERSION  3

/// Symbol name every plugin shared library must export.
#define QS_PLUGIN_ENTRY_SYMBOL "qs_plugin_entry"

/// Cross-platform export macro for the plugin entry point.
#ifdef _WIN32
  #define QS_PLUGIN_EXPORT __declspec(dllexport)
#else
  #define QS_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/* ================================================================
   PLUGIN CAPABILITY FLAGS
   ================================================================
   Plugins declare their capabilities via a bitmask in Qs_PluginDesc.
   The engine uses these flags to make scoped decisions on reload/
   enable/disable: only plugins that provide a renderer backend cause
   the renderer to be torn down; asset importers and UI extensions do
   not touch the render pipeline at all.
   ================================================================ */

typedef enum Qs_PluginCapability {
    /// Plugin does not affect any specific engine subsystem.
    QS_PLUGIN_CAP_NONE              = 0,

    /// Plugin registers a Qs_RendererBackend (via qs_renderer_backend_register).
    /// Reloading requires the scene renderer to be destroyed and recreated.
    QS_PLUGIN_CAP_RENDERER_BACKEND  = 1 << 0,

    /// Plugin registers one or more render graph nodes (QS_EXT_RENDER_GRAPH_NODE).
    /// Reloading requires the render pipeline to be rebuilt.
    QS_PLUGIN_CAP_RENDER_NODE       = 1 << 1,

    /// Plugin registers asset importers (QS_EXT_ASSET_IMPORTER).
    /// Safe to hot-reload without touching the renderer.
    QS_PLUGIN_CAP_ASSET_IMPORTER    = 1 << 2,

    /// Plugin contributes editor UI (toolbar, menus, inspector panels).
    /// Safe to hot-reload; only toolbar and menu are refreshed.
    QS_PLUGIN_CAP_EDITOR_UI         = 1 << 3,

    /// Plugin registers ECS systems or scene processors.
    /// Safe to hot-reload; scene systems are re-registered automatically.
    QS_PLUGIN_CAP_SCENE_SYSTEM      = 1 << 4,
} Qs_PluginCapability;

/// Convenience: any capability that requires destroying the renderer on reload.
#define QS_PLUGIN_CAP_AFFECTS_RENDERER \
    (QS_PLUGIN_CAP_RENDERER_BACKEND | QS_PLUGIN_CAP_RENDER_NODE)

/// Describes a plugin and provides its lifecycle callbacks.
///
/// All capabilities (toolbar items, menu entries, inspector panels, engine
/// systems, etc.) are registered dynamically in on_load() via the extension
/// point registry (see qs_ext.h) rather than through fixed callback slots.
typedef struct Qs_PluginDesc {
    /// Reverse-DNS unique identifier, e.g. "com.quasar.builtin.renderer.pbr".
    const char *id;

    /// Human-readable display name shown in the editor Plugins menu.
    const char *name;

    /// Semantic version string, e.g. "1.0.0".
    const char *version;

    /// Author name or organisation.
    const char *author;

    /// Short description shown in the editor.
    const char *description;

    /// Must equal QS_PLUGIN_API_VERSION.
    uint32_t api_version;

    /// Called once after the plugin library is loaded and enabled.
    /// Register extensions, engine systems, render nodes, etc. here.
    /// May be NULL.
    void (*on_load)(Qs_Engine *engine);

    /// Called before the plugin library is unloaded.
    /// Unregister extensions, release plugin-owned resources here.
    /// May be NULL.
    void (*on_unload)(Qs_Engine *engine);

    /// Bitmask of Qs_PluginCapability flags declaring what subsystems this
    /// plugin extends. The engine uses this to make scoped reload decisions
    /// (e.g. only rebuild the renderer when a RENDERER_BACKEND or RENDER_NODE
    /// plugin changes).  Leave 0 / QS_PLUGIN_CAP_NONE if the plugin is
    /// self-contained (e.g. pure scripting / data).
    uint32_t capabilities;
} Qs_PluginDesc;

/// Entry point function type.  Every plugin library must export a function
/// named QS_PLUGIN_ENTRY_SYMBOL with this signature.
typedef const Qs_PluginDesc *(*Qs_PluginEntryFn)(void);

/* ================================================================
   PLUGIN MANAGER API
   ================================================================ */

/// Creates a plugin manager that scans plugin_dir for shared libraries.
/// If plugin_dir is NULL the directory is resolved automatically as
/// "plugins/" adjacent to the running executable.
/// Returns NULL on allocation failure.
QS_API Qs_PluginManager *qs_plugin_manager_create(Qs_Engine *engine,
                                            const char *plugin_dir);

/// Scans the plugin directory, loads all enabled plugins, and persists state.
/// Call once after the core engine systems (Log, Job, Event, Input) are
/// registered and before Scene is registered, so that renderer plugins can
/// register their systems in the correct order.
QS_API void qs_plugin_manager_scan(Qs_PluginManager *pm);

/// Calls on_unload for every loaded plugin in reverse load order,
/// saves plugin state to the user config directory, and frees the manager.
QS_API void qs_plugin_manager_destroy(Qs_PluginManager *pm);

/// Enables a plugin by id and loads it if not already loaded.
/// Returns false if the plugin id is unknown.
QS_API bool qs_plugin_enable(Qs_PluginManager *pm, const char *id);

/// Disables a plugin by id and unloads it if currently loaded.
/// Returns false if the plugin id is unknown.
QS_API bool qs_plugin_disable(Qs_PluginManager *pm, const char *id);

/// Returns the number of discovered plugins (enabled or disabled).
QS_API uint32_t qs_plugin_count(const Qs_PluginManager *pm);

/// Returns the state record for the plugin at index idx.
QS_API const Qs_PluginState *qs_plugin_state_at(const Qs_PluginManager *pm,
                                          uint32_t idx);

/// Returns the descriptor of a plugin state record.  NULL if not loaded.
QS_API const Qs_PluginDesc *qs_plugin_state_desc(const Qs_PluginState *state);

/// Returns true if the plugin is currently enabled.
QS_API bool qs_plugin_state_enabled(const Qs_PluginState *state);

/// Returns true if the plugin library is currently loaded in memory.
QS_API bool qs_plugin_state_loaded(const Qs_PluginState *state);

/// Returns the file-system path of the plugin shared library.
QS_API const char *qs_plugin_state_path(const Qs_PluginState *state);

/// Returns the plugin id (available even before the library is loaded,
/// from the persisted state).
QS_API const char *qs_plugin_state_id(const Qs_PluginState *state);

/// Returns the plugin's human-readable name.
QS_API const char *qs_plugin_state_name(const Qs_PluginState *state);

/// Returns the plugin's version string.
QS_API const char *qs_plugin_state_version(const Qs_PluginState *state);

/// Returns the plugin's author.
QS_API const char *qs_plugin_state_author(const Qs_PluginState *state);

/// Returns the plugin's declared capability flags (bitmask of Qs_PluginCapability).
/// The value is cached from the descriptor at load time and remains available
/// even after the plugin is unloaded, so event handlers can safely query it.
QS_API uint32_t qs_plugin_state_capabilities(const Qs_PluginState *state);

/// Looks up capabilities by plugin id. Returns 0 if the id is unknown.
QS_API uint32_t qs_plugin_capabilities_for_id(const Qs_PluginManager *pm, const char *id);

/// Unloads a currently loaded plugin and immediately reloads it from disk.
/// Useful for hot-reload during development.  Returns false if the plugin id
/// is unknown or if the reload fails (the plugin is left unloaded in that case).
QS_API bool qs_plugin_reload(Qs_PluginManager *pm, const char *id);

#endif
