#include "qs_input.h"
#include "qs_system.h"
#include "qs_log.h"

#include <string.h>

typedef struct {
    bool current[QS_KEY_MAX];
    bool previous[QS_KEY_MAX];
} Qs_InputState;

static Qs_InputState *g_input = NULL;

static bool input_system_init(Qs_System *system, Qs_Engine *engine)
{
    (void)engine;
    g_input = (Qs_InputState *)qs_system_data(system);
    memset(g_input->current,  0, sizeof(g_input->current));
    memset(g_input->previous, 0, sizeof(g_input->previous));
    return true;
}

static void input_system_shutdown(Qs_System *system, Qs_Engine *engine)
{
    (void)system;
    (void)engine;
    g_input = NULL;
}

static void input_system_update(Qs_System *system, Qs_Engine *engine, float dt)
{
    (void)system;
    (void)engine;
    (void)dt;
    if (!g_input) return;
    memcpy(g_input->previous, g_input->current, sizeof(g_input->current));
}

Qs_SystemDesc qs_input_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Input",
        .data_size = sizeof(Qs_InputState),
        .init      = input_system_init,
        .shutdown  = input_system_shutdown,
        .update    = input_system_update,
    };
}

void qs_input_key_event(Qs_Key key, Qs_KeyAction action, int mods)
{
    if (!g_input) return;
    if (key < 0 || key >= QS_KEY_MAX) return;

    const char *name = qs_key_name(key);
    const char *mods_str = "";
    if (mods & QS_MOD_CONTROL) mods_str = "Ctrl+";
    else if (mods & QS_MOD_SHIFT) mods_str = "Shift+";
    else if (mods & QS_MOD_ALT) mods_str = "Alt+";

    switch (action) {
    case QS_KEY_PRESS:
        g_input->current[key] = true;
        qs_log(QS_LOG_DEBUG, "Key Press: %s%s", mods_str, name);
        break;
    case QS_KEY_RELEASE:
        g_input->current[key] = false;
        qs_log(QS_LOG_DEBUG, "Key Release: %s%s", mods_str, name);
        break;
    case QS_KEY_REPEAT:
        qs_log(QS_LOG_TRACE, "Key Hold: %s%s", mods_str, name);
        break;
    }
}

bool qs_input_key_down(Qs_Key key)
{
    if (!g_input || key < 0 || key >= QS_KEY_MAX) return false;
    return g_input->current[key];
}

bool qs_input_key_pressed(Qs_Key key)
{
    if (!g_input || key < 0 || key >= QS_KEY_MAX) return false;
    return g_input->current[key] && !g_input->previous[key];
}

bool qs_input_key_released(Qs_Key key)
{
    if (!g_input || key < 0 || key >= QS_KEY_MAX) return false;
    return !g_input->current[key] && g_input->previous[key];
}

const char *qs_key_name(Qs_Key key)
{
    switch (key) {
    case QS_KEY_SPACE:         return "Space";
    case QS_KEY_APOSTROPHE:    return "'";
    case QS_KEY_COMMA:         return ",";
    case QS_KEY_MINUS:         return "-";
    case QS_KEY_PERIOD:        return ".";
    case QS_KEY_SLASH:         return "/";
    case QS_KEY_0: return "0"; case QS_KEY_1: return "1";
    case QS_KEY_2: return "2"; case QS_KEY_3: return "3";
    case QS_KEY_4: return "4"; case QS_KEY_5: return "5";
    case QS_KEY_6: return "6"; case QS_KEY_7: return "7";
    case QS_KEY_8: return "8"; case QS_KEY_9: return "9";
    case QS_KEY_SEMICOLON:     return ";";
    case QS_KEY_EQUAL:         return "=";
    case QS_KEY_A: return "A"; case QS_KEY_B: return "B";
    case QS_KEY_C: return "C"; case QS_KEY_D: return "D";
    case QS_KEY_E: return "E"; case QS_KEY_F: return "F";
    case QS_KEY_G: return "G"; case QS_KEY_H: return "H";
    case QS_KEY_I: return "I"; case QS_KEY_J: return "J";
    case QS_KEY_K: return "K"; case QS_KEY_L: return "L";
    case QS_KEY_M: return "M"; case QS_KEY_N: return "N";
    case QS_KEY_O: return "O"; case QS_KEY_P: return "P";
    case QS_KEY_Q: return "Q"; case QS_KEY_R: return "R";
    case QS_KEY_S: return "S"; case QS_KEY_T: return "T";
    case QS_KEY_U: return "U"; case QS_KEY_V: return "V";
    case QS_KEY_W: return "W"; case QS_KEY_X: return "X";
    case QS_KEY_Y: return "Y"; case QS_KEY_Z: return "Z";
    case QS_KEY_LEFT_BRACKET:  return "[";
    case QS_KEY_BACKSLASH:     return "\\";
    case QS_KEY_RIGHT_BRACKET: return "]";
    case QS_KEY_GRAVE_ACCENT:  return "`";
    case QS_KEY_ESCAPE:        return "Escape";
    case QS_KEY_ENTER:         return "Enter";
    case QS_KEY_TAB:           return "Tab";
    case QS_KEY_BACKSPACE:     return "Backspace";
    case QS_KEY_INSERT:        return "Insert";
    case QS_KEY_DELETE:        return "Delete";
    case QS_KEY_RIGHT:         return "Right";
    case QS_KEY_LEFT:          return "Left";
    case QS_KEY_DOWN:          return "Down";
    case QS_KEY_UP:            return "Up";
    case QS_KEY_PAGE_UP:       return "PageUp";
    case QS_KEY_PAGE_DOWN:     return "PageDown";
    case QS_KEY_HOME:          return "Home";
    case QS_KEY_END:           return "End";
    case QS_KEY_CAPS_LOCK:     return "CapsLock";
    case QS_KEY_SCROLL_LOCK:   return "ScrollLock";
    case QS_KEY_NUM_LOCK:      return "NumLock";
    case QS_KEY_PRINT_SCREEN:  return "PrintScreen";
    case QS_KEY_PAUSE:         return "Pause";
    case QS_KEY_F1:  return "F1";  case QS_KEY_F2:  return "F2";
    case QS_KEY_F3:  return "F3";  case QS_KEY_F4:  return "F4";
    case QS_KEY_F5:  return "F5";  case QS_KEY_F6:  return "F6";
    case QS_KEY_F7:  return "F7";  case QS_KEY_F8:  return "F8";
    case QS_KEY_F9:  return "F9";  case QS_KEY_F10: return "F10";
    case QS_KEY_F11: return "F11"; case QS_KEY_F12: return "F12";
    case QS_KEY_KP_0: return "KP0"; case QS_KEY_KP_1: return "KP1";
    case QS_KEY_KP_2: return "KP2"; case QS_KEY_KP_3: return "KP3";
    case QS_KEY_KP_4: return "KP4"; case QS_KEY_KP_5: return "KP5";
    case QS_KEY_KP_6: return "KP6"; case QS_KEY_KP_7: return "KP7";
    case QS_KEY_KP_8: return "KP8"; case QS_KEY_KP_9: return "KP9";
    case QS_KEY_KP_DECIMAL:    return "KP.";
    case QS_KEY_KP_DIVIDE:     return "KP/";
    case QS_KEY_KP_MULTIPLY:   return "KP*";
    case QS_KEY_KP_SUBTRACT:   return "KP-";
    case QS_KEY_KP_ADD:        return "KP+";
    case QS_KEY_KP_ENTER:      return "KPEnter";
    case QS_KEY_LEFT_SHIFT:    return "LShift";
    case QS_KEY_LEFT_CONTROL:  return "LCtrl";
    case QS_KEY_LEFT_ALT:      return "LAlt";
    case QS_KEY_LEFT_SUPER:    return "LSuper";
    case QS_KEY_RIGHT_SHIFT:   return "RShift";
    case QS_KEY_RIGHT_CONTROL: return "RCtrl";
    case QS_KEY_RIGHT_ALT:     return "RAlt";
    case QS_KEY_RIGHT_SUPER:   return "RSuper";
    case QS_KEY_MENU:          return "Menu";
    default:                   return "Unknown";
    }
}
