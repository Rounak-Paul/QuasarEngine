#include "Input.h"
#include "Application.h"

namespace Quasar
{
	b8 Input::init(void* config) {
		input_system_config* conf = (input_system_config*)config;
		main_window = conf->main_window;
		// glfwSetInputMode(main_window->get_GLFWwindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		glfwSetKeyCallback(main_window->get_GLFWwindow(), is_key_pressed);
		glfwSetMouseButtonCallback(main_window->get_GLFWwindow() ,is_mbtn_pressed);
		return TRUE;
	}

	void Input::shutdown() {
		
	}

    void Input::is_key_pressed(GLFWwindow* window, int key, int scancode, int action, int mods) {
		// if (!QS_APP_STATE.gui_in_focus) {
			QS_INPUT.keyboard_state.keys[key] = action;
		// }
    }

    void Input::is_mbtn_pressed(GLFWwindow* window, int button, int action, int mods) {
		// if (!QS_APP_STATE.gui_in_focus)
		QS_INPUT.mouse_state.buttons[button] = action;
	}

	void Input::update()
	{
		
		glfwGetCursorPos(main_window->get_GLFWwindow(), &mouse_state.x, &mouse_state.y);
		// if (QS_APP_STATE.gui_in_focus) {
		// 	mouse_state.x_prev = mouse_state.x;
		// 	mouse_state.y_prev = mouse_state.y;
		// 	mouse_state.xdt = 0.f;
		// 	mouse_state.ydt = 0.f;
		// 	return;
		// }
		mouse_state.xdt = mouse_state.x_prev - mouse_state.x;
		mouse_state.ydt = mouse_state.y_prev - mouse_state.y;
		if ((mouse_state.x_prev == mouse_state.x) && (mouse_state.y_prev == mouse_state.y)) return;
		mouse_state.x_prev = mouse_state.x;
		mouse_state.y_prev = mouse_state.y;
	}

	f64 Input::get_mouseX()
	{
		return mouse_state.x;
	}

	f64 Input::get_mouseY()
	{
		return mouse_state.y;
	}

	f64 Input::get_mouseXDT()
	{
		return mouse_state.xdt;
	}

	f64 Input::get_mouseYDT()
	{
		return mouse_state.ydt;
	}
} // namespace Quasar