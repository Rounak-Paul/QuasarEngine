#pragma once
#include <qspch.h>

#include <Core/Window.h>
#include <Renderer/Renderer.h>

namespace Quasar
{
    typedef struct QS_API app_create_info {
        std::string app_name;
        u32 width;
        u32 height;
    } app_create_info ;

    class QS_API Application
    {
    public:
        Application(app_create_info info);
        ~Application() = default;

        Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;

        b8 init();
        void run();
        void shutdown();
    
    private:
        static Application* _instance;
        b8 _running = true;
        b8 _suspended = false;
        app_create_info create_info;

        Window _window;
        Renderer _renderer;
    };

    Application* CreateApplication();
} // namespace Quasar