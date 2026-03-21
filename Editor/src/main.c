#include "editor.h"

int main(void) {
    Editor *editor = editor_create(&(EditorDesc){
        .title  = "Quasar Editor",
        .width  = 1280,
        .height = 720,
    });
    if (!editor) return 1;

    int result = editor_run(editor);
    editor_destroy(editor);
    return result;
}
