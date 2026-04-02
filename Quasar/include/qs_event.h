#ifndef QS_EVENT_H
#define QS_EVENT_H

#include <stdint.h>
#include <stdbool.h>

/// Event ID type — engine reserves IDs below QS_EVENT_USER_BASE.
typedef uint32_t Qs_EventId;

/// User-defined events start at this value.
#define QS_EVENT_USER_BASE 0x10000

/* ── Built-in engine events ─────────────────────────────────── */

#define QS_EVENT_NONE           ((Qs_EventId)0)
#define QS_EVENT_ENGINE_INIT    ((Qs_EventId)1)
#define QS_EVENT_ENGINE_SHUTDOWN ((Qs_EventId)2)
#define QS_EVENT_TICK           ((Qs_EventId)3)
#define QS_EVENT_KEY_PRESS      ((Qs_EventId)4)
#define QS_EVENT_KEY_RELEASE    ((Qs_EventId)5)
#define QS_EVENT_MOUSE_MOVE     ((Qs_EventId)6)
#define QS_EVENT_MOUSE_PRESS    ((Qs_EventId)7)
#define QS_EVENT_MOUSE_RELEASE  ((Qs_EventId)8)
#define QS_EVENT_MOUSE_SCROLL   ((Qs_EventId)9)
#define QS_EVENT_WINDOW_RESIZE  ((Qs_EventId)10)
#define QS_EVENT_WINDOW_CLOSE   ((Qs_EventId)11)
#define QS_EVENT_WINDOW_FOCUS   ((Qs_EventId)12)
#define QS_EVENT_PLUGIN_RELOAD_BEGIN ((Qs_EventId)13)   /* fired before plugin unload  */
#define QS_EVENT_PLUGIN_RELOAD_END   ((Qs_EventId)14)   /* fired after plugin reloaded */
#define QS_EVENT_PLUGIN_DISABLE_BEGIN ((Qs_EventId)15)  /* fired before plugin disable */
#define QS_EVENT_PLUGIN_ENABLE_END    ((Qs_EventId)16)  /* fired after plugin enabled  */

/// Opaque event bus handle.
typedef struct Qs_EventBus Qs_EventBus;

/// Event payload passed to listeners.
typedef struct Qs_Event {
    Qs_EventId  id;
    void*       data;
    uint32_t    data_size;
    bool        handled;
} Qs_Event;

/// Listener callback signature. Return true to consume the event.
typedef bool (*Qs_EventFn)(const Qs_Event* event, void* user_data);

/// Subscribes a listener to an event ID. Returns a handle for unsubscribing.
uint32_t qs_event_subscribe(Qs_EventBus* bus, Qs_EventId id,
                            Qs_EventFn callback, void* user_data);

/// Removes a listener by its handle.
void qs_event_unsubscribe(Qs_EventBus* bus, uint32_t handle);

/// Fires an event immediately. Listeners are called in subscription order.
/// Stops early if a listener marks the event as handled.
void qs_event_fire(Qs_EventBus* bus, Qs_EventId id,
                   void* data, uint32_t data_size);

#endif
