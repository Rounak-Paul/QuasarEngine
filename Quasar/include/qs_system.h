#ifndef QS_SYSTEM_H
#define QS_SYSTEM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct Qs_Engine        Qs_Engine;
typedef struct Qs_SystemManager Qs_SystemManager;
typedef struct Qs_System        Qs_System;

/// Descriptor for registering an engine system.
typedef struct Qs_SystemDesc {
    const char *name;       ///< Unique identifier (e.g. "Texture", "Mesh", "Material").
    size_t      data_size;  ///< Bytes to allocate for system-specific data (zeroed).

    /// Called once when the system is registered. Return false to abort.
    bool (*init)(Qs_System *system, Qs_Engine *engine);

    /// Called when the system is shut down (engine destroy or explicit unregister).
    void (*shutdown)(Qs_System *system, Qs_Engine *engine);

    /// Called each frame. NULL to skip.
    void (*update)(Qs_System *system, Qs_Engine *engine, float dt);
} Qs_SystemDesc;

/// Creates a system manager. Owned by the engine — do not destroy manually.
Qs_SystemManager *qs_system_manager_create(Qs_Engine *engine);

/// Shuts down all systems in reverse registration order and frees the manager.
void qs_system_manager_destroy(Qs_SystemManager *manager);

/// Registers and initializes a new system. Returns the handle, or NULL on failure.
Qs_System *qs_system_register(Qs_SystemManager *manager, const Qs_SystemDesc *desc);

/// Unregisters a system and calls its shutdown callback.
void qs_system_unregister(Qs_SystemManager *manager, Qs_System *system);

/// Updates all active systems in registration order.
void qs_system_manager_update(Qs_SystemManager *manager, float dt);

/// Returns a pointer to the system's allocated data block (data_size bytes).
void *qs_system_data(Qs_System *system);

/// Returns the system's name.
const char *qs_system_name(const Qs_System *system);

/// Finds a registered system by name. Returns NULL if not found.
Qs_System *qs_system_find(Qs_SystemManager *manager, const char *name);

/// Returns how many systems are currently registered.
uint32_t qs_system_count(const Qs_SystemManager *manager);

#endif
