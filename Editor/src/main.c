#include "editor.h"
#include "ui/ed_project_launcher.h"
#include "quasar.h"

/* Static UI allocator wrappers for the editor process. */
static void *ed_ui_malloc (size_t sz)           { return qs_malloc(sz, QS_MEM_UI); }
static void *ed_ui_calloc (size_t n, size_t sz) { return qs_calloc(n, sz, QS_MEM_UI); }
static void *ed_ui_realloc(void *p, size_t sz)  { return qs_realloc(p, sz, QS_MEM_UI); }

int main(void) {
    qs_mem_init();
    ca_set_allocator(ed_ui_malloc, ed_ui_calloc, ed_ui_realloc, qs_free);

    char project_path[1024];
    if (!ed_project_launcher_run(project_path, sizeof(project_path))) {
        qs_mem_shutdown();
        return 0;
    }

    Editor *editor = editor_create(&(EditorDesc){
        .title        = "Quasar Editor",
        .project_path = project_path,
        .width        = 1280,
        .height       = 720,
    });
    if (!editor) return 1;

    ca_window_maximize(qs_engine_window(editor_engine(editor)));

    int result = editor_run(editor);
    editor_destroy(editor);
    qs_mem_shutdown();
    return result;
}
