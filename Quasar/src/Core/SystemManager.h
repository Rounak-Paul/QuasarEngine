#pragma once

#include <qspch.h>
#include "System.h"

#include <Core/Event.h>
#include <Core/Input.h>

namespace Quasar
{
    typedef enum qs_system {
        SYSTEM_EVENT,
        SYSTEM_INPUT,
        SYSTEM_MAX = 0xFF
    } qs_system;

    class QS_API SystemManager {
        public:
        ~SystemManager() = default;
        static b8 init();
        void shutdown();
        static SystemManager& get_instance() {return *instance;}

        System* get_system(qs_system s_id) {return registered_systems[s_id];}

        b8 Register(qs_system s_id, System* s, void* config);
        void Unregister(qs_system s_id);

        private:
        SystemManager() {};
        static SystemManager* instance;

        /**
         * @brief array to hold registered systems for the core engine, allocated on init(), destroyed on shutdown()
         * 
         */
        System** registered_systems;
    };

    #define QS_SYSTEM_MANAGER SystemManager::get_instance()
    #define QS_EVENT (*(Event*)QS_SYSTEM_MANAGER.get_system(SYSTEM_EVENT))
    #define QS_INPUT (*(Input*)QS_SYSTEM_MANAGER.get_system(SYSTEM_INPUT))
} // namespace Quasar
