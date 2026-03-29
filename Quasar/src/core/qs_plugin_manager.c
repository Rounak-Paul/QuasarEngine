#include "qs_plugin.h"
#include "qs_dylib.h"
#include "qs_log.h"
#include "causality.h"
#include "quasar.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#elif defined(__APPLE__)
  #include <sys/stat.h>
  #include <dirent.h>
#else
  #include <sys/stat.h>
  #include <dirent.h>
#endif

/* ================================================================
   PLATFORM
   ================================================================ */

#ifdef _WIN32
  #define QS_DYLIB_EXT ".dll"
  #define QS_PATH_SEP  "\\"
#elif defined(__APPLE__)
  #define QS_DYLIB_EXT ".dylib"
  #define QS_PATH_SEP  "/"
#else
  #define QS_DYLIB_EXT ".so"
  #define QS_PATH_SEP  "/"
#endif

#define QS_MAX_PATH 1024

/* ================================================================
   INTERNAL TYPES
   ================================================================ */

struct Qs_PluginState {
    char              path[QS_MAX_PATH];    /* file-system path to the .dll/.so */
    char              id[256];              /* from desc->id or persisted state  */
    bool              enabled;
    bool              loaded;
    Qs_Dylib         *lib;
    const Qs_PluginDesc *desc;
};

struct Qs_PluginManager {
    Qs_Engine        *engine;
    char              plugin_dir[QS_MAX_PATH];
    char              state_path[QS_MAX_PATH];
    Qs_PluginState   *entries;
    uint32_t          count;
    uint32_t          capacity;
};

/* ================================================================
   STATE FILE PATH
   ================================================================ */

static bool config_dir(char *out, size_t n)
{
#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    if (!appdata) return false;
    snprintf(out, n, "%s\\QuasarEngine", appdata);
    CreateDirectoryA(out, NULL);
    return true;
#elif defined(__APPLE__)
    const char *home = getenv("HOME");
    if (!home) return false;
    snprintf(out, n, "%s/Library/Application Support/QuasarEngine", home);
    mkdir(out, 0755);
    return true;
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg) {
        snprintf(out, n, "%s/QuasarEngine", xdg);
    } else {
        const char *home = getenv("HOME");
        if (!home) return false;
        snprintf(out, n, "%s/.config/QuasarEngine", home);
    }
    mkdir(out, 0755);
    return true;
#endif
}

/* ================================================================
   PERSIST / LOAD STATE  (cJSON)
   ================================================================ */

#include "cJSON.h"

static void load_state(Qs_PluginManager *pm)
{
    FILE *f = fopen(pm->state_path, "rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return; }

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return; }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return;

    cJSON *plugins = cJSON_GetObjectItem(root, "plugins");
    if (!cJSON_IsArray(plugins)) { cJSON_Delete(root); return; }

    cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, plugins) {
        cJSON *jid      = cJSON_GetObjectItem(entry, "id");
        cJSON *jenabled = cJSON_GetObjectItem(entry, "enabled");
        if (!cJSON_IsString(jid)) continue;

        const char *id      = jid->valuestring;
        bool        enabled = cJSON_IsBool(jenabled) ? cJSON_IsTrue(jenabled) : true;

        /* Update existing entry or pre-create a disabled record */
        bool found = false;
        for (uint32_t i = 0; i < pm->count; i++) {
            if (strcmp(pm->entries[i].id, id) == 0) {
                pm->entries[i].enabled = enabled;
                found = true;
                break;
            }
        }
        if (!found) {
            /* Create a placeholder entry for a plugin that isn't on disk yet */
            if (pm->count == pm->capacity) {
                uint32_t nc  = pm->capacity * 2;
                Qs_PluginState *na = realloc(pm->entries,
                                             nc * sizeof(Qs_PluginState));
                if (!na) continue;
                pm->entries  = na;
                pm->capacity = nc;
            }
            Qs_PluginState *s = &pm->entries[pm->count++];
            memset(s, 0, sizeof(*s));
            snprintf(s->id, sizeof(s->id), "%s", id);
            s->enabled = enabled;
        }
    }

    cJSON_Delete(root);
}

static void save_state(Qs_PluginManager *pm)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "plugins", arr);

    for (uint32_t i = 0; i < pm->count; i++) {
        Qs_PluginState *s = &pm->entries[i];
        /* Skip entries with no id, and skip path-less placeholder entries
           created by load_state — they are superseded by the real DLL entry
           once it is discovered on disk (which sets both id and path). */
        if (s->id[0] == '\0' || s->path[0] == '\0') continue;
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "id", s->id);
        cJSON_AddBoolToObject(obj, "enabled", s->enabled);
        cJSON_AddItemToArray(arr, obj);
    }

    char *json = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json) return;

    FILE *f = fopen(pm->state_path, "wb");
    if (f) {
        fwrite(json, 1, strlen(json), f);
        fclose(f);
    }
    free(json);
}

/* ================================================================
   LOAD / UNLOAD ONE PLUGIN
   ================================================================ */

static bool plugin_load(Qs_PluginManager *pm, Qs_PluginState *s)
{
    if (s->loaded) return true;
    if (s->path[0] == '\0') return false;

    Qs_Dylib *lib = qs_dylib_open(s->path);
    if (!lib) {
        QS_LOG_ERROR("Plugin '%s': failed to open library: %s",
                     s->id[0] ? s->id : s->path,
                     qs_dylib_error() ? qs_dylib_error() : "unknown");
        return false;
    }

    Qs_PluginEntryFn entry_fn =
        (Qs_PluginEntryFn)qs_dylib_sym(lib, QS_PLUGIN_ENTRY_SYMBOL);
    if (!entry_fn) {
        QS_LOG_ERROR("Plugin '%s': symbol '%s' not found",
                     s->path, QS_PLUGIN_ENTRY_SYMBOL);
        qs_dylib_close(lib);
        return false;
    }

    const Qs_PluginDesc *desc = entry_fn();
    if (!desc) {
        QS_LOG_ERROR("Plugin '%s': entry returned NULL descriptor", s->path);
        qs_dylib_close(lib);
        return false;
    }

    if (desc->api_version != QS_PLUGIN_API_VERSION) {
        QS_LOG_ERROR("Plugin '%s': API version mismatch (plugin=%u, engine=%u)",
                     desc->name ? desc->name : s->path,
                     desc->api_version, QS_PLUGIN_API_VERSION);
        qs_dylib_close(lib);
        return false;
    }

    if (desc->id) snprintf(s->id, sizeof(s->id), "%s", desc->id);

    s->lib    = lib;
    s->desc   = desc;
    s->loaded = true;

    if (desc->on_load) desc->on_load(pm->engine);

    QS_LOG_INFO("Plugin '%s' v%s loaded", desc->name, desc->version);
    return true;
}

static void plugin_unload(Qs_PluginManager *pm, Qs_PluginState *s)
{
    if (!s->loaded) return;

    if (s->desc && s->desc->on_unload)
        s->desc->on_unload(pm->engine);

    QS_LOG_INFO("Plugin '%s' unloaded",
                s->desc && s->desc->name ? s->desc->name : s->id);

    qs_dylib_close(s->lib);
    s->lib    = NULL;
    s->desc   = NULL;
    s->loaded = false;
}

/* ================================================================
   DIRECTORY SCAN
   ================================================================ */

static void discover_plugins(Qs_PluginManager *pm)
{
#ifdef _WIN32
    char pattern[QS_MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*" QS_DYLIB_EXT, pm->plugin_dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        char full[QS_MAX_PATH];
        snprintf(full, sizeof(full), "%s\\%s", pm->plugin_dir, fd.cFileName);

        /* Skip if already discovered */
        bool dup = false;
        for (uint32_t i = 0; i < pm->count; i++) {
            if (strcmp(pm->entries[i].path, full) == 0) { dup = true; break; }
        }
        if (dup) continue;

        if (pm->count == pm->capacity) {
            uint32_t nc  = pm->capacity * 2;
            Qs_PluginState *na = realloc(pm->entries,
                                         nc * sizeof(Qs_PluginState));
            if (!na) break;
            pm->entries  = na;
            pm->capacity = nc;
        }

        Qs_PluginState *s = &pm->entries[pm->count++];
        memset(s, 0, sizeof(*s));
        snprintf(s->path, sizeof(s->path), "%s", full);
        s->enabled = true; /* new plugins default to enabled */
    } while (FindNextFileA(h, &fd));

    FindClose(h);
#else
    DIR *d = opendir(pm->plugin_dir);
    if (!d) return;

    struct dirent *de;
    const size_t ext_len = strlen(QS_DYLIB_EXT);

    while ((de = readdir(d)) != NULL) {
        size_t name_len = strlen(de->d_name);
        if (name_len <= ext_len) continue;
        if (strcmp(de->d_name + name_len - ext_len, QS_DYLIB_EXT) != 0) continue;

        char full[QS_MAX_PATH];
        snprintf(full, sizeof(full), "%s/%s", pm->plugin_dir, de->d_name);

        bool dup = false;
        for (uint32_t i = 0; i < pm->count; i++) {
            if (strcmp(pm->entries[i].path, full) == 0) { dup = true; break; }
        }
        if (dup) continue;

        if (pm->count == pm->capacity) {
            uint32_t nc  = pm->capacity * 2;
            Qs_PluginState *na = realloc(pm->entries,
                                         nc * sizeof(Qs_PluginState));
            if (!na) break;
            pm->entries  = na;
            pm->capacity = nc;
        }

        Qs_PluginState *s = &pm->entries[pm->count++];
        memset(s, 0, sizeof(*s));
        snprintf(s->path, sizeof(s->path), "%s", full);
        s->enabled = true;
    }
    closedir(d);
#endif
}

/* ================================================================
   PUBLIC API
   ================================================================ */

Qs_PluginManager *qs_plugin_manager_create(Qs_Engine *engine,
                                            const char *plugin_dir)
{
    Qs_PluginManager *pm = calloc(1, sizeof(Qs_PluginManager));
    if (!pm) return NULL;

    pm->engine   = engine;
    pm->capacity = 16;
    pm->entries  = calloc(pm->capacity, sizeof(Qs_PluginState));
    if (!pm->entries) { free(pm); return NULL; }

    /* Resolve plugin directory */
    if (plugin_dir) {
        snprintf(pm->plugin_dir, sizeof(pm->plugin_dir), "%s", plugin_dir);
    } else {
        char exe_dir[QS_MAX_PATH];
        if (qs_dylib_exe_dir(exe_dir, sizeof(exe_dir))) {
            snprintf(pm->plugin_dir, sizeof(pm->plugin_dir),
                     "%s" QS_PATH_SEP "plugins", exe_dir);
        } else {
            snprintf(pm->plugin_dir, sizeof(pm->plugin_dir), "plugins");
        }
    }

    /* Resolve state file path */
    char cfg[QS_MAX_PATH];
    if (config_dir(cfg, sizeof(cfg))) {
        snprintf(pm->state_path, sizeof(pm->state_path),
                 "%s" QS_PATH_SEP "plugins.json", cfg);
    }

    /* Load persisted state before scanning so enable/disable flags are ready */
    if (pm->state_path[0]) load_state(pm);

    return pm;
}

void qs_plugin_manager_scan(Qs_PluginManager *pm)
{
    if (!pm) return;

    discover_plugins(pm);

    for (uint32_t i = 0; i < pm->count; i++) {
        Qs_PluginState *s = &pm->entries[i];
        if (s->enabled && !s->loaded && s->path[0])
            plugin_load(pm, s);
    }

    save_state(pm);
}

void qs_plugin_manager_destroy(Qs_PluginManager *pm)
{
    if (!pm) return;

    /* Unload in reverse order */
    for (uint32_t i = pm->count; i > 0; i--)
        plugin_unload(pm, &pm->entries[i - 1]);

    save_state(pm);

    free(pm->entries);
    free(pm);
}

bool qs_plugin_enable(Qs_PluginManager *pm, const char *id)
{
    if (!pm || !id) return false;
    for (uint32_t i = 0; i < pm->count; i++) {
        if (strcmp(pm->entries[i].id, id) == 0) {
            pm->entries[i].enabled = true;
            if (!pm->entries[i].loaded)
                plugin_load(pm, &pm->entries[i]);
            save_state(pm);
            return true;
        }
    }
    return false;
}

bool qs_plugin_disable(Qs_PluginManager *pm, const char *id)
{
    if (!pm || !id) return false;
    for (uint32_t i = 0; i < pm->count; i++) {
        if (strcmp(pm->entries[i].id, id) == 0) {
            pm->entries[i].enabled = false;
            if (pm->entries[i].loaded)
                plugin_unload(pm, &pm->entries[i]);
            save_state(pm);
            return true;
        }
    }
    return false;
}

uint32_t qs_plugin_count(const Qs_PluginManager *pm)
{
    return pm ? pm->count : 0;
}

const Qs_PluginState *qs_plugin_state_at(const Qs_PluginManager *pm,
                                          uint32_t idx)
{
    if (!pm || idx >= pm->count) return NULL;
    return &pm->entries[idx];
}

const Qs_PluginDesc *qs_plugin_state_desc(const Qs_PluginState *state)
{
    return state ? state->desc : NULL;
}

bool qs_plugin_state_enabled(const Qs_PluginState *state)
{
    return state ? state->enabled : false;
}

bool qs_plugin_state_loaded(const Qs_PluginState *state)
{
    return state ? state->loaded : false;
}

const char *qs_plugin_state_path(const Qs_PluginState *state)
{
    return state ? state->path : NULL;
}

const char *qs_plugin_state_id(const Qs_PluginState *state)
{
    return state ? state->id : NULL;
}

bool qs_plugin_reload(Qs_PluginManager *pm, const char *id)
{
    if (!pm || !id) return false;
    for (uint32_t i = 0; i < pm->count; i++) {
        if (strcmp(pm->entries[i].id, id) == 0) {
            Qs_PluginState *s = &pm->entries[i];
            /* Notify listeners before unloading so they can release references */
            qs_event_fire(qs_engine_event_bus(pm->engine),
                          QS_EVENT_PLUGIN_RELOAD_BEGIN,
                          (void *)id, (uint32_t)(strlen(id) + 1));
            plugin_unload(pm, s);
            bool ok = plugin_load(pm, s);
            if (ok) {
                QS_LOG_INFO("Plugin '%s' reloaded", id);
                qs_event_fire(qs_engine_event_bus(pm->engine),
                              QS_EVENT_PLUGIN_RELOAD_END,
                              (void *)id, (uint32_t)(strlen(id) + 1));
            } else {
                QS_LOG_ERROR("Plugin '%s': reload failed", id);
            }
            return ok;
        }
    }
    return false;
}
