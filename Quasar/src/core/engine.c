#include "quasar.h"
#include "qs_plugin.h"
#include "qs_ext.h"
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
Qs_SystemDesc qs_asset_system_desc(void);
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
    Qs_ExtRegistry*   extensions;
    Qs_Project*       project;
};

static double engine_clock(void)
{
#ifdef _WIN32
    static LARGE_INTEGER freq = {0};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER counter;
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

/* Static wrapper functions so Causality allocations are tracked as QS_MEM_UI. */
static void *engine_ui_malloc (size_t sz)            { return qs_malloc(sz, QS_MEM_UI); }
static void *engine_ui_calloc (size_t n, size_t sz)  { return qs_calloc(n, sz, QS_MEM_UI); }
static void *engine_ui_realloc(void *p, size_t sz)   { return qs_realloc(p, sz, QS_MEM_UI); }

Qs_Engine* qs_engine_create(const Qs_EngineDesc* desc) {
    if (!desc) return NULL;

    qs_mem_init();

    /* Route all Causality internal allocations through the memory system. */
    ca_set_allocator(engine_ui_malloc, engine_ui_calloc, engine_ui_realloc, qs_free);

    Qs_Engine* engine = qs_calloc(1, sizeof(Qs_Engine), QS_MEM_ENGINE);
    if (!engine) return NULL;

    engine->last_time = engine_clock();
    engine->dt        = 1.0f / 60.0f;

    if (desc->app_name) {
        size_t len = strlen(desc->app_name);
        engine->app_name = qs_malloc(len + 1, QS_MEM_ENGINE);
        if (engine->app_name)
            memcpy(engine->app_name, desc->app_name, len + 1);
    }

    engine->version_major = desc->version_major;
    engine->version_minor = desc->version_minor;
    engine->version_patch = desc->version_patch;

    /* ---- Create Causality instance ---- */
    engine->ca_instance = ca_instance_create(&(Ca_InstanceDesc){
        .app_name          = desc->app_name,
        .font_size_px      = desc->font_size_px > 0 ? desc->font_size_px : 14.0f,
        .default_ui_scale  = desc->ui_scale,
    });
    if (!engine->ca_instance) {
        qs_free(engine->app_name);
        qs_free(engine);
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
        qs_free(engine->app_name);
        qs_free(engine);
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

    /* ---- Extension registry ---- */
    engine->extensions = qs_ext_registry_create();
    if (!engine->extensions) goto fail;

    /* ---- Plugin loading ----
       Plugins may register additional systems here before Scene is initialised
       so that dependency order is preserved. */
    engine->plugins = qs_plugin_manager_create(engine, desc->plugin_dir);
    if (engine->plugins)
        qs_plugin_manager_scan(engine->plugins);

    /* ---- Built-in PBR renderer backend ----
       Registered before the render system so render_sys_init finds it. */
    extern const Qs_RendererBackend pbr_renderer_backend;
    qs_renderer_backend_register(&pbr_renderer_backend);

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

    /* ---- Asset system (depends on texture/mesh/material for GPU upload) ---- */
    Qs_SystemDesc asset_desc = qs_asset_system_desc();
    if (!qs_system_register(engine->systems, &asset_desc)) goto fail;

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
    if (engine->extensions) qs_ext_registry_destroy(engine->extensions);
    if (engine->systems) qs_system_manager_destroy(engine->systems);
    ca_instance_destroy(engine->ca_instance);
    qs_free(engine->app_name);
    qs_free(engine);
    return NULL;
}

void qs_engine_destroy(Qs_Engine* engine) {
    if (!engine) return;
    qs_event_fire(qs_engine_event_bus(engine), QS_EVENT_ENGINE_SHUTDOWN, NULL, 0);
    /* Plugins must clean up render nodes / pipelines before systems tear down. */
    if (engine->plugins) qs_plugin_manager_destroy(engine->plugins);
    if (engine->extensions) qs_ext_registry_destroy(engine->extensions);
    qs_system_manager_destroy(engine->systems);
    if (engine->stylesheet) ca_css_destroy(engine->stylesheet);
    ca_instance_destroy(engine->ca_instance);
    qs_free(engine->app_name);
    qs_free(engine);
    qs_mem_shutdown();
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

void qs_engine_set_project(Qs_Engine* engine, Qs_Project* project) {
    if (engine) engine->project = project;
}

Qs_Project* qs_engine_project(Qs_Engine* engine) {
    return engine ? engine->project : NULL;
}

Qs_ExtRegistry* qs_engine_ext_registry(const Qs_Engine* engine) {
    return engine ? engine->extensions : NULL;
}

const char* qs_version_string(void) {
    static char version[32];
    snprintf(version, sizeof(version), "%d.%d.%d",
             QS_VERSION_MAJOR, QS_VERSION_MINOR, QS_VERSION_PATCH);
    return version;
}

/* ================================================================
   SYSTEM MANAGER  (was qs_system.c)
   ================================================================ */

#include "qs_log.h"
#include <string.h>

struct Qs_System {
    char    *name;
    void    *data;
    bool   (*init)(Qs_System *, Qs_Engine *);
    void   (*shutdown)(Qs_System *, Qs_Engine *);
    void   (*update)(Qs_System *, Qs_Engine *, float);
};

struct Qs_SystemManager {
    Qs_Engine  *engine;
    Qs_System **systems;
    uint32_t    count;
    uint32_t    capacity;
};

Qs_SystemManager *qs_system_manager_create(Qs_Engine *engine)
{
    Qs_SystemManager *mgr = qs_calloc(1, sizeof(Qs_SystemManager), QS_MEM_ENGINE);
    if (!mgr) return NULL;
    mgr->engine   = engine;
    mgr->capacity = 16;
    mgr->systems  = qs_calloc(mgr->capacity, sizeof(Qs_System *), QS_MEM_ENGINE);
    if (!mgr->systems) { qs_free(mgr); return NULL; }
    return mgr;
}

void qs_system_manager_destroy(Qs_SystemManager *manager)
{
    if (!manager) return;

    for (uint32_t i = manager->count; i > 0; i--) {
        Qs_System *s = manager->systems[i - 1];
        if (s->shutdown) s->shutdown(s, manager->engine);
        qs_log(QS_LOG_DEBUG, "System '%s' shut down", s->name);
        qs_free(s->name);
        qs_free(s->data);
        qs_free(s);
    }

    qs_free(manager->systems);
    qs_free(manager);
}

Qs_System *qs_system_register(Qs_SystemManager *manager, const Qs_SystemDesc *desc)
{
    if (!manager || !desc || !desc->name) return NULL;

    Qs_System *s = qs_calloc(1, sizeof(Qs_System), QS_MEM_ENGINE);
    if (!s) return NULL;

    size_t name_len = strlen(desc->name);
    s->name = qs_malloc(name_len + 1, QS_MEM_ENGINE);
    if (!s->name) { qs_free(s); return NULL; }
    memcpy(s->name, desc->name, name_len + 1);

    s->init     = desc->init;
    s->shutdown = desc->shutdown;
    s->update   = desc->update;

    if (desc->data_size > 0) {
        s->data = qs_calloc(1, desc->data_size, QS_MEM_ENGINE);
        if (!s->data) { qs_free(s->name); qs_free(s); return NULL; }
    }

    if (s->init && !s->init(s, manager->engine)) {
        qs_free(s->data);
        qs_free(s->name);
        qs_free(s);
        return NULL;
    }

    if (manager->count == manager->capacity) {
        uint32_t new_cap = manager->capacity * 2;
        Qs_System **new_arr = qs_realloc(manager->systems, new_cap * sizeof(Qs_System *), QS_MEM_ENGINE);
        if (!new_arr) {
            if (s->shutdown) s->shutdown(s, manager->engine);
            qs_free(s->data);
            qs_free(s->name);
            qs_free(s);
            return NULL;
        }
        manager->systems  = new_arr;
        manager->capacity = new_cap;
    }

    manager->systems[manager->count++] = s;

    qs_log(QS_LOG_DEBUG, "System '%s' registered", s->name);
    return s;
}

void qs_system_unregister(Qs_SystemManager *manager, Qs_System *system)
{
    if (!manager || !system) return;

    for (uint32_t i = 0; i < manager->count; i++) {
        if (manager->systems[i] == system) {
            if (system->shutdown) system->shutdown(system, manager->engine);
            memmove(&manager->systems[i], &manager->systems[i + 1],
                    (manager->count - i - 1) * sizeof(Qs_System *));
            manager->count--;
            qs_log(QS_LOG_DEBUG, "System '%s' unregistered", system->name);
            qs_free(system->name);
            qs_free(system->data);
            qs_free(system);
            return;
        }
    }
}

void qs_system_manager_update(Qs_SystemManager *manager, float dt)
{
    if (!manager) return;
    for (uint32_t i = 0; i < manager->count; i++) {
        Qs_System *s = manager->systems[i];
        if (s->update) s->update(s, manager->engine, dt);
    }
}

void *qs_system_data(Qs_System *system)
{
    return system ? system->data : NULL;
}

const char *qs_system_name(const Qs_System *system)
{
    return system ? system->name : NULL;
}

Qs_System *qs_system_find(Qs_SystemManager *manager, const char *name)
{
    if (!manager || !name) return NULL;
    for (uint32_t i = 0; i < manager->count; i++) {
        if (strcmp(manager->systems[i]->name, name) == 0)
            return manager->systems[i];
    }
    return NULL;
}

uint32_t qs_system_count(const Qs_SystemManager *manager)
{
    return manager ? manager->count : 0;
}

/* ================================================================
   EXTENSION REGISTRY  (was qs_ext.c)
   ================================================================ */


#include <string.h>

/* ================================================================
   INTERNAL DATA STRUCTURES
   ================================================================ */

#define MAX_POINT_NAME  64
#define MAX_ENTRIES     32
#define MAX_POINTS      64

typedef struct ExtEntry {
    const void *iface;
    void       *data;
    uint32_t    point_idx;   /* index into registry's points array */
    uint32_t    entry_idx;   /* index within the point's entries   */
} ExtEntry;

typedef struct ExtPoint {
    char      name[MAX_POINT_NAME];
    ExtEntry  entries[MAX_ENTRIES];
    uint32_t  count;
} ExtPoint;

struct Qs_ExtRegistry {
    ExtPoint  points[MAX_POINTS];
    uint32_t  point_count;
};

/* Qs_Extension is just an alias for ExtEntry so the handle can reach
   back into the registry for unregistration. */
struct Qs_Extension {
    Qs_ExtRegistry *registry;
    uint32_t        point_idx;
    uint32_t        entry_idx;
};

/* Pool of extension handles — avoids per-handle malloc. */
#define MAX_HANDLES (MAX_POINTS * MAX_ENTRIES)
static struct Qs_Extension s_handles[MAX_HANDLES];
static uint32_t            s_handle_count;
static uint32_t            s_free_list[MAX_HANDLES];
static uint32_t            s_free_count;

/* ================================================================
   HELPERS
   ================================================================ */

static ExtPoint *find_point(const Qs_ExtRegistry *reg, const char *name)
{
    for (uint32_t i = 0; i < reg->point_count; i++) {
        if (strcmp(reg->points[i].name, name) == 0)
            return (ExtPoint *)&reg->points[i];
    }
    return NULL;
}

static ExtPoint *find_or_create_point(Qs_ExtRegistry *reg, const char *name)
{
    ExtPoint *p = find_point(reg, name);
    if (p) return p;
    if (reg->point_count >= MAX_POINTS) return NULL;
    p = &reg->points[reg->point_count++];
    memset(p, 0, sizeof(*p));
    snprintf(p->name, sizeof(p->name), "%s", name);
    return p;
}

/* ================================================================
   REGISTRY LIFECYCLE
   ================================================================ */

Qs_ExtRegistry *qs_ext_registry_create(void)
{
    Qs_ExtRegistry *reg = qs_calloc(1, sizeof(Qs_ExtRegistry), QS_MEM_ENGINE);
    return reg;
}

void qs_ext_registry_destroy(Qs_ExtRegistry *reg)
{
    if (!reg) return;
    /* Invalidate any outstanding handles pointing at this registry */
    for (uint32_t i = 0; i < s_handle_count; i++) {
        if (s_handles[i].registry == reg)
            s_handles[i].registry = NULL;
    }
    qs_free(reg);
}

/* ================================================================
   REGISTER / UNREGISTER
   ================================================================ */

Qs_Extension *qs_ext_register(Qs_ExtRegistry *reg, const char *point,
                              const void *iface, void *data)
{
    if (!reg || !point || !iface) return NULL;

    ExtPoint *p = find_or_create_point(reg, point);
    if (!p || p->count >= MAX_ENTRIES) return NULL;

    uint32_t idx = p->count++;
    p->entries[idx].iface     = iface;
    p->entries[idx].data      = data;
    p->entries[idx].point_idx = (uint32_t)(p - reg->points);
    p->entries[idx].entry_idx = idx;

    /* Allocate a handle (prefer recycled slot from freelist) */
    struct Qs_Extension *h;
    if (s_free_count > 0) {
        h = &s_handles[s_free_list[--s_free_count]];
    } else {
        if (s_handle_count >= MAX_HANDLES) return NULL;
        h = &s_handles[s_handle_count++];
    }
    h->registry  = reg;
    h->point_idx = p->entries[idx].point_idx;
    h->entry_idx = idx;
    return h;
}

void qs_ext_unregister(Qs_Extension *ext)
{
    if (!ext || !ext->registry) return;

    Qs_ExtRegistry *reg = ext->registry;
    if (ext->point_idx >= reg->point_count) return;

    ExtPoint *p = &reg->points[ext->point_idx];
    if (ext->entry_idx >= p->count) return;

    /* Swap-remove the entry */
    uint32_t last = p->count - 1;
    if (ext->entry_idx != last) {
        p->entries[ext->entry_idx] = p->entries[last];
        /* Update any handle that pointed at the moved entry */
        for (uint32_t i = 0; i < s_handle_count; i++) {
            if (s_handles[i].registry == reg &&
                s_handles[i].point_idx == ext->point_idx &&
                s_handles[i].entry_idx == last)
            {
                s_handles[i].entry_idx = ext->entry_idx;
                break;
            }
        }
    }
    p->count--;
    ext->registry = NULL;

    /* Return handle slot to freelist */
    uint32_t slot = (uint32_t)(ext - s_handles);
    if (s_free_count < MAX_HANDLES)
        s_free_list[s_free_count++] = slot;
}

/* ================================================================
   QUERIES
   ================================================================ */

uint32_t qs_ext_count(const Qs_ExtRegistry *reg, const char *point)
{
    if (!reg || !point) return 0;
    const ExtPoint *p = find_point(reg, point);
    return p ? p->count : 0;
}

const void *qs_ext_interface(const Qs_ExtRegistry *reg,
                             const char *point, uint32_t index)
{
    if (!reg || !point) return NULL;
    const ExtPoint *p = find_point(reg, point);
    if (!p || index >= p->count) return NULL;
    return p->entries[index].iface;
}

void *qs_ext_data(const Qs_ExtRegistry *reg,
                  const char *point, uint32_t index)
{
    if (!reg || !point) return NULL;
    const ExtPoint *p = find_point(reg, point);
    if (!p || index >= p->count) return NULL;
    return p->entries[index].data;
}

/* ================================================================
   ENGINE CONVENIENCE WRAPPERS
   ================================================================ */

Qs_ExtRegistry *qs_engine_ext_registry(const Qs_Engine *engine);

Qs_Extension *qs_engine_ext_register(Qs_Engine *engine, const char *point,
                                     const void *iface, void *data)
{
    Qs_ExtRegistry *reg = qs_engine_ext_registry(engine);
    return reg ? qs_ext_register(reg, point, iface, data) : NULL;
}

uint32_t qs_engine_ext_count(const Qs_Engine *engine, const char *point)
{
    const Qs_ExtRegistry *reg = qs_engine_ext_registry(engine);
    return reg ? qs_ext_count(reg, point) : 0;
}

const void *qs_engine_ext_interface(const Qs_Engine *engine,
                                   const char *point, uint32_t index)
{
    const Qs_ExtRegistry *reg = qs_engine_ext_registry(engine);
    return reg ? qs_ext_interface(reg, point, index) : NULL;
}

void *qs_engine_ext_data(const Qs_Engine *engine,
                         const char *point, uint32_t index)
{
    const Qs_ExtRegistry *reg = qs_engine_ext_registry(engine);
    return reg ? qs_ext_data(reg, point, index) : NULL;
}
