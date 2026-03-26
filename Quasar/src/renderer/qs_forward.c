#include "qs_forward.h"
#include "qs_renderer_shared.h"

bool qs_forward_init(Qs_Engine *engine, Qs_Renderer *renderer)
{
    if (!g_renderer_backend || !g_renderer_backend->forward_init) return false;
    return g_renderer_backend->forward_init(engine, renderer, g_render_ctx);
}

void qs_forward_shutdown(void)
{
    if (!g_renderer_backend || !g_renderer_backend->forward_shutdown) return;
    g_renderer_backend->forward_shutdown(g_render_ctx);
}
