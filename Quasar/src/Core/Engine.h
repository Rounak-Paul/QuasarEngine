#pragma once
#include <qspch.h>

namespace Quasar
{
    typedef struct QS_API app_create_info {
        char* app_name;
        u32 width;
        u32 height;
    } app_create_info ;

    class QS_API Engine
    {
    public:
        Engine(app_create_info info);
        ~Engine();

        Engine(const Engine&) = delete;
		Engine& operator=(const Engine&) = delete;

        void run();
    
    private:
        static Engine* instance;
        b8 running = true;
        b8 suspended = false;
    };

    Engine* CreateEngine();
} // namespace Quasar