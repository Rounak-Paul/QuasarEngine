#include "quasar.h"

int main(void) {
    Qs_Engine* engine = qs_engine_create(&(Qs_EngineDesc){
        .app_name      = "Quasar Runtime",
        .version_major = 0,
        .version_minor = 1,
        .version_patch = 0,
    });

    qs_engine_destroy(engine);
    return 0;
}
