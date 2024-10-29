#include "Application.h"

namespace Quasar {
    Application* Application::instance = nullptr;

    Application::Application(app_create_info info) : engine_name{info.app_name}, window{info.width, info.height, info.app_name.c_str()} {
        assert(!instance);
        instance = this;

        LOG_INFO("Booting Quasar Engine...");

        LOG_INFO("Initializing Log...")
        if (!Log::init()) {LOG_ERROR("Log failed to Initialize")}

        VkExtent2D extent = window.get_extent();
        LOG_INFO("Created main window [%u, %u]", extent.width, extent.height)

        LOG_INFO("Initializing Memory System...")
        if (!Memory::init()) {
            LOG_ERROR("Memory System failed to Initialize")
            abort();
        }

        // This is really a core count. Subtract 1 to account for the main thread already being in use.
        i32 thread_count = platform_get_processor_count() - 1;
        if (thread_count < 1) {
            LOG_FATAL("Platform reported processor count (minus one for main thread) as %i. Need at least one additional thread for the job system.", thread_count)
            abort();
        } else {
            LOG_TRACE("Available threads: %i", thread_count)
        }

        // Cap the thread count.
        const i32 max_thread_count = 15;
        if (thread_count > max_thread_count) {
            LOG_TRACE("Available threads on the system is %i, but will be capped at %i.", thread_count, max_thread_count)
            thread_count = max_thread_count;
        }

        LOG_INFO("Initializing System Manager...")
        if (!SystemManager::init()) {
            LOG_ERROR("System Manager failed to Initialize")
            abort();
        }

        LOG_INFO("Initializing Event System...")
        Event* event_system = new (QSMEM.allocate(sizeof(Event))) Event;
        QS_SYSTEM_MANAGER.Register(SYSTEM_EVENT, event_system, nullptr);

        LOG_INFO("Initializing Input System...")
        input_system_config input_config;
        input_config.main_window = &window;
        Input* input_system = new (QSMEM.allocate(sizeof(Input))) Input;
        QS_SYSTEM_MANAGER.Register(SYSTEM_INPUT, input_system, &input_config);
    }

    Application::~Application() {

    }

    void Application::run() {
        LOG_INFO("Running...");
        while (!window.should_close() && running)
        {
            if (suspended) { 
                window.wait_events();
                continue; 
            }

            window.poll_events(); 
            
            if (QS_INPUT.get_key_state(KeyCode::QS_KEY_SPACE)) {
                LOG_TRACE("Space clicked");
            }
        }
        
    }
}