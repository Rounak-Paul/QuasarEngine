#include "ed_undo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ED_UNDO_MAX_DEPTH 128

struct EdUndoCmd {
    EdUndoApplyFn redo_fn;
    EdUndoApplyFn undo_fn;
    EdUndoFreeFn  free_fn;
    void         *data;
    const char   *label;
};

/* Two ring-bounded LIFO stacks.  When the undo stack overflows we drop
   the oldest command (and free its data). */
static EdUndoCmd s_undo_stack[ED_UNDO_MAX_DEPTH];
static EdUndoCmd s_redo_stack[ED_UNDO_MAX_DEPTH];
static uint32_t  s_undo_count;
static uint32_t  s_redo_count;

/* ------------------------------------------------------------------ */

static void cmd_destroy(EdUndoCmd *c)
{
    if (!c) return;
    if (c->free_fn) c->free_fn(c->data);
    else            free(c->data);
    memset(c, 0, sizeof(*c));
}

static void clear_stack(EdUndoCmd *stack, uint32_t *count)
{
    while (*count > 0) {
        (*count)--;
        cmd_destroy(&stack[*count]);
    }
}

void ed_undo_init(void)
{
    s_undo_count = 0;
    s_redo_count = 0;
    memset(s_undo_stack, 0, sizeof(s_undo_stack));
    memset(s_redo_stack, 0, sizeof(s_redo_stack));
}

void ed_undo_shutdown(void)
{
    ed_undo_clear();
}

void ed_undo_clear(void)
{
    clear_stack(s_undo_stack, &s_undo_count);
    clear_stack(s_redo_stack, &s_redo_count);
}

void ed_undo_push(EdUndoApplyFn redo_fn,
                  EdUndoApplyFn undo_fn,
                  EdUndoFreeFn  free_fn,
                  void         *data,
                  const char   *label)
{
    if (!redo_fn || !undo_fn || !data) {
        if (data) {
            if (free_fn) free_fn(data);
            else         free(data);
        }
        return;
    }

    /* New action invalidates the redo branch. */
    clear_stack(s_redo_stack, &s_redo_count);

    if (s_undo_count == ED_UNDO_MAX_DEPTH) {
        /* Drop the oldest entry (bottom of stack). */
        cmd_destroy(&s_undo_stack[0]);
        memmove(&s_undo_stack[0], &s_undo_stack[1],
                sizeof(EdUndoCmd) * (ED_UNDO_MAX_DEPTH - 1));
        s_undo_count--;
    }

    s_undo_stack[s_undo_count++] = (EdUndoCmd){
        .redo_fn = redo_fn,
        .undo_fn = undo_fn,
        .free_fn = free_fn,
        .data    = data,
        .label   = label,
    };
}

bool ed_undo(void)
{
    if (s_undo_count == 0) return false;
    EdUndoCmd cmd = s_undo_stack[--s_undo_count];
    memset(&s_undo_stack[s_undo_count], 0, sizeof(EdUndoCmd));

    cmd.undo_fn(cmd.data);

    if (s_redo_count < ED_UNDO_MAX_DEPTH) {
        s_redo_stack[s_redo_count++] = cmd;
    } else {
        cmd_destroy(&cmd);
    }
    QS_LOG_INFO("Undo: %s", cmd.label ? cmd.label : "(unnamed)");
    return true;
}

bool ed_redo(void)
{
    if (s_redo_count == 0) return false;
    EdUndoCmd cmd = s_redo_stack[--s_redo_count];
    memset(&s_redo_stack[s_redo_count], 0, sizeof(EdUndoCmd));

    cmd.redo_fn(cmd.data);

    if (s_undo_count < ED_UNDO_MAX_DEPTH) {
        s_undo_stack[s_undo_count++] = cmd;
    } else {
        cmd_destroy(&cmd);
    }
    QS_LOG_INFO("Redo: %s", cmd.label ? cmd.label : "(unnamed)");
    return true;
}

const char *ed_undo_top_label(void)
{
    return s_undo_count ? s_undo_stack[s_undo_count - 1].label : NULL;
}

const char *ed_redo_top_label(void)
{
    return s_redo_count ? s_redo_stack[s_redo_count - 1].label : NULL;
}

/* ================================================================
   TYPED COMMAND HELPERS
   ================================================================ */

/* ---- Transform ---- */

typedef struct {
    Qs_Scene     *scene;
    Qs_Entity     entity;
    Qs_Transform  before;
    Qs_Transform  after;
} TransformCmd;

static void apply_transform(TransformCmd *c, const Qs_Transform *src)
{
    if (!c || !c->scene || !qs_entity_valid(c->scene, c->entity)) return;
    Qs_Transform *tr = (Qs_Transform *)qs_entity_get(
        c->scene, c->entity, qs_transform_type());
    if (!tr) return;
    *tr = *src;
}
static void transform_redo(void *d) { apply_transform((TransformCmd *)d, &((TransformCmd *)d)->after); }
static void transform_undo(void *d) { apply_transform((TransformCmd *)d, &((TransformCmd *)d)->before); }

void ed_undo_push_transform(Qs_Scene *scene, Qs_Entity entity,
                            const Qs_Transform *before,
                            const Qs_Transform *after)
{
    if (!scene || !before || !after) return;
    if (memcmp(before, after, sizeof(Qs_Transform)) == 0) return;
    TransformCmd *c = (TransformCmd *)calloc(1, sizeof(*c));
    if (!c) return;
    c->scene  = scene;
    c->entity = entity;
    c->before = *before;
    c->after  = *after;
    ed_undo_push(transform_redo, transform_undo, NULL, c, "Edit Transform");
}

/* ---- Generic field ---- */

typedef struct {
    Qs_Scene         *scene;
    Qs_Entity         entity;
    Qs_ComponentType *comp_type;
    size_t            offset;
    size_t            size;
    /* Variable-length tail: [size bytes before][size bytes after] */
} FieldCmd;

static void apply_field(FieldCmd *c, bool to_after)
{
    if (!c || !c->scene || !qs_entity_valid(c->scene, c->entity)) return;
    void *comp = qs_entity_get(c->scene, c->entity, c->comp_type);
    if (!comp) return;
    char *src = (char *)(c + 1) + (to_after ? c->size : 0);
    memcpy((char *)comp + c->offset, src, c->size);
}
static void field_redo(void *d) { apply_field((FieldCmd *)d, true);  }
static void field_undo(void *d) { apply_field((FieldCmd *)d, false); }

void ed_undo_push_field(Qs_Scene *scene, Qs_Entity entity,
                        Qs_ComponentType *comp_type,
                        size_t field_offset, size_t field_size,
                        const void *before, const void *after)
{
    if (!scene || !comp_type || !before || !after || field_size == 0) return;
    if (memcmp(before, after, field_size) == 0) return;
    FieldCmd *c = (FieldCmd *)calloc(1, sizeof(*c) + field_size * 2);
    if (!c) return;
    c->scene     = scene;
    c->entity    = entity;
    c->comp_type = comp_type;
    c->offset    = field_offset;
    c->size      = field_size;
    memcpy((char *)(c + 1),               before, field_size);
    memcpy((char *)(c + 1) + field_size,  after,  field_size);
    ed_undo_push(field_redo, field_undo, NULL, c, "Edit Field");
}

/* ---- Entity name ---- */

typedef struct {
    Qs_Scene  *scene;
    Qs_Entity  entity;
    /* Variable-length tail: NUL-terminated before then NUL-terminated after. */
} NameCmd;

static const char *name_after(const NameCmd *c)
{
    const char *before = (const char *)(c + 1);
    return before + strlen(before) + 1;
}
static void apply_name(NameCmd *c, const char *str)
{
    if (!c || !c->scene || !qs_entity_valid(c->scene, c->entity)) return;
    qs_entity_set_name(c->scene, c->entity, str);
}
static void name_redo(void *d) { apply_name((NameCmd *)d, name_after((NameCmd *)d)); }
static void name_undo(void *d) { apply_name((NameCmd *)d, (const char *)((NameCmd *)d + 1)); }

void ed_undo_push_name(Qs_Scene *scene, Qs_Entity entity,
                       const char *before, const char *after)
{
    if (!scene || !before || !after) return;
    if (strcmp(before, after) == 0) return;
    size_t lb = strlen(before) + 1, la = strlen(after) + 1;
    NameCmd *c = (NameCmd *)calloc(1, sizeof(*c) + lb + la);
    if (!c) return;
    c->scene  = scene;
    c->entity = entity;
    memcpy((char *)(c + 1),      before, lb);
    memcpy((char *)(c + 1) + lb, after,  la);
    ed_undo_push(name_redo, name_undo, NULL, c, "Edit Name");
}

/* ---- Tag ---- */

typedef struct {
    Qs_Scene *scene;
    Qs_Entity entity;
    /* Variable-length tail: same layout as NameCmd */
} TagCmd;

static const char *tag_after(const TagCmd *c)
{
    const char *before = (const char *)(c + 1);
    return before + strlen(before) + 1;
}
static void apply_tag(TagCmd *c, const char *str)
{
    if (!c || !c->scene || !qs_entity_valid(c->scene, c->entity)) return;
    Qs_TagComp *tg = (Qs_TagComp *)qs_entity_get(
        c->scene, c->entity, qs_tag_comp_type());
    if (!tg) return;
    snprintf(tg->tag, sizeof(tg->tag), "%s", str ? str : "");
}
static void tag_redo(void *d) { apply_tag((TagCmd *)d, tag_after((TagCmd *)d)); }
static void tag_undo(void *d) { apply_tag((TagCmd *)d, (const char *)((TagCmd *)d + 1)); }

void ed_undo_push_tag(Qs_Scene *scene, Qs_Entity entity,
                      const char *before, const char *after)
{
    if (!scene || !before || !after) return;
    if (strcmp(before, after) == 0) return;
    size_t lb = strlen(before) + 1, la = strlen(after) + 1;
    TagCmd *c = (TagCmd *)calloc(1, sizeof(*c) + lb + la);
    if (!c) return;
    c->scene  = scene;
    c->entity = entity;
    memcpy((char *)(c + 1),      before, lb);
    memcpy((char *)(c + 1) + lb, after,  la);
    ed_undo_push(tag_redo, tag_undo, NULL, c, "Edit Tag");
}

/* ---- Prototype override ---- */

typedef struct {
    Qs_PrototypeComp *pc;
    uint32_t          inner_id;
    char              comp_name[32];
    char              field_name[32];
    Qs_FieldType      type;
    bool              had_before;
    bool              clear_after;
    /* Followed by [value_size bytes before][value_size bytes after],
       sized to the largest possible override value (sizeof union). */
    union {
        float    fv[4];
        int32_t  iv;
        uint32_t uv;
        bool     bv;
        char     sv[256];
    } before_value;
    union {
        float    fv[4];
        int32_t  iv;
        uint32_t uv;
        bool     bv;
        char     sv[256];
    } after_value;
} OverrideCmd;

static void apply_override(OverrideCmd *c, bool to_after)
{
    if (!c || !c->pc) return;
    bool clear  = to_after ? c->clear_after  : !c->had_before;
    if (clear) {
        qs_prototype_clear_override(c->pc, c->inner_id, c->comp_name, c->field_name);
        return;
    }
    const void *val = to_after ? (const void *)&c->after_value
                               : (const void *)&c->before_value;
    if (c->type == QS_FIELD_STRING)
        qs_prototype_set_override(c->pc, c->inner_id, c->comp_name,
                                  c->field_name, c->type,
                                  to_after ? c->after_value.sv : c->before_value.sv);
    else
        qs_prototype_set_override(c->pc, c->inner_id, c->comp_name,
                                  c->field_name, c->type, val);
}
static void override_redo(void *d) { apply_override((OverrideCmd *)d, true);  }
static void override_undo(void *d) { apply_override((OverrideCmd *)d, false); }

void ed_undo_push_override(Qs_PrototypeComp *pc,
                           uint32_t inner_entity_id,
                           const char *comp_name, const char *field_name,
                           Qs_FieldType type,
                           bool had_before,  const void *before_value,
                           bool clear_after, const void *after_value)
{
    if (!pc || !comp_name || !field_name) return;
    OverrideCmd *c = (OverrideCmd *)calloc(1, sizeof(*c));
    if (!c) return;
    c->pc          = pc;
    c->inner_id    = inner_entity_id;
    c->type        = type;
    c->had_before  = had_before;
    c->clear_after = clear_after;
    snprintf(c->comp_name,  sizeof(c->comp_name),  "%s", comp_name);
    snprintf(c->field_name, sizeof(c->field_name), "%s", field_name);
    if (had_before  && before_value) memcpy(&c->before_value, before_value, sizeof(c->before_value));
    if (!clear_after && after_value) memcpy(&c->after_value,  after_value,  sizeof(c->after_value));
    ed_undo_push(override_redo, override_undo, NULL, c, "Edit Override");
}
