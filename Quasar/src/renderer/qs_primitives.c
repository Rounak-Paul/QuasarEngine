#include "qs_primitives.h"
#include "qs_mesh.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ================================================================
   PLANE  (XZ, Y-up normal)
   ================================================================ */

Qs_Mesh *qs_primitive_plane(Qs_Engine *engine, float size, uint32_t subdivs)
{
    if (subdivs < 1) subdivs = 1;
    uint32_t verts_per_side = subdivs + 1;
    uint32_t vert_count = verts_per_side * verts_per_side;
    uint32_t idx_count  = subdivs * subdivs * 6;

    Qs_Vertex *verts   = (Qs_Vertex *)calloc(vert_count, sizeof(Qs_Vertex));
    uint32_t  *indices = (uint32_t *)malloc(idx_count * sizeof(uint32_t));
    if (!verts || !indices) { free(verts); free(indices); return NULL; }

    float half = size * 0.5f;
    float step = size / (float)subdivs;

    for (uint32_t z = 0; z < verts_per_side; z++) {
        for (uint32_t x = 0; x < verts_per_side; x++) {
            Qs_Vertex *v = &verts[z * verts_per_side + x];
            v->position[0] = -half + (float)x * step;
            v->position[1] = 0.0f;
            v->position[2] = -half + (float)z * step;
            v->normal[1]   = 1.0f;
            v->tangent[0]  = 1.0f;
            v->tangent[3]  = 1.0f;
            v->uv[0]       = (float)x / (float)subdivs;
            v->uv[1]       = (float)z / (float)subdivs;
        }
    }

    uint32_t idx = 0;
    for (uint32_t z = 0; z < subdivs; z++) {
        for (uint32_t x = 0; x < subdivs; x++) {
            uint32_t tl = z * verts_per_side + x;
            uint32_t tr = tl + 1;
            uint32_t bl = tl + verts_per_side;
            uint32_t br = bl + 1;
            indices[idx++] = tl; indices[idx++] = bl; indices[idx++] = tr;
            indices[idx++] = tr; indices[idx++] = bl; indices[idx++] = br;
        }
    }

    Qs_Mesh *mesh = qs_mesh_create(engine, &(Qs_MeshDesc){
        .name         = "primitive_plane",
        .vertices     = verts,
        .vertex_count = vert_count,
        .indices      = indices,
        .index_count  = idx_count,
        .index_type   = QS_INDEX_TYPE_UINT32,
    });

    free(verts);
    free(indices);
    return mesh;
}

/* ================================================================
   CUBE
   ================================================================ */

Qs_Mesh *qs_primitive_cube(Qs_Engine *engine, float size)
{
    float h = size * 0.5f;

    /* 24 vertices (4 per face × 6 faces) with distinct normals */
    Qs_Vertex verts[24];
    memset(verts, 0, sizeof(verts));

    /* Helper: set one vertex */
    #define V(i, px,py,pz, nx,ny,nz, tx,ty,tz,tw, u,v_) do { \
        verts[i].position[0]=px; verts[i].position[1]=py; verts[i].position[2]=pz; \
        verts[i].normal[0]=nx; verts[i].normal[1]=ny; verts[i].normal[2]=nz;       \
        verts[i].tangent[0]=tx; verts[i].tangent[1]=ty; verts[i].tangent[2]=tz;     \
        verts[i].tangent[3]=tw; verts[i].uv[0]=u; verts[i].uv[1]=v_;               \
    } while(0)

    /* +Z face */
    V( 0, -h,-h, h,  0, 0, 1,  1,0,0,1,  0,1);
    V( 1,  h,-h, h,  0, 0, 1,  1,0,0,1,  1,1);
    V( 2,  h, h, h,  0, 0, 1,  1,0,0,1,  1,0);
    V( 3, -h, h, h,  0, 0, 1,  1,0,0,1,  0,0);
    /* -Z face */
    V( 4,  h,-h,-h,  0, 0,-1, -1,0,0,1,  0,1);
    V( 5, -h,-h,-h,  0, 0,-1, -1,0,0,1,  1,1);
    V( 6, -h, h,-h,  0, 0,-1, -1,0,0,1,  1,0);
    V( 7,  h, h,-h,  0, 0,-1, -1,0,0,1,  0,0);
    /* +X face */
    V( 8,  h,-h, h,  1, 0, 0,  0,0,-1,1,  0,1);
    V( 9,  h,-h,-h,  1, 0, 0,  0,0,-1,1,  1,1);
    V(10,  h, h,-h,  1, 0, 0,  0,0,-1,1,  1,0);
    V(11,  h, h, h,  1, 0, 0,  0,0,-1,1,  0,0);
    /* -X face */
    V(12, -h,-h,-h, -1, 0, 0,  0,0,1,1,   0,1);
    V(13, -h,-h, h, -1, 0, 0,  0,0,1,1,   1,1);
    V(14, -h, h, h, -1, 0, 0,  0,0,1,1,   1,0);
    V(15, -h, h,-h, -1, 0, 0,  0,0,1,1,   0,0);
    /* +Y face */
    V(16, -h, h, h,  0, 1, 0,  1,0,0,1,   0,1);
    V(17,  h, h, h,  0, 1, 0,  1,0,0,1,   1,1);
    V(18,  h, h,-h,  0, 1, 0,  1,0,0,1,   1,0);
    V(19, -h, h,-h,  0, 1, 0,  1,0,0,1,   0,0);
    /* -Y face */
    V(20, -h,-h,-h,  0,-1, 0,  1,0,0,1,   0,1);
    V(21,  h,-h,-h,  0,-1, 0,  1,0,0,1,   1,1);
    V(22,  h,-h, h,  0,-1, 0,  1,0,0,1,   1,0);
    V(23, -h,-h, h,  0,-1, 0,  1,0,0,1,   0,0);
    #undef V

    uint32_t indices[36];
    for (uint32_t f = 0; f < 6; f++) {
        uint32_t base = f * 4;
        uint32_t i    = f * 6;
        indices[i+0] = base+0; indices[i+1] = base+1; indices[i+2] = base+2;
        indices[i+3] = base+0; indices[i+4] = base+2; indices[i+5] = base+3;
    }

    return qs_mesh_create(engine, &(Qs_MeshDesc){
        .name         = "primitive_cube",
        .vertices     = verts,
        .vertex_count = 24,
        .indices      = indices,
        .index_count  = 36,
        .index_type   = QS_INDEX_TYPE_UINT32,
    });
}

/* ================================================================
   SPHERE  (UV sphere, +Y up)
   ================================================================ */

Qs_Mesh *qs_primitive_sphere(Qs_Engine *engine, float radius,
                              uint32_t slices, uint32_t stacks)
{
    if (slices < 3) slices = 3;
    if (stacks < 2) stacks = 2;

    uint32_t vert_count = (slices + 1) * (stacks + 1);
    uint32_t idx_count  = slices * stacks * 6;

    Qs_Vertex *verts   = (Qs_Vertex *)calloc(vert_count, sizeof(Qs_Vertex));
    uint32_t  *indices = (uint32_t *)malloc(idx_count * sizeof(uint32_t));
    if (!verts || !indices) { free(verts); free(indices); return NULL; }

    uint32_t vi = 0;
    for (uint32_t st = 0; st <= stacks; st++) {
        float phi = (float)M_PI * (float)st / (float)stacks;
        float sp  = sinf(phi), cp = cosf(phi);

        for (uint32_t sl = 0; sl <= slices; sl++) {
            float theta = 2.0f * (float)M_PI * (float)sl / (float)slices;
            float st2   = sinf(theta), ct = cosf(theta);

            float nx = sp * ct;
            float ny = cp;
            float nz = sp * st2;

            Qs_Vertex *v = &verts[vi++];
            v->position[0] = radius * nx;
            v->position[1] = radius * ny;
            v->position[2] = radius * nz;
            v->normal[0]   = nx;
            v->normal[1]   = ny;
            v->normal[2]   = nz;
            /* Tangent along theta */
            v->tangent[0]  = -st2;
            v->tangent[1]  =  0.0f;
            v->tangent[2]  =  ct;
            v->tangent[3]  =  1.0f;
            v->uv[0]       = (float)sl / (float)slices;
            v->uv[1]       = (float)st / (float)stacks;
        }
    }

    uint32_t ii = 0;
    for (uint32_t st = 0; st < stacks; st++) {
        for (uint32_t sl = 0; sl < slices; sl++) {
            uint32_t a = st * (slices + 1) + sl;
            uint32_t b = a + slices + 1;
            indices[ii++] = a;
            indices[ii++] = b;
            indices[ii++] = a + 1;
            indices[ii++] = a + 1;
            indices[ii++] = b;
            indices[ii++] = b + 1;
        }
    }

    Qs_Mesh *mesh = qs_mesh_create(engine, &(Qs_MeshDesc){
        .name         = "primitive_sphere",
        .vertices     = verts,
        .vertex_count = vert_count,
        .indices      = indices,
        .index_count  = idx_count,
        .index_type   = QS_INDEX_TYPE_UINT32,
    });

    free(verts);
    free(indices);
    return mesh;
}
