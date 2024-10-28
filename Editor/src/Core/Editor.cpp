#include "Editor.h"

using namespace Quasar;

Quasar::Application* Quasar::CreateApplication()
{
    Quasar::app_create_info info;
    info.width = 800;
    info.height = 600;
    info.app_name = "Editor - Quasar Engine";

	return new Editor(info);
};

Editor::Editor(Quasar::app_create_info info) : Application(info) {

}

Editor::~Editor() {

}
