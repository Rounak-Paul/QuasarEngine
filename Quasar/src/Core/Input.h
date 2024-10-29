#pragma once
#include <qspch.h>
#include "Keycodes.h"
#include "System.h"
#include <Core/Window.h>

namespace Quasar
{
    /**
     * @brief Key states to hold active states of all registered keyboard keys.
     * 
     * contains array of b32
     */
    typedef struct keyboard_state {
        b32 keys[QS_KEY_MAX];
    } keyboard_state;

    /**
     * @brief Mouse state to track x, y coords
     * 
     * holds array of b32 to keep track of active states of all mouse buttons
     * 
     */
    typedef struct mouse_state {
        f64 x;
        f64 y;
        f64 x_prev;
        f64 y_prev;
        f64 xdt;
        f64 ydt;
        u8 buttons[QS_MBTN_MAX];
    } mouse_state;

    /**
     * @brief input system config for init()
     * 
     */
    typedef struct input_system_config {
        /**
         * @brief latch Quasar Window to this to link input system with the window
         * 
         */
        Window* main_window;
    } input_system_config;

    class QS_API Input : public System {
        public:
        Input() {}
        ~Input() = default;
        virtual b8 init(void* config) override;
        virtual void shutdown() override;
        
        /**
         * @brief Get the currrent key state of the key
         * 
         * @param key Quasar Keycode to quarry state of
         * @return b32 state 0 not pressed, state 1 pressed down, state 2 press and hold
         */
        QS_INLINE b32 get_key_state(KeyCode key) { return keyboard_state.keys[key]; };

        /**
         * @brief Get the currrent mouse btn state of the key
         * 
         * @param btn Quasar Mouse btn to quarry state of
         * @return b32 state 0 not clicked, state 1 clicked
         */
        QS_INLINE b32 get_mbtn_state(MouseCode btn) { return mouse_state.buttons[btn]; };


		f64 get_mouseX();
		f64 get_mouseY();
        f64 get_mouseXDT();
		f64 get_mouseYDT();

        void update();
        
        private:
        keyboard_state keyboard_state;
        mouse_state mouse_state;
        Window* main_window;

        static void is_key_pressed(GLFWwindow* window, int key, int scancode, int action, int mods);
		static void is_mbtn_pressed(GLFWwindow* window, int button, int action, int mods);
    };
} // namespace Quasar