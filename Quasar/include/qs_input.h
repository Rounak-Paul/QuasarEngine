#ifndef QS_INPUT_H
#define QS_INPUT_H

#include <stdint.h>
#include <stdbool.h>

/// Key action types.
typedef enum Qs_KeyAction {
    QS_KEY_RELEASE = 0,
    QS_KEY_PRESS   = 1,
    QS_KEY_REPEAT  = 2,
} Qs_KeyAction;

/// Modifier key flags.
typedef enum Qs_KeyMod {
    QS_MOD_SHIFT   = 0x0001,
    QS_MOD_CONTROL = 0x0002,
    QS_MOD_ALT     = 0x0004,
    QS_MOD_SUPER   = 0x0008,
} Qs_KeyMod;

/// Platform-independent key codes.
typedef enum Qs_Key {
    QS_KEY_UNKNOWN       = -1,
    QS_KEY_SPACE         = 32,
    QS_KEY_APOSTROPHE    = 39,
    QS_KEY_COMMA         = 44,
    QS_KEY_MINUS         = 45,
    QS_KEY_PERIOD        = 46,
    QS_KEY_SLASH         = 47,
    QS_KEY_0 = 48, QS_KEY_1, QS_KEY_2, QS_KEY_3, QS_KEY_4,
    QS_KEY_5, QS_KEY_6, QS_KEY_7, QS_KEY_8, QS_KEY_9,
    QS_KEY_SEMICOLON     = 59,
    QS_KEY_EQUAL         = 61,
    QS_KEY_A = 65, QS_KEY_B, QS_KEY_C, QS_KEY_D, QS_KEY_E,
    QS_KEY_F, QS_KEY_G, QS_KEY_H, QS_KEY_I, QS_KEY_J,
    QS_KEY_K, QS_KEY_L, QS_KEY_M, QS_KEY_N, QS_KEY_O,
    QS_KEY_P, QS_KEY_Q, QS_KEY_R, QS_KEY_S, QS_KEY_T,
    QS_KEY_U, QS_KEY_V, QS_KEY_W, QS_KEY_X, QS_KEY_Y, QS_KEY_Z,
    QS_KEY_LEFT_BRACKET  = 91,
    QS_KEY_BACKSLASH     = 92,
    QS_KEY_RIGHT_BRACKET = 93,
    QS_KEY_GRAVE_ACCENT  = 96,
    QS_KEY_ESCAPE        = 256,
    QS_KEY_ENTER         = 257,
    QS_KEY_TAB           = 258,
    QS_KEY_BACKSPACE     = 259,
    QS_KEY_INSERT        = 260,
    QS_KEY_DELETE        = 261,
    QS_KEY_RIGHT         = 262,
    QS_KEY_LEFT          = 263,
    QS_KEY_DOWN          = 264,
    QS_KEY_UP            = 265,
    QS_KEY_PAGE_UP       = 266,
    QS_KEY_PAGE_DOWN     = 267,
    QS_KEY_HOME          = 268,
    QS_KEY_END           = 269,
    QS_KEY_CAPS_LOCK     = 280,
    QS_KEY_SCROLL_LOCK   = 281,
    QS_KEY_NUM_LOCK      = 282,
    QS_KEY_PRINT_SCREEN  = 283,
    QS_KEY_PAUSE         = 284,
    QS_KEY_F1  = 290, QS_KEY_F2,  QS_KEY_F3,  QS_KEY_F4,
    QS_KEY_F5,  QS_KEY_F6,  QS_KEY_F7,  QS_KEY_F8,
    QS_KEY_F9,  QS_KEY_F10, QS_KEY_F11, QS_KEY_F12,
    QS_KEY_KP_0 = 320, QS_KEY_KP_1, QS_KEY_KP_2, QS_KEY_KP_3,
    QS_KEY_KP_4, QS_KEY_KP_5, QS_KEY_KP_6, QS_KEY_KP_7,
    QS_KEY_KP_8, QS_KEY_KP_9,
    QS_KEY_KP_DECIMAL    = 330,
    QS_KEY_KP_DIVIDE     = 331,
    QS_KEY_KP_MULTIPLY   = 332,
    QS_KEY_KP_SUBTRACT   = 333,
    QS_KEY_KP_ADD        = 334,
    QS_KEY_KP_ENTER      = 335,
    QS_KEY_LEFT_SHIFT    = 340,
    QS_KEY_LEFT_CONTROL  = 341,
    QS_KEY_LEFT_ALT      = 342,
    QS_KEY_LEFT_SUPER    = 343,
    QS_KEY_RIGHT_SHIFT   = 344,
    QS_KEY_RIGHT_CONTROL = 345,
    QS_KEY_RIGHT_ALT     = 346,
    QS_KEY_RIGHT_SUPER   = 347,
    QS_KEY_MENU          = 348,
    QS_KEY_LAST          = QS_KEY_MENU,
} Qs_Key;

#define QS_KEY_MAX 349

/// Feed a key event into the input system. Called by the platform layer.
void qs_input_key_event(Qs_Key key, Qs_KeyAction action, int mods);

/// Returns true if the key is currently held down.
bool qs_input_key_down(Qs_Key key);

/// Returns true if the key was pressed this frame (transition from up to down).
bool qs_input_key_pressed(Qs_Key key);

/// Returns true if the key was released this frame.
bool qs_input_key_released(Qs_Key key);

/// Returns the human-readable name for a key code.
const char *qs_key_name(Qs_Key key);

#endif
