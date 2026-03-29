#include "quasar.h"
#include "qs_plugin.h"
#include "qs_dylib.h"

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#else
  #include <time.h>
#endif

#define QS_VERSION_MAJOR 0
#define QS_VERSION_MINOR 1
#define QS_VERSION_PATCH 0

/* Internal system descriptors — not part of the public API. */
Qs_SystemDesc qs_log_system_desc(void);
Qs_SystemDesc qs_job_system_desc(void);
Qs_SystemDesc qs_event_system_desc(void);
Qs_SystemDesc qs_input_system_desc(void);
Qs_SystemDesc qs_render_system_desc(void);
Qs_SystemDesc qs_texture_system_desc(void);
Qs_SystemDesc qs_mesh_system_desc(void);
Qs_SystemDesc qs_material_system_desc(void);
Qs_SystemDesc qs_light_system_desc(void);
Qs_SystemDesc qs_scene_system_desc(void);

struct Qs_Engine {
    char*             app_name;
    uint32_t          version_major;
    uint32_t          version_minor;
    uint32_t          version_patch;
    Qs_SystemManager* systems;
    Qs_PluginManager* plugins;
    Ca_Instance*      ca_instance;
    Ca_Window*        window;
    Ca_Stylesheet*    stylesheet;
    double            last_time;
    float             dt;
    Qs_FrameFn        on_frame;
    void*             frame_userdata;
};

static double engine_clock(void)
{
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
#endif
}

/* Internal update — computes dt and ticks all systems. */
static void engine_update(Qs_Engine* engine)
{
    double now = engine_clock();
    engine->dt = (float)(now - engine->last_time);
    engine->last_time = now;
    if (engine->dt <= 0.0f) engine->dt = 1.0f / 60.0f;
    if (engine->dt > 0.25f) engine->dt = 0.25f;  /* clamp to avoid spiral */
    qs_system_manager_update(engine->systems, engine->dt);
}

/* Window frame callback — called each tick by Causality. */
static void engine_frame(void *userdata)
{
    Qs_Engine *engine = userdata;
    engine_update(engine);
    if (engine->on_frame)
        engine->on_frame(engine, engine->frame_userdata);
    /* Clear per-frame input accumulators after all consumers have run. */
    qs_input_end_frame();
}

Qs_Engine* qs_engine_create(const Qs_EngineDesc* desc) {
    if (!desc) return NULL;

    Qs_Engine* engine = calloc(1, sizeof(Qs_Engine));
    if (!engine) return NULL;

    engine->last_time = engine_clock();
    engine->dt        = 1.0f / 60.0f;

    if (desc->app_name) {
        size_t len = strlen(desc->app_name);
        engine->app_name = malloc(len + 1);
        if (engine->app_name)
            memcpy(engine->app_name, desc->app_name, len + 1);
    }

    engine->version_major = desc->version_major;
    engine->version_minor = desc->version_minor;
    engine->version_patch = desc->version_patch;

    /* ---- Create Causality instance ---- */
    engine->ca_instance = ca_instance_create(&(Ca_InstanceDesc){
        .app_name     = desc->app_name,
        .font_size_px = desc->font_size_px > 0 ? desc->font_size_px : 14.0f,
    });
    if (!engine->ca_instance) {
        free(engine->app_name);
        free(engine);
        return NULL;
    }

    /* Game engines need continuous frame rendering so that held-key movement
       and per-frame logic advance every tick rather than only on input events. */
    ca_instance_set_continuous(engine->ca_instance, true);

    /* ---- Create window ---- */
    engine->window = ca_window_create(engine->ca_instance, &(Ca_WindowDesc){
        .title  = desc->app_name,
        .width  = desc->window_width  > 0 ? desc->window_width  : 1280,
        .height = desc->window_height > 0 ? desc->window_height : 720,
    });
    if (!engine->window) {
        ca_instance_destroy(engine->ca_instance);
        free(engine->app_name);
        free(engine);
        return NULL;
    }

    ca_window_set_on_frame(engine->window, engine_frame, engine);

    /* ---- System manager ---- */
    engine->systems = qs_system_manager_create(engine);
    if (!engine->systems) goto fail;

    /* ---- Core systems (order matters) ---- */
    Qs_SystemDesc log_desc = qs_log_system_desc();
    if (!qs_system_register(engine->systems, &log_desc)) goto fail;

    Qs_SystemDesc job_desc = qs_job_system_desc();
    if (!qs_system_register(engine->systems, &job_desc)) goto fail;

    Qs_SystemDesc event_desc = qs_event_system_desc();
    if (!qs_system_register(engine->systems, &event_desc)) goto fail;

    Qs_SystemDesc input_desc = qs_input_system_desc();
    if (!qs_system_register(engine->systems, &input_desc)) goto fail;

    /* ---- Plugin loading ----
       Plugins may register additional systems (e.g. renderer) here,
       before Scene is initialised so dependency order is preserved. */
    engine->plugins = qs_plugin_manager_create(engine, desc->plugin_dir);
    if (engine->plugins)
        qs_plugin_manager_scan(engine->plugins);

    /* ---- Renderer systems ---- */
    Qs_SystemDesc render_desc = qs_render_system_desc();
    if (!qs_system_register(engine->systems, &render_desc)) goto fail;

    Qs_SystemDesc texture_desc = qs_texture_system_desc();
    if (!qs_system_register(engine->systems, &texture_desc)) goto fail;

    Qs_SystemDesc mesh_desc = qs_mesh_system_desc();
    if (!qs_system_register(engine->systems, &mesh_desc)) goto fail;

    Qs_SystemDesc material_desc = qs_material_system_desc();
    if (!qs_system_register(engine->systems, &material_desc)) goto fail;

    Qs_SystemDesc light_desc = qs_light_system_desc();
    if (!qs_system_register(engine->systems, &light_desc)) goto fail;

    /* ---- Scene system (depends on renderer being registered first) ---- */
    Qs_SystemDesc scene_desc = qs_scene_system_desc();
    if (!qs_system_register(engine->systems, &scene_desc)) goto fail;

    QS_LOG_INFO("Quasar Engine %s initialized (%s) [%u worker threads]",
                qs_version_string(),
                engine->app_name ? engine->app_name : "unnamed",
                qs_job_system_thread_count(qs_engine_job_system(engine)));

    qs_event_fire(qs_engine_event_bus(engine), QS_EVENT_ENGINE_INIT, NULL, 0);
    return engine;

fail:
    if (engine->plugins) qs_plugin_manager_destroy(engine->plugins);
    if (engine->systems) qs_system_manager_destroy(engine->systems);
    ca_instance_destroy(engine->ca_instance);
    free(engine->app_name);
    free(engine);
    return NULL;
}

void qs_engine_destroy(Qs_Engine* engine) {
    if (!engine) return;
    qs_event_fire(qs_engine_event_bus(engine), QS_EVENT_ENGINE_SHUTDOWN, NULL, 0);
    /* Plugins must clean up render nodes / pipelines before systems tear down. */
    if (engine->plugins) qs_plugin_manager_destroy(engine->plugins);
    qs_system_manager_destroy(engine->systems);
    if (engine->stylesheet) ca_css_destroy(engine->stylesheet);
    ca_instance_destroy(engine->ca_instance);
    free(engine->app_name);
    free(engine);
}

int qs_engine_run(Qs_Engine* engine) {
    if (!engine) return 1;
    while (ca_instance_tick(engine->ca_instance)) { }
    return 0;
}

Ca_Window* qs_engine_window(Qs_Engine* engine) {
    return engine ? engine->window : NULL;
}

Ca_Instance* qs_engine_ca_instance(Qs_Engine* engine) {
    return engine ? engine->ca_instance : NULL;
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

float qs_engine_dt(const Qs_Engine* engine) {
    return engine ? engine->dt : 0.0f;
}

void qs_engine_set_on_frame(Qs_Engine* engine, Qs_FrameFn fn, void* user_data) {
    if (!engine) return;
    engine->on_frame       = fn;
    engine->frame_userdata = user_data;
}

void qs_engine_set_stylesheet(Qs_Engine* engine, const char* css) {
    if (!engine || !css) return;
    if (engine->stylesheet) ca_css_destroy(engine->stylesheet);
    engine->stylesheet = ca_css_parse(css);
    if (engine->stylesheet)
        ca_instance_set_stylesheet(engine->ca_instance, engine->stylesheet);
}

void qs_engine_set_event_handler(Qs_Engine* engine, Ca_EventType type,
                                  Ca_EventFn fn, void* user_data) {
    if (!engine) return;
    ca_event_set_handler(engine->ca_instance, type, fn, user_data);
}

void qs_engine_request_exit(Qs_Engine* engine) {
    if (engine && engine->window)
        ca_window_close(engine->window);
}

void qs_engine_wake(void) {
    ca_instance_wake();
}

Qs_SystemManager* qs_engine_systems(Qs_Engine* engine) {
    return engine ? engine->systems : NULL;
}

Qs_PluginManager* qs_engine_plugin_manager(Qs_Engine* engine) {
    return engine ? engine->plugins : NULL;
}

const char* qs_version_string(void) {
    static char version[32];
    snprintf(version, sizeof(version), "%d.%d.%d",
             QS_VERSION_MAJOR, QS_VERSION_MINOR, QS_VERSION_PATCH);
    return version;
}
