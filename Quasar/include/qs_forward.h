#ifndef QS_FORWARD_H
#define QS_FORWARD_H

#include <stdbool.h>

typedef struct Qs_Engine   Qs_Engine;
typedef struct Qs_Renderer Qs_Renderer;

/// Creates the forward rendering pipeline and attaches it as a render node
/// to the given renderer.  Call once after scene system is ready.
/// Returns false on failure.
bool qs_forward_init(Qs_Engine *engine, Qs_Renderer *renderer);

/// Destroys the forward pipeline GPU resources.
void qs_forward_shutdown(void);

#endif
