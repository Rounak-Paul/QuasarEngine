#ifndef QS_PRIMITIVES_H
#define QS_PRIMITIVES_H

#include <stdint.h>

typedef struct Qs_Engine Qs_Engine;
typedef struct Qs_Mesh   Qs_Mesh;

/// Creates a flat XZ plane centered at origin.  size = total width/depth.
Qs_Mesh *qs_primitive_plane(Qs_Engine *engine, float size, uint32_t subdivisions);

/// Creates a unit cube (side length = size) centered at origin.
Qs_Mesh *qs_primitive_cube(Qs_Engine *engine, float size);

/// Creates a UV sphere centered at origin.
Qs_Mesh *qs_primitive_sphere(Qs_Engine *engine, float radius,
                              uint32_t slices, uint32_t stacks);

#endif
