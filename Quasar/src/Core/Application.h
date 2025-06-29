#pragma once
#include <qspch.h>

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
        ~Application();

        Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;

        void run();
    
    private:
        static Application* instance;
        b8 running = true;
        b8 suspended = false;
    };

    Application* CreateApplication();
} // namespace Quasar