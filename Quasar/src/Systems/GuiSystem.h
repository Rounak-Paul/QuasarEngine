#pragma once

#include <qspch.h>
#include <Core/System.h>
#include <Gui/GuiWindow.h>

namespace Quasar
{
#define MAX_GUI_WINDOWS 256

typedef struct gui_system_state {
    Hashmap<u32> window_table;
    GuiWindow* windows[MAX_GUI_WINDOWS] {nullptr};
} gui_system_state;

class QS_API GuiSystem : public System {
    public:
    GuiSystem() {};
    ~GuiSystem() = default;
    virtual b8 init(void* config) override;
    virtual void shutdown() override;

    /**
     * @brief GuiWindow must be created with the new operator
     * 
     * @param window GuiWindow* to be registered with the renderer
     * @return b8 
     */
    b8 register_window(GuiWindow* window);

    /**
     * @brief unregister the window created with new operator, performs delete operator
     * 
     * @param window registered GuiWindow*
     */
    void unregister_window(GuiWindow* window);

    GuiWindow** get_render_data() {
        return state.windows;
    }

    private:
    gui_system_state state;
};
}