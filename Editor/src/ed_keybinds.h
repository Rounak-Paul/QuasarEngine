#ifndef ED_KEYBINDS_H
#define ED_KEYBINDS_H

#include "quasar.h"
#include <stdbool.h>

/// Editor keybind dispatcher.  Independent of which UI element has focus —
/// keybinds are dispatched directly from the global key event handler so
/// shortcuts like Ctrl+S work whether the user is hovering the viewport,
/// the hierarchy, or a text input.
///
/// Modifier matching is exact (key with Ctrl+Shift only fires for that
/// exact combination, not for Ctrl alone).  Caps Lock / Num Lock bits in
/// the OS-supplied mods are stripped before comparison.

typedef void (*EdKeybindFn)(void *user_data);

void ed_keybinds_init(void);
void ed_keybinds_shutdown(void);

/// Registers a callback for `key` + `mods` (bitwise OR of Qs_KeyMod).
/// `label` is a short human-readable shortcut string (e.g. "Ctrl+S")
/// that the menu bar can display alongside the action.  The label
/// pointer must remain valid for the lifetime of the binding.
void ed_keybinds_register(int key, int mods,
                          EdKeybindFn fn, void *user_data,
                          const char *label);

/// Dispatches a key event.  Returns true if a binding consumed it.
/// Only QS_KEY_PRESS / QS_KEY_REPEAT events are dispatched.
bool ed_keybinds_dispatch(int key, int action, int mods);

/// Returns the registered shortcut label for a binding, or NULL if no
/// binding matches.  Useful for the menu bar to display the chord.
const char *ed_keybinds_label_for(int key, int mods);

#endif
