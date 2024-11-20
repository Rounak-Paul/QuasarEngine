#include "Window.h"

namespace Quasar
{
	b8 Window::ImGuiHasFocus = false;

	Window::Window(u32 w, u32 h, const char* name) : width{w}, height{h}, windowName{name} 
	{
		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
		if (width <= 0 || height <= 0) {
			glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
			GLFWmonitor* monitor = glfwGetPrimaryMonitor();
			const GLFWvidmode* mode = glfwGetVideoMode(monitor);
			width = mode->width; height = mode->height;
		}
		window = glfwCreateWindow(width, height, windowName, nullptr, nullptr);
		
		glfwSetWindowUserPointer(window, this);
		glfwSetFramebufferSizeCallback(window, framebuffer_resize_callback);
		glfwSetWindowFocusCallback(window, window_focus_callback);

		// Get the initial framebuffer size
		int framebufferWidth, framebufferHeight;
		glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
		width = framebufferWidth;
		height = framebufferHeight;
	}

	Window::~Window()
	{
		glfwDestroyWindow(window);
		glfwTerminate();
	}

	void Window::framebuffer_resize_callback(GLFWwindow* window, int width, int height)
	{
		auto qsWindow = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
		qsWindow->width = width;
		qsWindow->height = height;
		qsWindow->framebuffer_resized = true;
		event_context context;
		context.data.u16[0] = width;
		context.data.u16[1] = height;
		QS_EVENT.Execute(EVENT_CODE_RESIZED, 0, context);
	}

	// GLFW window focus callback
	void Window::window_focus_callback(GLFWwindow* window, int focused) {
		ImGuiHasFocus = focused == GLFW_FALSE; // TODO: Not used yet	
		event_context context = {};
		context.data.i32[0] = focused;
		QS_EVENT.Execute(EVENT_CODE_WINDOW_FOCUS_CHANGED, nullptr, context);
	}
}