#pragma once
#include <qspch.h>
#include <AppTypes.inl>

namespace Quasar
{
    class QS_API Application
    {
    public:
        Application(engine_state state);
        ~Application();

        Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;

        void run();
    
    private:
        static Application* instance;
        engine_state state;
    };

    Application* CreateApplication();
} // namespace Quasar