/*
 * gltf_loader.c — glTF/GLB asset importer for the Quasar engine.
 *
 * Implements the Qs_AssetImporterExt interface.  The import() callback
 * runs on a worker thread — it only performs file I/O, parsing, and
 * image decoding.  No GPU or engine API calls.
 *
 * Design:
 *   - Each glTF primitive becomes one Qs_ImportMesh.
 *   - Each glTF material becomes one Qs_ImportMaterial.
 *   - Each unique glTF image is decoded once into a Qs_ImportTexture.
 *   - Scene graph nodes are preserved with local transforms.
 *     Multi-primitive meshes expand into sibling nodes.
 */

#include "gltf_loader.h"

#include "cgltf.h"
#include "stb_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ================================================================
   HELPERS
   ================================================================ */

static void path_dir(const char *path, char *buf, size_t size)
{
    if (!path || !buf || size == 0) return;
    const char *last = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') last = p;
    }
    if (!last) {
        snprintf(buf, size, ".");
    } else {
        size_t len = (size_t)(last - path);
        if (len >= size) len = size - 1;
        memcpy(buf, path, len);
        buf[len] = '\0';
    }
}

static void make_mesh_name(const cgltf_mesh *gm, cgltf_size prim_idx,
                           char *out, size_t size)
{
    if (gm->name && gm->name[0]) {
        if (gm->primitives_count > 1)
            snprintf(out, size, "%s.%zu", gm->name, prim_idx);
        else
            snprintf(out, size, "%s", gm->name);
    } else {
        snprintf(out, size, "mesh_%zu.%zu",
                 (size_t)(gm - (const cgltf_mesh*)0), prim_idx);
    }
}

static uint32_t map_filter(cgltf_int f)
{
    switch (f) {
    case 9728: case 9984: case 9985:
        return (uint32_t)QS_TEXTURE_FILTER_NEAREST;
    default:
        return (uint32_t)QS_TEXTURE_FILTER_LINEAR;
    }
}

static uint32_t map_wrap(cgltf_int w)
{
    switch (w) {
    case 33071: return (uint32_t)QS_TEXTURE_WRAP_CLAMP_TO_EDGE;
    case 33648: return (uint32_t)QS_TEXTURE_WRAP_MIRRORED_REPEAT;
    default:    return (uint32_t)QS_TEXTURE_WRAP_REPEAT;
    }
}

/* ================================================================
   TEXTURE DECODING (CPU only)
   ================================================================ */

static bool decode_texture(const cgltf_image *img, const char *dir_path,
                           bool srgb, Qs_ImportTexture *out)
{
    int w = 0, h = 0, channels = 0;
    stbi_uc *pixels = NULL;

    if (img->uri && strncmp(img->uri, "data:", 5) != 0) {
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, img->uri);
        pixels = stbi_load(full_path, &w, &h, &channels, 4);
        snprintf(out->name, sizeof(out->name), "%s",
                 img->name ? img->name : img->uri);
    } else if (img->buffer_view && img->buffer_view->buffer &&
               img->buffer_view->buffer->data) {
        const unsigned char *data =
            (const unsigned char *)img->buffer_view->buffer->data +
            img->buffer_view->offset;
        size_t size = img->buffer_view->size;
        pixels = stbi_load_from_memory(data, (int)size, &w, &h, &channels, 4);
        snprintf(out->name, sizeof(out->name), "%s",
                 img->name ? img->name : "embedded");
    }

    if (!pixels) return false;

    out->width         = (uint32_t)w;
    out->height        = (uint32_t)h;
    out->format        = srgb ? (uint32_t)QS_TEXTURE_FORMAT_RGBA8_SRGB
                              : (uint32_t)QS_TEXTURE_FORMAT_RGBA8_UNORM;
    out->pixels        = pixels;
    out->generate_mips = true;
    out->srgb          = srgb;
    return true;
}

/* ================================================================
   VERTEX / INDEX BUILDING (CPU only)
   ================================================================ */

static bool read_float(const cgltf_accessor *acc, cgltf_size index,
                       float *out, cgltf_size n)
{
    if (!acc) return false;
    return (bool)cgltf_accessor_read_float(acc, index, out, n);
}

static Qs_Vertex *build_vertices(const cgltf_primitive *prim, uint32_t *out_count)
{
    const cgltf_accessor *pos_acc  = NULL;
    const cgltf_accessor *nrm_acc  = NULL;
    const cgltf_accessor *tan_acc  = NULL;
    const cgltf_accessor *uv0_acc  = NULL;

    for (cgltf_size a = 0; a < prim->attributes_count; a++) {
        const cgltf_attribute *attr = &prim->attributes[a];
        if (attr->index != 0) continue;
        switch (attr->type) {
        case cgltf_attribute_type_position: pos_acc = attr->data; break;
        case cgltf_attribute_type_normal:   nrm_acc = attr->data; break;
        case cgltf_attribute_type_tangent:  tan_acc = attr->data; break;
        case cgltf_attribute_type_texcoord: uv0_acc = attr->data; break;
        default: break;
        }
    }

    if (!pos_acc || pos_acc->count == 0) return NULL;

    uint32_t count = (uint32_t)pos_acc->count;
    Qs_Vertex *verts = (Qs_Vertex *)calloc(count, sizeof(Qs_Vertex));
    if (!verts) return NULL;

    for (uint32_t i = 0; i < count; i++) {
        Qs_Vertex *v = &verts[i];
        read_float(pos_acc, i, v->position, 3);
        if (nrm_acc) read_float(nrm_acc, i, v->normal, 3);
        else { v->normal[0] = 0; v->normal[1] = 1; v->normal[2] = 0; }
        if (tan_acc) read_float(tan_acc, i, v->tangent, 4);
        else { v->tangent[0]=1; v->tangent[1]=0; v->tangent[2]=0; v->tangent[3]=1; }
        if (uv0_acc) read_float(uv0_acc, i, v->uv, 2);
    }

    *out_count = count;
    return verts;
}

static uint32_t *build_indices(const cgltf_primitive *prim, uint32_t *out_count)
{
    if (!prim->indices || prim->indices->count == 0) {
        *out_count = 0;
        return NULL;
    }

    uint32_t count = (uint32_t)prim->indices->count;
    uint32_t *idx = (uint32_t *)malloc(count * sizeof(uint32_t));
    if (!idx) return NULL;

    for (uint32_t i = 0; i < count; i++)
        idx[i] = (uint32_t)cgltf_accessor_read_index(prim->indices, i);

    *out_count = count;
    return idx;
}

/* ================================================================
   MATERIAL BUILDING (CPU only → Qs_ImportMaterial)
   ================================================================ */

/* Resolve a cgltf_texture_view to an image index to be used as a
   texture index in the import result.  Image cache maps 1:1 with
   out->textures[].  Returns UINT32_MAX if no texture. */
static uint32_t resolve_image_index(const cgltf_data *gd,
                                    const cgltf_texture_view *tv,
                                    bool srgb,
                                    const char *dir_path,
                                    Qs_ImportTexture *tex_arr,
                                    bool *decoded,
                                    uint32_t image_count)
{
    if (!tv->texture || !tv->texture->image) return UINT32_MAX;

    cgltf_size img_idx = cgltf_image_index(gd, tv->texture->image);
    if (img_idx >= image_count) return UINT32_MAX;

    if (!decoded[img_idx]) {
        decode_texture(tv->texture->image, dir_path, srgb, &tex_arr[img_idx]);

        /* Apply sampler settings */
        if (tv->texture->sampler) {
            tex_arr[img_idx].min_filter = map_filter(tv->texture->sampler->min_filter);
            tex_arr[img_idx].mag_filter = map_filter(tv->texture->sampler->mag_filter);
            tex_arr[img_idx].wrap_u     = map_wrap(tv->texture->sampler->wrap_s);
            tex_arr[img_idx].wrap_v     = map_wrap(tv->texture->sampler->wrap_t);
        } else {
            tex_arr[img_idx].min_filter = (uint32_t)QS_TEXTURE_FILTER_LINEAR;
            tex_arr[img_idx].mag_filter = (uint32_t)QS_TEXTURE_FILTER_LINEAR;
            tex_arr[img_idx].wrap_u     = (uint32_t)QS_TEXTURE_WRAP_REPEAT;
            tex_arr[img_idx].wrap_v     = (uint32_t)QS_TEXTURE_WRAP_REPEAT;
        }

        decoded[img_idx] = true;
    }

    return (uint32_t)img_idx;
}

static void fill_import_material(const cgltf_data *gd,
                                 const cgltf_material *gm,
                                 const char *dir_path,
                                 Qs_ImportTexture *tex_arr,
                                 bool *decoded,
                                 uint32_t image_count,
                                 Qs_ImportMaterial *out)
{
    memset(out, 0, sizeof(*out));

    if (gm->name && gm->name[0])
        snprintf(out->name, sizeof(out->name), "%s", gm->name);
    else
        snprintf(out->name, sizeof(out->name), "material_%zu",
                 cgltf_material_index(gd, gm));

    out->base_color[0] = 1; out->base_color[1] = 1;
    out->base_color[2] = 1; out->base_color[3] = 1;
    out->roughness = 1.0f;
    out->normal_scale = 1.0f;
    out->occlusion_strength = 1.0f;
    out->alpha_cutoff = 0.5f;

    out->base_color_tex         = UINT32_MAX;
    out->metallic_roughness_tex = UINT32_MAX;
    out->normal_tex             = UINT32_MAX;
    out->occlusion_tex          = UINT32_MAX;
    out->emissive_tex           = UINT32_MAX;

    if (gm->has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness *pbr = &gm->pbr_metallic_roughness;
        out->base_color[0] = pbr->base_color_factor[0];
        out->base_color[1] = pbr->base_color_factor[1];
        out->base_color[2] = pbr->base_color_factor[2];
        out->base_color[3] = pbr->base_color_factor[3];
        out->metallic  = pbr->metallic_factor;
        out->roughness = pbr->roughness_factor;

        out->base_color_tex = resolve_image_index(
            gd, &pbr->base_color_texture, true, dir_path,
            tex_arr, decoded, image_count);
        out->metallic_roughness_tex = resolve_image_index(
            gd, &pbr->metallic_roughness_texture, false, dir_path,
            tex_arr, decoded, image_count);
    }

    out->normal_tex = resolve_image_index(
        gd, &gm->normal_texture, false, dir_path,
        tex_arr, decoded, image_count);
    out->occlusion_tex = resolve_image_index(
        gd, &gm->occlusion_texture, false, dir_path,
        tex_arr, decoded, image_count);
    out->emissive_tex = resolve_image_index(
        gd, &gm->emissive_texture, true, dir_path,
        tex_arr, decoded, image_count);

    out->normal_scale = gm->normal_texture.scale > 0.0f
                      ? gm->normal_texture.scale : 1.0f;
    out->occlusion_strength = gm->occlusion_texture.scale > 0.0f
                            ? gm->occlusion_texture.scale : 1.0f;

    out->emissive[0] = gm->emissive_factor[0];
    out->emissive[1] = gm->emissive_factor[1];
    out->emissive[2] = gm->emissive_factor[2];

    switch (gm->alpha_mode) {
    case cgltf_alpha_mode_mask:  out->alpha_mode = 1; break;
    case cgltf_alpha_mode_blend: out->alpha_mode = 2; break;
    default:                     out->alpha_mode = 0; break;
    }
    out->alpha_cutoff  = gm->alpha_cutoff;
    out->double_sided  = (bool)gm->double_sided;
}

/* ================================================================
   NODE GRAPH BUILDING
   ================================================================ */

/* Compute the flat primitive index for a given mesh + primitive. */
static uint32_t flat_prim_index(const cgltf_data *gd,
                                const cgltf_mesh *mesh,
                                cgltf_size prim)
{
    uint32_t idx = 0;
    for (cgltf_size m = 0; m < gd->meshes_count; m++) {
        if (&gd->meshes[m] == mesh) return idx + (uint32_t)prim;
        idx += (uint32_t)gd->meshes[m].primitives_count;
    }
    return UINT32_MAX;
}

/* Count total nodes needed (glTF nodes + extra nodes for multi-primitive meshes). */
static uint32_t count_import_nodes(const cgltf_data *gd)
{
    uint32_t count = (uint32_t)gd->nodes_count;
    for (cgltf_size n = 0; n < gd->nodes_count; n++) {
        const cgltf_node *node = &gd->nodes[n];
        if (node->mesh && node->mesh->primitives_count > 1)
            count += (uint32_t)(node->mesh->primitives_count - 1);
    }
    return count;
}

static void extract_local_trs(const cgltf_node *node,
                               float pos[3], float rot[4], float scl[3])
{
    if (node->has_translation) {
        pos[0] = node->translation[0];
        pos[1] = node->translation[1];
        pos[2] = node->translation[2];
    } else {
        pos[0] = pos[1] = pos[2] = 0.0f;
    }

    if (node->has_rotation) {
        rot[0] = node->rotation[0];
        rot[1] = node->rotation[1];
        rot[2] = node->rotation[2];
        rot[3] = node->rotation[3];
    } else {
        rot[0] = rot[1] = rot[2] = 0.0f;
        rot[3] = 1.0f;
    }

    if (node->has_scale) {
        scl[0] = node->scale[0];
        scl[1] = node->scale[1];
        scl[2] = node->scale[2];
    } else {
        scl[0] = scl[1] = scl[2] = 1.0f;
    }

    /* If the node has a matrix instead of TRS, decompose it. */
    if (node->has_matrix &&
        !node->has_translation && !node->has_rotation && !node->has_scale)
    {
        const float *m = node->matrix;

        /* Translation from column 3 */
        pos[0] = m[12]; pos[1] = m[13]; pos[2] = m[14];

        /* Scale from column lengths */
        float sx = sqrtf(m[0]*m[0] + m[1]*m[1] + m[2]*m[2]);
        float sy = sqrtf(m[4]*m[4] + m[5]*m[5] + m[6]*m[6]);
        float sz = sqrtf(m[8]*m[8] + m[9]*m[9] + m[10]*m[10]);
        scl[0] = sx; scl[1] = sy; scl[2] = sz;

        /* Rotation quaternion from normalised rotation matrix */
        if (sx > 1e-7f && sy > 1e-7f && sz > 1e-7f) {
            float r00 = m[0]/sx, r10 = m[1]/sx, r20 = m[2]/sx;
            float r01 = m[4]/sy, r11 = m[5]/sy, r21 = m[6]/sy;
            float r02 = m[8]/sz, r12 = m[9]/sz, r22 = m[10]/sz;
            float trace = r00 + r11 + r22;
            if (trace > 0.0f) {
                float s = 0.5f / sqrtf(trace + 1.0f);
                rot[3] = 0.25f / s;
                rot[0] = (r21 - r12) * s;
                rot[1] = (r02 - r20) * s;
                rot[2] = (r10 - r01) * s;
            } else if (r00 > r11 && r00 > r22) {
                float s = 2.0f * sqrtf(1.0f + r00 - r11 - r22);
                rot[3] = (r21 - r12) / s;
                rot[0] = 0.25f * s;
                rot[1] = (r01 + r10) / s;
                rot[2] = (r02 + r20) / s;
            } else if (r11 > r22) {
                float s = 2.0f * sqrtf(1.0f + r11 - r00 - r22);
                rot[3] = (r02 - r20) / s;
                rot[0] = (r01 + r10) / s;
                rot[1] = 0.25f * s;
                rot[2] = (r12 + r21) / s;
            } else {
                float s = 2.0f * sqrtf(1.0f + r22 - r00 - r11);
                rot[3] = (r10 - r01) / s;
                rot[0] = (r02 + r20) / s;
                rot[1] = (r12 + r21) / s;
                rot[2] = 0.25f * s;
            }
        }
    }
}

/* ================================================================
   IMPORT ENTRY POINT
   ================================================================ */

static bool gltf_import(void *data_ptr, const char *path,
                         Qs_ImportResult *out)
{
    (void)data_ptr;
    if (!path || !out) return false;

    cgltf_options opts;
    memset(&opts, 0, sizeof(opts));

    cgltf_data *gd = NULL;
    if (cgltf_parse_file(&opts, path, &gd) != cgltf_result_success)
        return false;
    if (cgltf_load_buffers(&opts, gd, path) != cgltf_result_success) {
        cgltf_free(gd);
        return false;
    }
    cgltf_validate(gd);

    char dir_path[1024];
    path_dir(path, dir_path, sizeof(dir_path));

    /* ------------------------------------------------------------------
       1. Textures — one slot per image (decoded lazily by materials)
    ------------------------------------------------------------------ */
    uint32_t image_count = (uint32_t)gd->images_count;
    Qs_ImportTexture *textures = NULL;
    bool *decoded = NULL;
    if (image_count > 0) {
        textures = (Qs_ImportTexture *)calloc(image_count, sizeof(Qs_ImportTexture));
        decoded  = (bool *)calloc(image_count, sizeof(bool));
    }

    /* ------------------------------------------------------------------
       2. Materials
    ------------------------------------------------------------------ */
    uint32_t mat_count = (uint32_t)gd->materials_count;
    Qs_ImportMaterial *materials = NULL;
    if (mat_count > 0) {
        materials = (Qs_ImportMaterial *)calloc(mat_count, sizeof(Qs_ImportMaterial));
        for (uint32_t i = 0; i < mat_count; i++) {
            fill_import_material(gd, &gd->materials[i], dir_path,
                                 textures, decoded, image_count,
                                 &materials[i]);
        }
    }

    /* ------------------------------------------------------------------
       3. Meshes — one ImportMesh per primitive
    ------------------------------------------------------------------ */
    uint32_t total_prims = 0;
    for (cgltf_size m = 0; m < gd->meshes_count; m++)
        total_prims += (uint32_t)gd->meshes[m].primitives_count;

    Qs_ImportMesh *meshes = NULL;
    if (total_prims > 0) {
        meshes = (Qs_ImportMesh *)calloc(total_prims, sizeof(Qs_ImportMesh));
        uint32_t prim_idx = 0;
        for (cgltf_size mi = 0; mi < gd->meshes_count; mi++) {
            const cgltf_mesh *gm = &gd->meshes[mi];
            for (cgltf_size pi = 0; pi < gm->primitives_count; pi++, prim_idx++) {
                const cgltf_primitive *prim = &gm->primitives[pi];
                if (prim->type != cgltf_primitive_type_triangles) continue;

                Qs_ImportMesh *im = &meshes[prim_idx];
                make_mesh_name(gm, pi, im->name, sizeof(im->name));

                im->vertices     = build_vertices(prim, &im->vertex_count);
                im->indices      = build_indices(prim, &im->index_count);
                im->material_index = prim->material
                    ? (uint32_t)cgltf_material_index(gd, prim->material)
                    : UINT32_MAX;
            }
        }
    }

    /* ------------------------------------------------------------------
       4. Nodes — preserve scene graph with local transforms.
          Multi-primitive meshes expand into extra sibling nodes.
    ------------------------------------------------------------------ */
    uint32_t total_nodes = count_import_nodes(gd);
    Qs_ImportNode *nodes = NULL;

    if (total_nodes > 0) {
        nodes = (Qs_ImportNode *)calloc(total_nodes, sizeof(Qs_ImportNode));

        /* Map glTF node index → ImportNode index (first node for that glTF node). */
        int32_t *node_map = (int32_t *)calloc(gd->nodes_count, sizeof(int32_t));

        uint32_t ni = 0; /* next free ImportNode slot */

        /* First pass: one ImportNode per glTF node */
        for (cgltf_size n = 0; n < gd->nodes_count; n++) {
            const cgltf_node *gn = &gd->nodes[n];
            node_map[n] = (int32_t)ni;

            Qs_ImportNode *in = &nodes[ni];
            if (gn->name && gn->name[0])
                snprintf(in->name, sizeof(in->name), "%s", gn->name);
            else
                snprintf(in->name, sizeof(in->name), "node_%zu", n);

            extract_local_trs(gn, in->position, in->rotation, in->scale);

            /* parent */
            if (gn->parent) {
                cgltf_size parent_idx = cgltf_node_index(gd, gn->parent);
                in->parent_index = node_map[parent_idx];
            } else {
                in->parent_index = -1;
            }

            /* mesh: single-prim → assign directly; multi-prim → no mesh (children will have it) */
            if (gn->mesh) {
                if (gn->mesh->primitives_count == 1) {
                    in->mesh_index = flat_prim_index(gd, gn->mesh, 0);
                } else {
                    in->mesh_index = UINT32_MAX;
                }
            } else {
                in->mesh_index = UINT32_MAX;
            }

            ni++;
        }

        /* Second pass: extra nodes for multi-primitive meshes */
        for (cgltf_size n = 0; n < gd->nodes_count; n++) {
            const cgltf_node *gn = &gd->nodes[n];
            if (!gn->mesh || gn->mesh->primitives_count <= 1) continue;

            int32_t parent_import = node_map[n];
            for (cgltf_size p = 0; p < gn->mesh->primitives_count; p++) {
                Qs_ImportNode *in = &nodes[ni];
                char suffix[16];
                snprintf(suffix, sizeof(suffix), ".prim%zu", p);

                if (gn->name && gn->name[0])
                    snprintf(in->name, sizeof(in->name), "%s%s", gn->name, suffix);
                else
                    snprintf(in->name, sizeof(in->name), "node_%zu%s", n, suffix);

                /* Identity local transform (transform lives on parent) */
                in->position[0] = in->position[1] = in->position[2] = 0.0f;
                in->rotation[0] = in->rotation[1] = in->rotation[2] = 0.0f;
                in->rotation[3] = 1.0f;
                in->scale[0] = in->scale[1] = in->scale[2] = 1.0f;
                in->parent_index = parent_import;
                in->mesh_index = flat_prim_index(gd, gn->mesh, p);

                ni++;
            }
        }

        free(node_map);
        total_nodes = ni;
    }

    /* ------------------------------------------------------------------
       5. Assemble result
    ------------------------------------------------------------------ */
    out->textures       = textures;
    out->texture_count  = image_count;
    out->materials      = materials;
    out->material_count = mat_count;
    out->meshes         = meshes;
    out->mesh_count     = total_prims;
    out->nodes          = nodes;
    out->node_count     = total_nodes;

    free(decoded);
    cgltf_free(gd);
    return true;
}

/* ================================================================
   EXTENSION VTABLE
   ================================================================ */

static const Qs_AssetImporterExt s_gltf_importer_ext = {
    .extensions = ".gltf,.glb",
    .import     = gltf_import,
};

const Qs_AssetImporterExt *gltf_importer_ext(void)
{
    return &s_gltf_importer_ext;
}
