#include "GuiSystem.h"

namespace Quasar
{
b8 GuiSystem::init(void *config)
{
    state.window_table.create(MAX_GUI_WINDOWS);

    return true;
}

void GuiSystem::shutdown()
{
    for (int i=0; i<MAX_GUI_WINDOWS; i++) {
        if (state.windows[i]) {
            unregister_window(state.windows[i]);
        }
    }
    state.window_table.destroy();
}

b8 GuiSystem::register_window(GuiWindow *window)
{
    auto it = state.window_table.get(window->get_name());
    if (it) {
        LOG_WARN("Window %s already exists!", window->get_name().c_str())
        return false;
    }
    u32 index;
    for (int i=0; i<MAX_GUI_WINDOWS; i++){
        if (state.windows[i] == nullptr) {
            state.windows[i] = window;
            index = i;
            break;
        }
    }
    state.window_table.set(window->get_name(), index);
    state.windows[index]->init();
    return true;
}

void GuiSystem::unregister_window(GuiWindow *window)
{
    u32 index = INVALID_ID;
    auto it = state.window_table.get(window->get_name());
    if (it) {
        index = *it;
    }
    else {
        LOG_WARN("Window %s is not registered!", window->get_name().c_str())
    }
    if (index != INVALID_ID) {
        state.window_table.erase(window->get_name());
        state.windows[index]->shutdown();
        delete window;
        state.windows[index] = nullptr;
    }
}

} // namespace Quasar
