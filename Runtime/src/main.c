#include "quasar.h"
#include "causality.h"

static Ca_Viewport *s_vp;

static void on_frame(void *userdata)
{
    Qs_Engine *engine = userdata;
    qs_engine_update(engine, 1.0f / 60.0f);
    ca_viewport_request_redraw(s_vp);
}

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

    Qs_SystemDesc render_desc = qs_render_system_desc(instance);
    if (!qs_system_register(qs_engine_systems(engine), &render_desc)) {
        ca_instance_destroy(instance);
        qs_engine_destroy(engine);
        return 1;
    }

    Qs_SystemDesc tex_desc = qs_texture_system_desc(instance);
    qs_system_register(qs_engine_systems(engine), &tex_desc);

    Qs_SystemDesc mesh_desc = qs_mesh_system_desc(instance);
    qs_system_register(qs_engine_systems(engine), &mesh_desc);

    Qs_SystemDesc mat_desc = qs_material_system_desc(instance);
    qs_system_register(qs_engine_systems(engine), &mat_desc);

    Qs_SystemDesc light_desc = qs_light_system_desc();
    qs_system_register(qs_engine_systems(engine), &light_desc);

    Qs_SystemDesc scene_desc = qs_scene_system_desc();
    qs_system_register(qs_engine_systems(engine), &scene_desc);

    Qs_Renderer *renderer = qs_renderer_create(engine, &(Qs_RendererDesc){
        .name        = "main",
        .clear_color = {{ 0.05f, 0.05f, 0.10f, 1.0f }},
        .depth_test  = true,
    });

    ca_ui_begin(window, &(Ca_DivDesc){
        .direction = CA_VERTICAL,
    });
    s_vp = ca_viewport(&(Ca_ViewportDesc){ 0 });
    qs_renderer_bind(renderer, s_vp);
    ca_ui_end();

    ca_window_set_on_frame(window, on_frame, engine);

    while (ca_instance_tick(instance)) { }

    qs_engine_destroy(engine);
    ca_instance_destroy(instance);
    return 0;
}
