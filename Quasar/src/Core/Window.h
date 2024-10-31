#pragma once
#include "qspch.h"

namespace Quasar 
{
	class Window
	{
	public:
		Window() {}
		Window(u32 w, u32 h, const char* name);
		~Window();

		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;

		QS_INLINE b8 should_close() { return glfwWindowShouldClose(window); }
		VkExtent2D get_extent() { 
			return { static_cast<uint32_t>(width), static_cast<uint32_t>(height) }; 
			}
        QS_INLINE void poll_events() {glfwPollEvents();}
		QS_INLINE void wait_events() {glfwWaitEvents();}
		
		GLFWwindow* get_GLFWwindow() const { return window; }
		static bool ImGuiHasFocus;

	private:
		static void framebuffer_resize_callback(GLFWwindow* window, int width, int height);
		static void window_focus_callback(GLFWwindow* window, int focused);

		u32 width;
		u32 height;
		b8 framebuffer_resized = false;

		const char* windowName;
		GLFWwindow* window;
	};
}
