#include "Window.h"

namespace Quasar
{

	b8 Window::create(u32 w, u32 h, const std::string& name)
	{
		_width = w;
		_height = h;
		_name = name;
		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
		if (_width <= 0 || _height <= 0) {
			glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
			GLFWmonitor* monitor = glfwGetPrimaryMonitor();
			const GLFWvidmode* mode = glfwGetVideoMode(monitor);
			_width = mode->width; _height = mode->height;
		}
		_window = glfwCreateWindow(_width, _height, _name.c_str(), nullptr, nullptr);
		
		glfwSetWindowUserPointer(_window, this);
		glfwSetFramebufferSizeCallback(_window, resize_callback);
		glfwSetWindowFocusCallback(_window, focus_callback);

		// Get the initial framebuffer size
		int framebufferWidth, framebufferHeight;
		glfwGetFramebufferSize(_window, &framebufferWidth, &framebufferHeight);
		_width = framebufferWidth;
		_height = framebufferHeight;

		return true;
	}

	void Window::destroy()
	{
		glfwDestroyWindow(_window);
		glfwTerminate();
	}

    void Window::resize_callback(GLFWwindow *window, int width, int height)
    {
		auto w = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
	}

	// GLFW window focus callback
	void Window::focus_callback(GLFWwindow* window, int focused) {
		
	}
}