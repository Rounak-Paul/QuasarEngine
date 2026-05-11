#ifndef QS_PRIMITIVE_H
#define QS_PRIMITIVE_H

#include "qs_mesh.h"
#include "qs_api.h"

/* ================================================================
   BUILT-IN ENGINE PRIMITIVES
   ================================================================

   Engine-managed primitive meshes are created once at startup and
   shared across all entities.  Reference them in MeshComp.mesh_path
   using the "@" prefix convention:

       "@cube"     — unit cube  (1×1×1, centred at origin)
       "@sphere"   — UV sphere  (radius 0.5, 32×16 segments)
       "@plane"    — unit plane (1×1, XZ, 10×10 quads)
       "@cylinder" — cylinder   (radius 0.5, height 1, 32 segments)

   These meshes are never destroyed while the engine is running and
   must NOT be passed to qs_mesh_destroy().
   ================================================================ */

typedef enum Qs_PrimitiveType {
    QS_PRIMITIVE_CUBE     = 0,
    QS_PRIMITIVE_SPHERE   = 1,
    QS_PRIMITIVE_PLANE    = 2,
    QS_PRIMITIVE_CYLINDER = 3,
    QS_PRIMITIVE_COUNT,
} Qs_PrimitiveType;

/// Returns the shared mesh for the requested primitive.
/// Returns NULL if the mesh system has not been initialised yet.
QS_API Qs_Mesh *qs_primitive_mesh(Qs_PrimitiveType type);

/// Returns the canonical mesh_path string for the given primitive
/// (e.g. "@cube").  Suitable for storing directly in MeshComp.mesh_path.
QS_API const char *qs_primitive_path(Qs_PrimitiveType type);

/// Returns the Qs_PrimitiveType for a "@"-prefixed path, or -1 if not a
/// recognised primitive path.
QS_API int qs_primitive_type_from_path(const char *path);

#endif /* QS_PRIMITIVE_H */
