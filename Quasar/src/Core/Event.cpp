#include "Event.h"

namespace Quasar {

    b8 Event::init(void* config) {
        return true;
    }

    void Event::shutdown() {
        
    }

    b8 Event::Register(u16 code, void* listener, PFN_on_event on_event) {
        size_t registered_count = event_state.registered[code].events.size();
        for(size_t i = 0; i < registered_count; ++i) {
            if(event_state.registered[code].events[i].listener == listener) {
                LOG_WARN("Duplicate event listener was issued!");
                return false;
            }
        }

        // If at this point, no duplicate was found. Proceed with registration.
        registered_event event;
        event.listener = listener;
        event.callback = on_event;
        event_state.registered[code].events.push_back(event);

        return true;
    }

    b8 Event::Unregister(u16 code, void* listener, PFN_on_event on_event) {
        // On nothing is registered for the code, boot out.
        if(event_state.registered[code].events.size() == 0) {
            LOG_WARN("Event list is empty");
            return false;
        }

        u64 registered_count = event_state.registered[code].events.size();
        for (u64 i = 0; i < registered_count; ++i) {
            registered_event &e = event_state.registered[code].events[i]; // Use reference to modify the element if needed
            if (e.listener == listener && e.callback == on_event) {
                // Found the element to remove
                event_state.registered[code].events.erase(event_state.registered[code].events.begin() + i);
                return true; // Assuming true is a typo and you meant true (lowercase)
            }
        }

        // Not found.
        return false;
    }

    b8 Event::Execute(u16 code, void* sender, event_context context) {
        // If nothing is registered for the code, boot out.
        if(event_state.registered[code].events.size() == 0) {
            return false;
        }

        u64 registered_count = event_state.registered[code].events.size();
        for(u64 i = 0; i < registered_count; ++i) {
            registered_event e = event_state.registered[code].events[i];
            if(e.callback(code, sender, e.listener, context)) {
                // Message has been handled, do not send to other listeners.
                return true;
            }
        }

        // Not found.
        return false;
    }  
}