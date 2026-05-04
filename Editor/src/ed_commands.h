#ifndef ED_COMMANDS_H
#define ED_COMMANDS_H

#include "quasar.h"
#include <stdbool.h>

/* ================================================================
   ED_COMMANDS
   ----------------------------------------------------------------
   Editor "command" infrastructure: keybind dispatcher + undo/redo
   stack. Both subsystems are owned by the editor lifecycle and are
   typically activated together, so they share a header.

   Keybinds:
     Modifier matching is exact (Ctrl+Shift only fires for that exact
     combination, not Ctrl alone).  Caps/Num Lock are stripped before
     comparison.  Dispatched directly from the global key handler so
     shortcuts work regardless of which UI element has focus.

   Undo/Redo:
     Commands capture before/after snapshots of an atomic edit.
     Granularity is one command per user-visible action (e.g. one
     transform record per gizmo drag, not per delta).  Commands
     no-op on undo when the referenced entity no longer exists.
   ================================================================ */

/* ---------------- Keybinds ---------------- */

typedef void (*EdKeybindFn)(void *user_data);

void ed_keybinds_init(void);
void ed_keybinds_shutdown(void);

/// Registers a callback for `key` + `mods` (bitwise OR of Qs_KeyMod).
/// `label` is a short shortcut string ("Ctrl+S") for menu display;
/// the pointer must remain valid for the lifetime of the binding.
void ed_keybinds_register(int key, int mods,
                          EdKeybindFn fn, void *user_data,
                          const char *label);

/// Dispatches a key event.  Returns true if a binding consumed it.
/// Only QS_KEY_PRESS / QS_KEY_REPEAT events are dispatched.
bool ed_keybinds_dispatch(int key, int action, int mods);

/// Returns the registered shortcut label for a binding, or NULL.
const char *ed_keybinds_label_for(int key, int mods);

/* ---------------- Undo / Redo ---------------- */

typedef struct EdUndoCmd EdUndoCmd;

typedef void (*EdUndoApplyFn)(void *data);
typedef void (*EdUndoFreeFn)(void *data);

void ed_undo_init(void);
void ed_undo_shutdown(void);

/// Pushes a command onto the undo stack.  `redo_fn` writes the `after`
/// state, `undo_fn` writes the `before` state.  `free_fn` may be NULL
/// (falls back to free()).  `label` should be a static string.
void ed_undo_push(EdUndoApplyFn redo_fn,
                  EdUndoApplyFn undo_fn,
                  EdUndoFreeFn  free_fn,
                  void         *data,
                  const char   *label);

/// Pops the top command and applies its undo function.
bool ed_undo(void);

/// Pops the top redo command and applies its redo function.
bool ed_redo(void);

/// Drops both stacks (called on scene swap to avoid stale refs).
void ed_undo_clear(void);

const char *ed_undo_top_label(void);
const char *ed_redo_top_label(void);

/* ---- Typed pushers ---- */

void ed_undo_push_transform(Qs_Scene *scene, Qs_Entity entity,
                            const Qs_Transform *before,
                            const Qs_Transform *after);

void ed_undo_push_field(Qs_Scene *scene, Qs_Entity entity,
                        Qs_ComponentType *comp_type,
                        size_t field_offset, size_t field_size,
                        const void *before, const void *after);

void ed_undo_push_name(Qs_Scene *scene, Qs_Entity entity,
                       const char *before, const char *after);

void ed_undo_push_tag(Qs_Scene *scene, Qs_Entity entity,
                      const char *before, const char *after);

void ed_undo_push_override(Qs_PrototypeComp *pc,
                           uint32_t inner_entity_id,
                           const char *comp_name, const char *field_name,
                           Qs_FieldType type,
                           bool had_before,  const void *before_value,
                           bool clear_after, const void *after_value);

#endif /* ED_COMMANDS_H */
