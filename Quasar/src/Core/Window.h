#pragma once

#include <qspch.h>
#include <Math/MathUtils.h>

namespace Quasar 
{
	class Window
	{
	public:
		Window() {};
		~Window() = default;;

		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;

		b8 create(u32 w, u32 h, const std::string& name);
		void destroy();

		QS_INLINE b8 should_close() { return glfwWindowShouldClose(_window); }
		const Extent2D get_extent() const { 
			return { _width, _height }; 
			}
        QS_INLINE void poll_events() { glfwPollEvents(); }
		QS_INLINE void wait_events() { glfwWaitEvents(); }
		
		GLFWwindow* get_GLFWwindow() const { return _window; }

	private:
		static void resize_callback(GLFWwindow* window, int width, int height);
		static void focus_callback(GLFWwindow* window, int focused);

		u32 _width;
		u32 _height;
		b8 _framebuffer_resized = false;

		std::string _name;
		GLFWwindow* _window;
	};
}
