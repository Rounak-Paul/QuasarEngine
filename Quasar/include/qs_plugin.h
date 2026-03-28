#ifndef QS_PLUGIN_H
#define QS_PLUGIN_H

#include <stdint.h>
#include <stdbool.h>

typedef struct Qs_Engine        Qs_Engine;
typedef struct Qs_PluginManager Qs_PluginManager;
typedef struct Qs_PluginState   Qs_PluginState;

/* ================================================================
   PLUGIN ABI
   ================================================================ */

/// Current API version. Plugins must set api_version to this value.
/// A plugin with a mismatched version is rejected at load time.
#define QS_PLUGIN_API_VERSION  1

/// Symbol name every plugin shared library must export.
#define QS_PLUGIN_ENTRY_SYMBOL "qs_plugin_entry"

/// Cross-platform export macro for the plugin entry point.
#ifdef _WIN32
  #define QS_PLUGIN_EXPORT __declspec(dllexport)
#else
  #define QS_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/// Describes a plugin and provides its lifecycle callbacks.
/// Plugins return a pointer to a static instance of this struct from
/// qs_plugin_entry().  The pointer must remain valid for the lifetime of
/// the loaded library.
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
    /// Register engine systems, render nodes, component types, etc. here.
    /// May be NULL.
    void (*on_load)(Qs_Engine *engine);

    /// Called before the plugin library is unloaded.
    /// Remove render nodes, release plugin-owned resources here.
    /// Called before engine systems are shut down.
    /// May be NULL.
    void (*on_unload)(Qs_Engine *engine);

    /// Called each editor frame to draw per-plugin inspector UI.
    /// Only called when an editor window is present.  May be NULL.
    void (*on_inspector_gui)(Qs_Engine *engine);

    /// Called each editor frame to draw items inside the Plugins menu.
    /// May be NULL.
    void (*on_menu_gui)(Qs_Engine *engine);
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
Qs_PluginManager *qs_plugin_manager_create(Qs_Engine *engine,
                                            const char *plugin_dir);

/// Scans the plugin directory, loads all enabled plugins, and persists state.
/// Call once after the core engine systems (Log, Job, Event, Input) are
/// registered and before Scene is registered, so that renderer plugins can
/// register their systems in the correct order.
void qs_plugin_manager_scan(Qs_PluginManager *pm);

/// Calls on_unload for every loaded plugin in reverse load order,
/// saves plugin state to the user config directory, and frees the manager.
void qs_plugin_manager_destroy(Qs_PluginManager *pm);

/// Enables a plugin by id and loads it if not already loaded.
/// Returns false if the plugin id is unknown.
bool qs_plugin_enable(Qs_PluginManager *pm, const char *id);

/// Disables a plugin by id and unloads it if currently loaded.
/// Returns false if the plugin id is unknown.
bool qs_plugin_disable(Qs_PluginManager *pm, const char *id);

/// Returns the number of discovered plugins (enabled or disabled).
uint32_t qs_plugin_count(const Qs_PluginManager *pm);

/// Returns the state record for the plugin at index idx.
const Qs_PluginState *qs_plugin_state_at(const Qs_PluginManager *pm,
                                          uint32_t idx);

/// Returns the descriptor of a plugin state record.  NULL if not loaded.
const Qs_PluginDesc *qs_plugin_state_desc(const Qs_PluginState *state);

/// Returns true if the plugin is currently enabled.
bool qs_plugin_state_enabled(const Qs_PluginState *state);

/// Returns true if the plugin library is currently loaded in memory.
bool qs_plugin_state_loaded(const Qs_PluginState *state);

/// Returns the file-system path of the plugin shared library.
const char *qs_plugin_state_path(const Qs_PluginState *state);

/// Returns the plugin id (available even before the library is loaded,
/// from the persisted state).
const char *qs_plugin_state_id(const Qs_PluginState *state);

#endif
