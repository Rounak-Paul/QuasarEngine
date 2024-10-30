#pragma once

#include <qspch.h>
#include "System.h"

#include <Core/Event.h>
#include <Core/Input.h>
#include <Systems/JobSystem.h>

namespace Quasar
{
    /**
     * @brief Quasar Engine's all systems
     * 
     */
    typedef enum qs_system {
        SYSTEM_EVENT,
        SYSTEM_INPUT,
        SYSTEM_JOB,
        SYSTEM_MAX = 0xFF
    } qs_system;

    class QS_API SystemManager {
        public:
        ~SystemManager() = default;
        static b8 init();
        void shutdown();
        static SystemManager& get_instance() {return *instance;}

        System* get_system(qs_system s_id) {return registered_systems[s_id];}

        /**
         * @brief Register a system to the system manager, calls init() of the System, this SystemManager becomes owner of the system instance
         * 
         * @param s_id system's target id, Note: one system registration per id 
         * @param s Pointer to the System
         * @param config initial config data, used with init(config)
         * @return b8 Registration is successful. If System is nullptr or override registration returns false
         */
        b8 Register(qs_system s_id, System* s, void* config);

        /**
         * @brief Unregister the system from the SystemManager, free System memory and ends ownership of the System
         * 
         * @param s_id System id to be Unregistered
         */
        void Unregister(qs_system s_id);

        private:
        SystemManager() {};
        static SystemManager* instance;

        /**
         * @brief array to hold registered systems for the core engine, allocated on init(), destroyed on shutdown()
         * 
         */
        System* registered_systems[SYSTEM_MAX];
    };

    #define QS_SYSTEM_MANAGER SystemManager::get_instance()
    #define QS_EVENT (*(Event*)QS_SYSTEM_MANAGER.get_system(SYSTEM_EVENT))
    #define QS_INPUT (*(Input*)QS_SYSTEM_MANAGER.get_system(SYSTEM_INPUT))
    #define QS_JOB_SYSTEM (*(JobSystem*)QS_SYSTEM_MANAGER.get_system(SYSTEM_JOB))
} // namespace Quasar
