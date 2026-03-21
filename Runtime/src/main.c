#include "quasar.h"
#include "causality.h"

int main(void)
{
    Qs_Engine *engine = qs_engine_create(&(Qs_EngineDesc){
        .app_name      = "Quasar Runtime",
        .version_major = 0,
        .version_minor = 1,
        .version_patch = 0,
    });
    if (!engine) return 1;

    Ca_Instance *instance = ca_instance_create(&(Ca_InstanceDesc){
        .app_name     = "Quasar Runtime",
        .font_size_px = 14.0f,
    });
    if (!instance) { qs_engine_destroy(engine); return 1; }

    Ca_Window *window = ca_window_create(instance, &(Ca_WindowDesc){
        .title  = "Quasar Runtime",
        .width  = 1280,
        .height = 720,
    });
    if (!window) { ca_instance_destroy(instance); qs_engine_destroy(engine); return 1; }

    Qs_Renderer *renderer = qs_renderer_create(&(Qs_RendererDesc){
        .device      = ca_gpu_device(instance),
        .clear_color = {{ 0.05f, 0.05f, 0.10f, 1.0f }},
    });

    ca_ui_begin(window, &(Ca_DivDesc){
        .direction = CA_VERTICAL,
    });
    Ca_Viewport *vp = ca_viewport(&(Ca_ViewportDesc){ 0 });
    qs_renderer_bind(renderer, vp);
    ca_ui_end();

    int result = ca_instance_exec(instance);

    qs_renderer_destroy(renderer);
    qs_engine_destroy(engine);
    return result;
}
