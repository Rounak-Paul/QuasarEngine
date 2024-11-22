#pragma once
#include <qspch.h>

namespace Quasar {

	class GuiWindow
	{
	public:
		GuiWindow(const String& name = "Window");
		virtual ~GuiWindow() = default;

		virtual void init() {}
		virtual void shutdown() {}
		virtual void update() {}

		const String& get_name() const { return window_name; }
	protected:
		String window_name;
	};

}