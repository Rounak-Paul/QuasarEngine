#include "Editor.h"

using namespace Quasar;

Quasar::Application* Quasar::CreateApplication()
{
    Quasar::engine_state state;
    state.width = 800;
    state.height = 600;
    state.app_name = "Editor - Quasar Engine";

	return new Editor(state);
};

Editor::Editor(Quasar::engine_state state) : Application(state) {
    
}

Editor::~Editor() {

}
