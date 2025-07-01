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

		b8 create(u32 w, u32 h, const char* name);
		void destroy();

		QS_INLINE b8 should_close() { return glfwWindowShouldClose(_window); }
		Extent2D get_extent() { 
			return { _width, _height }; 
			}
        QS_INLINE void poll_events() { glfwPollEvents(); }
		QS_INLINE void wait_events() { glfwWaitEvents(); }
		
		GLFWwindow* get_GLFWwindow() const { return _window; }
		static bool _gui_has_focus;

	private:
		static void resize_callback(GLFWwindow* window, int width, int height);
		static void focus_callback(GLFWwindow* window, int focused);

		u32 _width;
		u32 _height;
		b8 _framebuffer_resized = false;

		const char* _name;
		GLFWwindow* _window;
	};
}
