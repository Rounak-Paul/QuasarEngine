#pragma once
#include <qspch.h>
#include <AppTypes.inl>

#include "Window.h"

namespace Quasar
{
    /**
     * @brief Singleton Quasar Application class to be created by the main() function in EntryPoint.h.
     * 
     * The Application class can be inherited and instantiated by the user as shown below:
     * 
     * @code
     * // Function to create the application instance.
     * Quasar::Application* Quasar::CreateApplication()
     * {
     *     Quasar::app_create_info info;
     *     info.width = 800;
     *     info.height = 600;
     *     info.app_name = "Editor - Quasar Engine";
     * 
     *     return new Editor(info); // Return a new instance of the derived Editor class.
     * };
     * 
     * // Editor class constructor and destructor.
     * Editor::Editor(Quasar::app_create_info info) : Application(info) {
     *     // Custom initialization for Editor
     * }
     * 
     * Editor::~Editor() {
     *     // Cleanup for Editor
     * }
     * @endcode
     */
    class QS_API Application
    {
    public:

        /**
         * @brief Construct a new Application object
         * 
         * @param info create new application with Width, Height and App name
         */
        Application(app_create_info info);
        ~Application();

        Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;

        /**
         * @brief Main application loop for Quasar Engine
         * 
         */
        void run();
    
    private:
        static Application* instance;

        /**
         * @brief Main window used for Input, Events and Rendering
         * 
         */
        Window window;
        
        std::string engine_name;
        b8 running = true;
        b8 suspended = false;
    };

    Application* CreateApplication();
} // namespace Quasar