#ifndef ED_UNDO_H
#define ED_UNDO_H

#include "quasar.h"
#include <stdbool.h>

/// Generic undo/redo stack for the editor.
///
/// The system is built around small command records that capture the
/// `before` and `after` state of an atomic edit.  Apply functions write
/// `before` (undo) or `after` (redo) back into the live scene without
/// re-emitting commands, so the stack is always consistent.
///
/// Granularity: each command represents one user-visible action.  E.g.
/// dragging the gizmo produces ONE transform command on mouse release,
/// not one per delta.  Inspector text edits produce ONE field command
/// per focus session (push happens on focus loss / Enter).
///
/// Commands reference scenes and entities by raw pointer / id.  If an
/// entity referenced by a command is destroyed later, the command silently
/// no-ops on undo — that case can only arise when the user destroys an
/// entity, and we currently do not stack-undo through entity destruction.

typedef struct EdUndoCmd EdUndoCmd;

typedef void (*EdUndoApplyFn)(void *data);
typedef void (*EdUndoFreeFn)(void *data);

void ed_undo_init(void);
void ed_undo_shutdown(void);

/// Pushes a command onto the undo stack.  `redo_fn` writes the `after`
/// state, `undo_fn` writes the `before` state, `free_fn` releases `data`
/// (may be NULL for trivial structs allocated with malloc — the system
/// will fall back to free()).  `label` should be a static string.
void ed_undo_push(EdUndoApplyFn redo_fn,
                  EdUndoApplyFn undo_fn,
                  EdUndoFreeFn  free_fn,
                  void         *data,
                  const char   *label);

/// Pops the top command and applies its undo function.  Pushes onto the
/// redo stack.  Returns true if a command was undone.
bool ed_undo(void);

/// Pops the top redo command and applies its redo function.  Pushes back
/// onto the undo stack.  Returns true if a command was redone.
bool ed_redo(void);

/// Drops both stacks (called on scene swap to avoid stale entity refs).
void ed_undo_clear(void);

/// Top-of-stack labels for menu display (or NULL when empty).
const char *ed_undo_top_label(void);
const char *ed_redo_top_label(void);

/* ------------------------------------------------------------------ */
/* Helpers — typed command pushers used by editing UIs                 */
/* ------------------------------------------------------------------ */

/// Captures a Transform component snapshot (position/rotation/scale) and
/// pushes a command that restores `before` on undo and `after` on redo.
/// Identical before/after states are skipped.
void ed_undo_push_transform(Qs_Scene *scene, Qs_Entity entity,
                            const Qs_Transform *before,
                            const Qs_Transform *after);

/// Generic component field edit.  Copies up to `size` bytes of `before`
/// and `after`.  `comp_type` and field offset/size are taken from
/// reflection by the caller.
void ed_undo_push_field(Qs_Scene *scene, Qs_Entity entity,
                        Qs_ComponentType *comp_type,
                        size_t field_offset, size_t field_size,
                        const void *before, const void *after);

/// Entity name edit.
void ed_undo_push_name(Qs_Scene *scene, Qs_Entity entity,
                       const char *before, const char *after);

/// Tag component edit.
void ed_undo_push_tag(Qs_Scene *scene, Qs_Entity entity,
                      const char *before, const char *after);

/// Prototype-instance override edit.  When `had_before` is false the undo
/// step clears the override (it didn't exist before); when true it
/// restores `before_value`.  When `clear_after` is true the redo step
/// clears the override; otherwise it sets `after_value`.
void ed_undo_push_override(Qs_PrototypeComp *pc,
                           uint32_t inner_entity_id,
                           const char *comp_name, const char *field_name,
                           Qs_FieldType type,
                           bool had_before,  const void *before_value,
                           bool clear_after, const void *after_value);

#endif
