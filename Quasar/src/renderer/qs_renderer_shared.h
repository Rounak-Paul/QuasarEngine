/* Engine-internal shared declarations for renderer dispatcher files.
   Not part of the public API — do not include outside Quasar/src/renderer/. */

#ifndef QS_RENDERER_SHARED_H
#define QS_RENDERER_SHARED_H

#include "qs_renderer.h"

/* Set by qs_renderer.c during render system init; used by the forward
   dispatcher (qs_forward.c) without adding a public API dependency. */
extern const Qs_RendererBackend *g_renderer_backend;
extern void                     *g_render_ctx;

#endif
