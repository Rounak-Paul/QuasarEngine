#include "quasar.h"

#define QS_VERSION_MAJOR 0
#define QS_VERSION_MINOR 1
#define QS_VERSION_PATCH 0

struct Qs_Engine {
    char*             app_name;
    uint32_t          version_major;
    uint32_t          version_minor;
    uint32_t          version_patch;
    Qs_SystemManager* systems;
};

Qs_Engine* qs_engine_create(const Qs_EngineDesc* desc) {
    Qs_Engine* engine = calloc(1, sizeof(Qs_Engine));
    if (!engine) return NULL;

    if (desc && desc->app_name) {
        size_t len = strlen(desc->app_name);
        engine->app_name = malloc(len + 1);
        if (engine->app_name) {
            memcpy(engine->app_name, desc->app_name, len + 1);
        }
    }

    if (desc) {
        engine->version_major = desc->version_major;
        engine->version_minor = desc->version_minor;
        engine->version_patch = desc->version_patch;
    }

    engine->systems = qs_system_manager_create(engine);
    if (!engine->systems) {
        free(engine->app_name);
        free(engine);
        return NULL;
    }

    Qs_SystemDesc log_desc = qs_log_system_desc();
    if (!qs_system_register(engine->systems, &log_desc)) {
        qs_system_manager_destroy(engine->systems);
        free(engine->app_name);
        free(engine);
        return NULL;
    }

    Qs_SystemDesc job_desc = qs_job_system_desc();
    if (!qs_system_register(engine->systems, &job_desc)) {
        qs_system_manager_destroy(engine->systems);
        free(engine->app_name);
        free(engine);
        return NULL;
    }

    Qs_SystemDesc event_desc = qs_event_system_desc();
    if (!qs_system_register(engine->systems, &event_desc)) {
        qs_system_manager_destroy(engine->systems);
        free(engine->app_name);
        free(engine);
        return NULL;
    }

    Qs_SystemDesc input_desc = qs_input_system_desc();
    if (!qs_system_register(engine->systems, &input_desc)) {
        qs_system_manager_destroy(engine->systems);
        free(engine->app_name);
        free(engine);
        return NULL;
    }

    QS_LOG_INFO("Quasar Engine %s initialized (%s) [%u worker threads]",
                qs_version_string(),
                engine->app_name ? engine->app_name : "unnamed",
                qs_job_system_thread_count(qs_engine_job_system(engine)));

    qs_event_fire(qs_engine_event_bus(engine), QS_EVENT_ENGINE_INIT, NULL, 0);
    return engine;
}

void qs_engine_destroy(Qs_Engine* engine) {
    if (!engine) return;
    qs_event_fire(qs_engine_event_bus(engine), QS_EVENT_ENGINE_SHUTDOWN, NULL, 0);
    qs_system_manager_destroy(engine->systems);
    free(engine->app_name);
    free(engine);
}

Qs_EventBus* qs_engine_event_bus(Qs_Engine* engine) {
    if (!engine) return NULL;
    Qs_System *s = qs_system_find(engine->systems, "Event");
    return s ? (Qs_EventBus *)qs_system_data(s) : NULL;
}

Qs_JobSystem* qs_engine_job_system(Qs_Engine* engine) {
    if (!engine) return NULL;
    Qs_System *s = qs_system_find(engine->systems, "Job");
    if (!s) return NULL;
    Qs_JobSystem **slot = (Qs_JobSystem **)qs_system_data(s);
    return slot ? *slot : NULL;
}

Qs_SystemManager* qs_engine_systems(Qs_Engine* engine) {
    return engine ? engine->systems : NULL;
}

void qs_engine_update(Qs_Engine* engine, float dt) {
    if (!engine) return;
    qs_system_manager_update(engine->systems, dt);
}

const char* qs_version_string(void) {
    static char version[32];
    snprintf(version, sizeof(version), "%d.%d.%d",
             QS_VERSION_MAJOR, QS_VERSION_MINOR, QS_VERSION_PATCH);
    return version;
}
