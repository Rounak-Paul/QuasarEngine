#include "ed_keybinds.h"

#include <stdlib.h>
#include <string.h>

#define ED_KEYBINDS_MAX 64

typedef struct EdKeybind {
    int          key;
    int          mods;       ///< Normalised modifier mask (caps/num lock stripped).
    EdKeybindFn  fn;
    void        *user_data;
    const char  *label;
} EdKeybind;

static EdKeybind s_binds[ED_KEYBINDS_MAX];
static uint32_t  s_count;

/* GLFW exposes Caps Lock (0x10) and Num Lock (0x20) bits in the mods
   field.  We don't want them to affect matching, so strip them. */
#define MOD_MASK (QS_MOD_SHIFT | QS_MOD_CONTROL | QS_MOD_ALT | QS_MOD_SUPER)

void ed_keybinds_init(void)
{
    s_count = 0;
    memset(s_binds, 0, sizeof(s_binds));
}

void ed_keybinds_shutdown(void)
{
    s_count = 0;
}

void ed_keybinds_register(int key, int mods,
                          EdKeybindFn fn, void *user_data,
                          const char *label)
{
    if (!fn || s_count >= ED_KEYBINDS_MAX) return;
    /* Replace existing binding for the same chord (avoids duplicate
       handlers if init is called twice). */
    int normalised = mods & MOD_MASK;
    for (uint32_t i = 0; i < s_count; i++) {
        if (s_binds[i].key == key && s_binds[i].mods == normalised) {
            s_binds[i].fn        = fn;
            s_binds[i].user_data = user_data;
            s_binds[i].label     = label;
            return;
        }
    }
    s_binds[s_count++] = (EdKeybind){
        .key       = key,
        .mods      = normalised,
        .fn        = fn,
        .user_data = user_data,
        .label     = label,
    };
}

bool ed_keybinds_dispatch(int key, int action, int mods)
{
    if (action != QS_KEY_PRESS && action != QS_KEY_REPEAT) return false;
    int normalised = mods & MOD_MASK;
    for (uint32_t i = 0; i < s_count; i++) {
        if (s_binds[i].key == key && s_binds[i].mods == normalised) {
            s_binds[i].fn(s_binds[i].user_data);
            return true;
        }
    }
    return false;
}

const char *ed_keybinds_label_for(int key, int mods)
{
    int normalised = mods & MOD_MASK;
    for (uint32_t i = 0; i < s_count; i++) {
        if (s_binds[i].key == key && s_binds[i].mods == normalised)
            return s_binds[i].label;
    }
    return NULL;
}
