#include "qs_system.h"
#include "qs_log.h"
#include <stdlib.h>
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
    Qs_SystemManager *mgr = calloc(1, sizeof(Qs_SystemManager));
    if (!mgr) return NULL;
    mgr->engine   = engine;
    mgr->capacity = 16;
    mgr->systems  = calloc(mgr->capacity, sizeof(Qs_System *));
    if (!mgr->systems) { free(mgr); return NULL; }
    return mgr;
}

void qs_system_manager_destroy(Qs_SystemManager *manager)
{
    if (!manager) return;

    for (uint32_t i = manager->count; i > 0; i--) {
        Qs_System *s = manager->systems[i - 1];
        if (s->shutdown) s->shutdown(s, manager->engine);
        qs_log(QS_LOG_DEBUG, "System '%s' shut down", s->name);
        free(s->name);
        free(s->data);
        free(s);
    }

    free(manager->systems);
    free(manager);
}

Qs_System *qs_system_register(Qs_SystemManager *manager, const Qs_SystemDesc *desc)
{
    if (!manager || !desc || !desc->name) return NULL;

    Qs_System *s = calloc(1, sizeof(Qs_System));
    if (!s) return NULL;

    size_t name_len = strlen(desc->name);
    s->name = malloc(name_len + 1);
    if (!s->name) { free(s); return NULL; }
    memcpy(s->name, desc->name, name_len + 1);

    s->init     = desc->init;
    s->shutdown = desc->shutdown;
    s->update   = desc->update;

    if (desc->data_size > 0) {
        s->data = calloc(1, desc->data_size);
        if (!s->data) { free(s->name); free(s); return NULL; }
    }

    if (s->init && !s->init(s, manager->engine)) {
        free(s->data);
        free(s->name);
        free(s);
        return NULL;
    }

    if (manager->count == manager->capacity) {
        uint32_t new_cap = manager->capacity * 2;
        Qs_System **new_arr = realloc(manager->systems, new_cap * sizeof(Qs_System *));
        if (!new_arr) {
            if (s->shutdown) s->shutdown(s, manager->engine);
            free(s->data);
            free(s->name);
            free(s);
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
            free(system->name);
            free(system->data);
            free(system);
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
