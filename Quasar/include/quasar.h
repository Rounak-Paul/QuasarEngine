#ifndef QUASAR_H
#define QUASAR_H

#include <stdint.h>
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
#include "qs_primitives.h"
#include "qs_forward.h"
#include "qs_input.h"

typedef struct Qs_Engine Qs_Engine;

typedef struct Qs_EngineDesc {
    const char* app_name;
    uint32_t    version_major;
    uint32_t    version_minor;
    uint32_t    version_patch;
} Qs_EngineDesc;

/// Creates a new Quasar Engine instance.
Qs_Engine* qs_engine_create(const Qs_EngineDesc* desc);

/// Destroys a Quasar Engine instance and frees all resources.
void qs_engine_destroy(Qs_Engine* engine);

/// Returns the engine's event bus.
Qs_EventBus* qs_engine_event_bus(Qs_Engine* engine);

/// Returns the engine's job system.
Qs_JobSystem* qs_engine_job_system(Qs_Engine* engine);

/// Returns the engine's system manager.
Qs_SystemManager* qs_engine_systems(Qs_Engine* engine);

/// Updates all engine systems (call once per frame).
void qs_engine_update(Qs_Engine* engine, float dt);

/// Returns the engine version string.
const char* qs_version_string(void);

#endif
