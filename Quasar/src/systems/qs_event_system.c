#include "qs_event.h"
#include "qs_system.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct Qs_Listener {
    Qs_EventId  id;
    Qs_EventFn  callback;
    void       *user_data;
    uint32_t    handle;
} Qs_Listener;

struct Qs_EventBus {
    Qs_Listener *listeners;
    uint32_t     count;
    uint32_t     capacity;
    uint32_t     next_handle;
};

#define QS_EVENT_INITIAL_CAP 32

static bool event_system_init(Qs_System *system, Qs_Engine *engine)
{
    (void)engine;
    Qs_EventBus *bus = qs_system_data(system);

    bus->capacity    = QS_EVENT_INITIAL_CAP;
    bus->listeners   = calloc(bus->capacity, sizeof(Qs_Listener));
    if (!bus->listeners) return false;
    bus->next_handle = 1;
    return true;
}

static void event_system_shutdown(Qs_System *system, Qs_Engine *engine)
{
    (void)engine;
    Qs_EventBus *bus = qs_system_data(system);
    free(bus->listeners);
    bus->listeners = NULL;
    bus->count     = 0;
    bus->capacity  = 0;
}

Qs_SystemDesc qs_event_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Event",
        .data_size = sizeof(Qs_EventBus),
        .init      = event_system_init,
        .shutdown  = event_system_shutdown,
    };
}

uint32_t qs_event_subscribe(Qs_EventBus *bus, Qs_EventId id,
                            Qs_EventFn callback, void *user_data)
{
    if (!bus || !callback) return 0;

    if (bus->count == bus->capacity) {
        uint32_t new_cap = bus->capacity * 2;
        Qs_Listener *grown = realloc(bus->listeners, new_cap * sizeof(Qs_Listener));
        if (!grown) return 0;
        bus->listeners = grown;
        bus->capacity  = new_cap;
    }

    uint32_t handle = bus->next_handle++;
    bus->listeners[bus->count++] = (Qs_Listener){
        .id        = id,
        .callback  = callback,
        .user_data = user_data,
        .handle    = handle,
    };
    return handle;
}

void qs_event_unsubscribe(Qs_EventBus *bus, uint32_t handle)
{
    if (!bus || handle == 0) return;

    for (uint32_t i = 0; i < bus->count; ++i) {
        if (bus->listeners[i].handle == handle) {
            bus->listeners[i] = bus->listeners[--bus->count];
            return;
        }
    }
}

void qs_event_fire(Qs_EventBus *bus, Qs_EventId id,
                   void *data, uint32_t data_size)
{
    if (!bus) return;

    Qs_Event event = {
        .id        = id,
        .data      = data,
        .data_size = data_size,
        .handled   = false,
    };

    for (uint32_t i = 0; i < bus->count; ++i) {
        if (bus->listeners[i].id == id) {
            if (bus->listeners[i].callback(&event, bus->listeners[i].user_data)) {
                event.handled = true;
                return;
            }
        }
    }
}
