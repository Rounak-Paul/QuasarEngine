#include "Application.h"

namespace Quasar {
    Application* Application::instance = nullptr;

    Application::Application(app_create_info info) : engine_name{info.app_name}, window{info.width, info.height, info.app_name.c_str()} {
        assert(!instance);
        instance = this;

        LOG_DEBUG("Booting Quasar Engine...");

        LOG_DEBUG("Initializing Log...")
        if (!Log::init()) {LOG_ERROR("Log failed to Initialize")}

        VkExtent2D extent = window.get_extent();
        LOG_DEBUG("Created main window [%u, %u]", extent.width, extent.height)

        LOG_DEBUG("Initializing Memory System...")
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
            LOG_DEBUG("Available threads: %i", thread_count)
        }

        // Cap the thread count.
        const i32 max_thread_count = 15;
        if (thread_count > max_thread_count) {
            LOG_INFO("Available threads on the system is %i, but will be capped at %i.", thread_count, max_thread_count)
            thread_count = max_thread_count;
        }

        LOG_DEBUG("Initializing System Manager...")
        if (!SystemManager::init()) {
            LOG_ERROR("System Manager failed to Initialize")
            abort();
        }

        LOG_DEBUG("Initializing Event System...")
        Event* event_system = new (QSMEM.allocate(sizeof(Event))) Event;
        QS_SYSTEM_MANAGER.Register(SYSTEM_EVENT, event_system, nullptr);

        LOG_DEBUG("Initializing Input System...")
        input_system_config input_config;
        input_config.main_window = &window;
        Input* input_system = new (QSMEM.allocate(sizeof(Input))) Input;
        QS_SYSTEM_MANAGER.Register(SYSTEM_INPUT, input_system, &input_config);

        // Event registration for window size change
        QS_EVENT.Register(EVENT_CODE_RESIZED, this, application_on_resized);

        LOG_DEBUG("Initializing Renderer...")
        renderer_system_config renderer_sys_config;
        renderer_sys_config.application_name = info.app_name;
        renderer_sys_config.window = &window;
        Renderer* renderer_system = new (QSMEM.allocate(sizeof(Renderer))) Renderer;
        QS_SYSTEM_MANAGER.Register(SYSTEM_RENDERER, renderer_system, &renderer_sys_config);

        b8 renderer_multithreaded = QS_RENDERER.is_multithreaded();

        // Initialize the job system.
        // Requires knowledge of renderer multithread support, so should be initialized here.
        LOG_DEBUG("Initializing Job System...")
        job_system_config job_sys_config{};
        job_sys_config.max_job_thread_count = thread_count;
        for (u32 i = 0; i < 15; ++i) {
            job_sys_config.type_masks[i] = JOB_TYPE_GENERAL;
        }
        if (max_thread_count == 1 || !renderer_multithreaded) {
            // Everything on one job thread.
            job_sys_config.type_masks[0] |= (JOB_TYPE_GPU_RESOURCE | JOB_TYPE_RESOURCE_LOAD);
        } else if (max_thread_count == 2) {
            // Split things between the 2 threads
            job_sys_config.type_masks[0] |= JOB_TYPE_GPU_RESOURCE;
            job_sys_config.type_masks[1] |= JOB_TYPE_RESOURCE_LOAD;
        } else {
            // Dedicate the first 2 threads to these things, pass off general tasks to other threads.
            job_sys_config.type_masks[0] = JOB_TYPE_GPU_RESOURCE;
            job_sys_config.type_masks[1] = JOB_TYPE_RESOURCE_LOAD;
        }
        JobSystem* job_system = new (QSMEM.allocate(sizeof(JobSystem))) JobSystem;
        QS_SYSTEM_MANAGER.Register(SYSTEM_JOB, job_system, &job_sys_config);

    }

    Application::~Application() {

    }

    void Application::run() {
        LOG_DEBUG("Running...");
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

        // Shutdown routine
        {
            event_context context{};
            QS_EVENT.Execute(EVENT_CODE_APPLICATION_QUIT, this, context);
        }
        QS_EVENT.Unregister(EVENT_CODE_RESIZED, this, application_on_resized);
        
        QS_SYSTEM_MANAGER.Unregister(SYSTEM_JOB);
        QS_SYSTEM_MANAGER.Unregister(SYSTEM_RENDERER);
        QS_SYSTEM_MANAGER.Unregister(SYSTEM_INPUT);
        QS_SYSTEM_MANAGER.Unregister(SYSTEM_EVENT);
        

        LOG_DEBUG("Shutdown Quasar Engine successful")
    }

    b8 Application::application_on_resized(u16 code, void* sender, void* listener_inst, event_context context) {
        Application* app = static_cast<Application*>(listener_inst);
        if (code == EVENT_CODE_RESIZED) {
            u16 width = context.data.u16[0];
            u16 height = context.data.u16[1];

            if (width == 0 || height == 0) {
                LOG_INFO("Application suspended")
                app->suspended = true;
            }
            else {
                app->suspended = false;
                LOG_INFO("Application Resized [%u, %u]", width, height)
                // QS_RENDERER_API.resize(width, height);
            }

            return true;
        }
        return false;
    }
}