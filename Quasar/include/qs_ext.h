#ifndef QS_EXT_H
#define QS_EXT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct Qs_Engine      Qs_Engine;
typedef struct Qs_ExtRegistry Qs_ExtRegistry;
typedef struct Qs_Extension   Qs_Extension;

/* ================================================================
   EXTENSION POINT REGISTRY
   ================================================================

   A generic, string-keyed registry that lets plugins (or engine
   subsystems) register *extensions* at named *extension points*.

   Each extension is a pair:  (interface vtable, user data).
   The interface pointer's concrete type depends on the extension
   point; consumers cast it to the appropriate struct.

   Plugins register extensions in on_load() and unregister them in
   on_unload().  The editor or engine queries extensions by point
   name to discover available capabilities at runtime.

   Well-known extension point names and their corresponding
   interface types are defined below the core API.
   ================================================================ */

/// Create a new, empty extension registry.
Qs_ExtRegistry *qs_ext_registry_create(void);

/// Destroy the registry and free all internal storage.
/// Any outstanding Qs_Extension handles become invalid.
void qs_ext_registry_destroy(Qs_ExtRegistry *reg);

/// Register an extension at the named extension point.
///
/// @param reg    The registry (owned by the engine).
/// @param point  Extension point name (e.g. "editor.toolbar").
/// @param iface  Pointer to an interface vtable.  The concrete type
///               depends on the extension point.  Must remain valid
///               until unregistered.
/// @param data   Opaque user data forwarded to interface callbacks.
/// @return       Handle for unregistering, or NULL on failure.
Qs_Extension *qs_ext_register(Qs_ExtRegistry *reg, const char *point,
                              const void *iface, void *data);

/// Unregister a previously registered extension.
void qs_ext_unregister(Qs_Extension *ext);

/// Number of extensions currently registered at the given point.
uint32_t qs_ext_count(const Qs_ExtRegistry *reg, const char *point);

/// Interface vtable for the i-th extension at a point.
/// Returns NULL if index is out of range or the point doesn't exist.
const void *qs_ext_interface(const Qs_ExtRegistry *reg,
                             const char *point, uint32_t index);

/// User data for the i-th extension at a point.
void *qs_ext_data(const Qs_ExtRegistry *reg,
                  const char *point, uint32_t index);

/* ================================================================
   ENGINE CONVENIENCE WRAPPERS
   ================================================================
   These forward to the registry owned by the engine so callers
   don't need to fetch it manually.
   ================================================================ */

/// Register an extension through the engine's registry.
Qs_Extension *qs_engine_ext_register(Qs_Engine *engine, const char *point,
                                     const void *iface, void *data);

/// Number of extensions at a point in the engine's registry.
uint32_t qs_engine_ext_count(const Qs_Engine *engine, const char *point);

/// Interface vtable for the i-th extension at a point (engine registry).
const void *qs_engine_ext_interface(const Qs_Engine *engine,
                                   const char *point, uint32_t index);

/// User data for the i-th extension at a point (engine registry).
void *qs_engine_ext_data(const Qs_Engine *engine,
                         const char *point, uint32_t index);

/* ================================================================
   WELL-KNOWN EXTENSION POINTS
   ================================================================
   Each point name has a corresponding interface struct that
   extensions at that point must implement.  Consumers cast the
   result of qs_ext_interface() to the correct type.
   ================================================================ */

typedef struct Qs_Mesh     Qs_Mesh;
typedef struct Qs_Material Qs_Material;
typedef struct Qs_Texture  Qs_Texture;
typedef struct Qs_Scene    Qs_Scene;

#ifndef QS_ENTITY_DEFINED
#define QS_ENTITY_DEFINED
typedef uint32_t Qs_Entity;
#define QS_ENTITY_INVALID UINT32_MAX
#endif

/* ---- editor.toolbar --------------------------------------------- */

#define QS_EXT_EDITOR_TOOLBAR  "editor.toolbar"

/// Maximum toolbar items a single extension may contribute per frame.
#define QS_TOOLBAR_MAX_ITEMS  8

/// A single icon button contributed to the editor toolbar.
typedef struct Qs_ToolbarItem {
    const char *icon;       ///< UTF-8 icon glyph (Codicon / NF icon)
    const char *id;         ///< Unique stable string id for state tracking
    const char *tooltip;    ///< Optional tooltip text (may be NULL)
    bool        active;     ///< Current active state
    /// Called when the button is clicked.  *active is toggled before the call.
    void (*on_click)(Qs_Engine *engine, bool *active);
} Qs_ToolbarItem;

/// Interface for "editor.toolbar" extensions.
typedef struct Qs_ToolbarExt {
    /// Fill items (up to *count) and set *count to the actual number written.
    void (*get_items)(void *data, Qs_Engine *engine,
                      Qs_ToolbarItem *items, int *count);
} Qs_ToolbarExt;

/* ---- editor.menu ------------------------------------------------ */

#define QS_EXT_EDITOR_MENU  "editor.menu"

/// Maximum menu items a single extension may contribute.
#define QS_MENU_MAX_ITEMS  16

typedef struct Ca_MenuItemDesc Ca_MenuItemDesc;

/// Interface for "editor.menu" extensions.
typedef struct Qs_MenuExt {
    /// Label for the sub-menu entry in the Plugins top-level menu.
    const char *label;
    /// Fill items (up to *count) and set *count to the actual number written.
    void (*get_items)(void *data, Qs_Engine *engine,
                      Ca_MenuItemDesc *items, int *count);
} Qs_MenuExt;

/* ---- editor.inspector ------------------------------------------- */

#define QS_EXT_EDITOR_INSPECTOR  "editor.inspector"

/// Interface for "editor.inspector" extensions.
/// Allows plugins to draw custom inspector sections for entities.
typedef struct Qs_InspectorExt {
    /// Human-readable label for this inspector section.
    const char *label;
    /// Draw the inspector UI.  Called each frame when an entity is selected.
    void (*draw)(void *data, Qs_Engine *engine, uint64_t entity);
} Qs_InspectorExt;

#endif
