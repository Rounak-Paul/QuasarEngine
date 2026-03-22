#include "editor.h"
#include "ui/ed_project_launcher.h"

int main(void) {
    char project_path[1024];
    if (!ed_project_launcher_run(project_path, sizeof(project_path)))
        return 0;

    Editor *editor = editor_create(&(EditorDesc){
        .title        = "Quasar Editor",
        .project_path = project_path,
        .width        = 800,
        .height       = 480,
    });
    if (!editor) return 1;

    int result = editor_run(editor);
    editor_destroy(editor);
    return result;
}
