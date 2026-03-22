#include "quasar.h"

static Ca_Viewport *s_vp;

static void on_frame(Qs_Engine *engine, void *userdata)
{
    (void)engine;
    (void)userdata;
    ca_viewport_request_redraw(s_vp);
}

int main(void)
{
    Qs_Engine *engine = qs_engine_create(&(Qs_EngineDesc){
        .app_name      = "Quasar Runtime",
        .version_major = 0,
        .version_minor = 1,
        .version_patch = 0,
        .window_width  = 1280,
        .window_height = 720,
    });
    if (!engine) return 1;

    Qs_Renderer *renderer = qs_renderer_create(engine, &(Qs_RendererDesc){
        .name        = "main",
        .clear_color = {{ 0.05f, 0.05f, 0.10f, 1.0f }},
        .depth_test  = true,
    });

    ca_ui_begin(qs_engine_window(engine), &(Ca_DivDesc){
        .direction = CA_VERTICAL,
    });
    s_vp = ca_viewport(&(Ca_ViewportDesc){ 0 });
    qs_renderer_bind(renderer, s_vp);
    ca_ui_end();

    qs_engine_set_on_frame(engine, on_frame, NULL);
    qs_engine_run(engine);
    qs_engine_destroy(engine);
    return 0;
}
