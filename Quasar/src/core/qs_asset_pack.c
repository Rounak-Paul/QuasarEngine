/*
 * qs_asset_pack.c — Packed asset format I/O, runtime cache, cook step.
 *
 * Format details: see qs_asset_pack.h.
 *
 * Cook step is synchronous (callable from a worker thread).  It:
 *   1. Writes one .qstex per included texture.
 *   2. Writes one .qsmat per included material (JSON).
 *   3. Writes one .qsmesh per included mesh (binary).
 *   4. Builds an entity tree mirroring the import scene graph
 *      and serializes it to a .qproto file via qs_scene_to_json.
 *
 * Cache step is synchronous and main-thread-only (calls into GPU APIs).
 */

#include "qs_asset_pack.h"

#include "qs_asset.h"
#include "qs_mesh.h"
#include "qs_material.h"
#include "qs_texture.h"
#include "qs_project.h"
#include "qs_log.h"
#include "qs_system.h"
#include "quasar.h"
#include "cJSON.h"

#include <meshoptimizer.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <math.h>

#ifdef _WIN32
  #include <direct.h>
  #define qs_pack_mkdir(p) _mkdir(p)
  #ifndef S_ISDIR
    #define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
  #endif
#else
  #define qs_pack_mkdir(p) mkdir((p), 0755)
#endif

/* ================================================================
   PATH HELPERS
   ================================================================ */

#define QS_PACK_MAX_PATH 1024

static bool dir_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool ensure_dir(const char *p)
{
    if (!*p) return true;
    if (dir_exists(p)) return true;
    return qs_pack_mkdir(p) == 0 || dir_exists(p);
}

/// Recursively create every directory in `path` (treats trailing component as dir).
static bool ensure_dirs(const char *path)
{
    char buf[QS_PACK_MAX_PATH];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = '\0';
            ensure_dir(buf);
            *p = saved;
        }
    }
    return ensure_dir(buf);
}

static void path_dirname(const char *path, char *out, size_t out_size)
{
    const char *last = NULL;
    for (const char *p = path; *p; p++)
        if (*p == '/' || *p == '\\') last = p;
    if (!last) {
        snprintf(out, out_size, ".");
    } else {
        size_t len = (size_t)(last - path);
        if (len >= out_size) len = out_size - 1;
        memcpy(out, path, len);
        out[len] = '\0';
    }
}

static void sanitize_filename(const char *in, char *out, size_t out_size)
{
    size_t j = 0;
    if (out_size == 0) return;
    if (!in || !*in) {
        snprintf(out, out_size, "unnamed");
        return;
    }
    for (size_t i = 0; in[i] && j + 1 < out_size; i++) {
        char c = in[i];
        bool ok = (c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') ||
                  c == '_' || c == '-' || c == '.';
        out[j++] = ok ? c : '_';
    }
    if (j == 0) out[j++] = '_';
    out[j] = '\0';
}

static bool path_is_absolute(const char *p)
{
    if (!p || !*p) return false;
    if (p[0] == '/' || p[0] == '\\') return true;
#ifdef _WIN32
    if (p[1] == ':') return true;
#endif
    return false;
}

/// Compute path of `target` relative to the directory of `from_file`.
/// Both paths must be absolute; output uses '/' separators.
static void path_relative_to(const char *from_file, const char *target,
                             char *out, size_t out_size)
{
    char from_dir[QS_PACK_MAX_PATH];
    path_dirname(from_file, from_dir, sizeof(from_dir));

    /* Find common prefix at directory boundary */
    size_t i = 0, last_slash = 0;
    while (from_dir[i] && target[i] && from_dir[i] == target[i]) {
        if (from_dir[i] == '/' || from_dir[i] == '\\') last_slash = i;
        i++;
    }
    /* Match must end at directory boundary */
    if (from_dir[i] == '\0' && (target[i] == '/' || target[i] == '\\'))
        last_slash = i;
    else if (target[i] == '\0' && (from_dir[i] == '/' || from_dir[i] == '\\'))
        last_slash = i;

    /* Count "../" segments from from_dir down to common ancestor */
    int up = 0;
    for (size_t k = last_slash; from_dir[k]; k++)
        if (from_dir[k] == '/' || from_dir[k] == '\\') up++;
    if (from_dir[last_slash] != '\0' &&
        from_dir[last_slash] != '/' && from_dir[last_slash] != '\\')
        up++;

    /* Build the result */
    size_t pos = 0;
    for (int k = 0; k < up && pos + 3 < out_size; k++) {
        out[pos++] = '.'; out[pos++] = '.'; out[pos++] = '/';
    }
    const char *tail = target + last_slash;
    while (*tail == '/' || *tail == '\\') tail++;
    snprintf(out + pos, out_size - pos, "%s", tail);
    /* Normalize backslashes */
    for (char *q = out; *q; q++) if (*q == '\\') *q = '/';
}

/* ================================================================
   QSTEX  — texture writer / reader
   ================================================================ */

static uint32_t bytes_per_pixel(uint32_t format)
{
    switch ((Qs_TextureFormat)format) {
    case QS_TEXTURE_FORMAT_RG8_UNORM:     return 2;
    case QS_TEXTURE_FORMAT_R8_UNORM:      return 1;
    case QS_TEXTURE_FORMAT_RGBA16_SFLOAT: return 8;
    default:                              return 4;
    }
}

static bool write_qstex(const char *path, const Qs_ImportTexture *tex,
                        bool srgb_override, bool gen_mips)
{
    if (!tex || !tex->pixels) return false;

    Qs_TexFileHeader h = {
        .magic       = QS_TEX_MAGIC,
        .version     = QS_TEX_VERSION,
        .width       = tex->width,
        .height      = tex->height,
        .format      = srgb_override ? QS_TEXTURE_FORMAT_RGBA8_SRGB
                                     : (uint32_t)tex->format,
        .mip_count   = 1,
        .flags       = (srgb_override ? QS_TEX_FLAG_SRGB : 0u),
        .min_filter  = (uint8_t)tex->min_filter,
        .mag_filter  = (uint8_t)tex->mag_filter,
        .wrap_u      = (uint8_t)tex->wrap_u,
        .wrap_v      = (uint8_t)tex->wrap_v,
    };
    /* mip-0 only for v1; gen_mips is recorded as a flag for runtime hint */
    if (gen_mips) h.flags |= (1u << 1);

    uint32_t bpp  = bytes_per_pixel(h.format);
    uint32_t size = h.width * h.height * bpp;

    FILE *f = fopen(path, "wb");
    if (!f) {
        QS_LOG_ERROR("Failed to open .qstex for write: %s", path);
        return false;
    }
    fwrite(&h, sizeof(h), 1, f);
    fwrite(&size, sizeof(size), 1, f);
    fwrite(tex->pixels, 1, size, f);
    fclose(f);
    return true;
}

static bool read_qstex(const char *path,
                       Qs_TexFileHeader *out_header,
                       void **out_pixels,
                       uint32_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    Qs_TexFileHeader h;
    if (fread(&h, sizeof(h), 1, f) != 1) { fclose(f); return false; }
    if (h.magic != QS_TEX_MAGIC) {
        QS_LOG_ERROR("Not a .qstex file: %s", path);
        fclose(f); return false;
    }
    uint32_t size = 0;
    if (fread(&size, sizeof(size), 1, f) != 1 || size == 0) {
        fclose(f); return false;
    }
    void *pixels = malloc(size);
    if (!pixels) { fclose(f); return false; }
    if (fread(pixels, 1, size, f) != size) {
        free(pixels); fclose(f); return false;
    }
    fclose(f);
    *out_header = h;
    *out_pixels = pixels;
    *out_size   = size;
    return true;
}

/* ================================================================
   QSMESH — mesh writer / reader
   ================================================================ */

static void compute_aabb(const Qs_Vertex *v, uint32_t n,
                         float min[3], float max[3])
{
    if (n == 0) {
        for (int i = 0; i < 3; i++) { min[i] = 0; max[i] = 0; }
        return;
    }
    for (int i = 0; i < 3; i++) { min[i] = v[0].position[i]; max[i] = v[0].position[i]; }
    for (uint32_t i = 1; i < n; i++) {
        for (int k = 0; k < 3; k++) {
            float p = v[i].position[k];
            if (p < min[k]) min[k] = p;
            if (p > max[k]) max[k] = p;
        }
    }
}

/// Run meshoptimizer vertex remap + cache + overdraw + fetch passes in place.
/// Replaces vertex/index arrays with new mallocs (caller must free).
static void optimize_mesh(Qs_Vertex **verts_io, uint32_t *vc_io,
                          uint32_t **idx_io, uint32_t ic)
{
    if (!verts_io || !idx_io || !*verts_io || !*idx_io || ic == 0) return;

    Qs_Vertex *verts = *verts_io;
    uint32_t   vc    = *vc_io;
    uint32_t  *idx   = *idx_io;

    /* 1) Vertex remap (de-dup) */
    unsigned int *remap = malloc(sizeof(unsigned int) * vc);
    if (!remap) return;
    size_t new_vc = meshopt_generateVertexRemap(remap,
                        idx, ic, verts, vc, sizeof(Qs_Vertex));

    Qs_Vertex *new_verts = malloc(sizeof(Qs_Vertex) * new_vc);
    uint32_t  *new_idx   = malloc(sizeof(uint32_t)  * ic);
    if (!new_verts || !new_idx) {
        free(remap); free(new_verts); free(new_idx);
        return;
    }
    meshopt_remapVertexBuffer(new_verts, verts, vc, sizeof(Qs_Vertex), remap);
    meshopt_remapIndexBuffer(new_idx, idx, ic, remap);
    free(remap);

    /* 2) Vertex cache optimisation */
    meshopt_optimizeVertexCache(new_idx, new_idx, ic, new_vc);

    /* 3) Overdraw — needs vertex positions */
    meshopt_optimizeOverdraw(new_idx, new_idx, ic,
                             (const float *)new_verts,
                             new_vc, sizeof(Qs_Vertex), 1.05f);

    /* 4) Vertex fetch */
    meshopt_optimizeVertexFetch(new_verts, new_idx, ic,
                                new_verts, new_vc, sizeof(Qs_Vertex));

    free(verts);
    free(idx);
    *verts_io = new_verts;
    *vc_io    = (uint32_t)new_vc;
    *idx_io   = new_idx;
}

static bool write_qsmesh(const char *path, const Qs_ImportMesh *m, bool optimize)
{
    if (!m || !m->vertices || !m->indices) return false;

    Qs_Vertex *verts = malloc(sizeof(Qs_Vertex) * m->vertex_count);
    uint32_t  *idx   = malloc(sizeof(uint32_t)  * m->index_count);
    if (!verts || !idx) { free(verts); free(idx); return false; }
    memcpy(verts, m->vertices, sizeof(Qs_Vertex) * m->vertex_count);
    memcpy(idx,   m->indices,  sizeof(uint32_t)  * m->index_count);

    uint32_t vc = m->vertex_count;
    uint32_t ic = m->index_count;

    if (optimize)
        optimize_mesh(&verts, &vc, &idx, ic);

    Qs_MeshFileHeader h = {
        .magic        = QS_MESH_MAGIC,
        .version      = QS_MESH_VERSION,
        .vertex_count = vc,
        .index_count  = ic,
    };
    snprintf(h.surface_name, sizeof(h.surface_name), "%s", m->name);
    compute_aabb(verts, vc, h.aabb_min, h.aabb_max);

    FILE *f = fopen(path, "wb");
    if (!f) {
        free(verts); free(idx);
        QS_LOG_ERROR("Failed to open .qsmesh for write: %s", path);
        return false;
    }
    fwrite(&h, sizeof(h), 1, f);
    fwrite(verts, sizeof(Qs_Vertex), vc, f);
    fwrite(idx,   sizeof(uint32_t),  ic, f);
    fclose(f);

    free(verts); free(idx);
    return true;
}

static bool read_qsmesh(const char *path,
                        Qs_MeshFileHeader *out_header,
                        Qs_Vertex **out_verts,
                        uint32_t  **out_idx)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    Qs_MeshFileHeader h;
    if (fread(&h, sizeof(h), 1, f) != 1) { fclose(f); return false; }
    if (h.magic != QS_MESH_MAGIC) {
        QS_LOG_ERROR("Not a .qsmesh file: %s", path);
        fclose(f); return false;
    }
    Qs_Vertex *v = malloc(sizeof(Qs_Vertex) * h.vertex_count);
    uint32_t  *i = malloc(sizeof(uint32_t)  * h.index_count);
    if (!v || !i) { free(v); free(i); fclose(f); return false; }
    if (fread(v, sizeof(Qs_Vertex), h.vertex_count, f) != h.vertex_count ||
        fread(i, sizeof(uint32_t),  h.index_count,  f) != h.index_count)
    {
        free(v); free(i); fclose(f); return false;
    }
    fclose(f);
    *out_header = h;
    *out_verts  = v;
    *out_idx    = i;
    return true;
}

/* ================================================================
   QSMAT — JSON writer / reader
   ================================================================ */

/// Path field is OPTIONAL — empty string means "no texture for this slot".
typedef struct QsMatRefs {
    char base_color_tex      [QS_PACK_MAX_PATH];
    char metallic_roughness_tex[QS_PACK_MAX_PATH];
    char normal_tex          [QS_PACK_MAX_PATH];
    char occlusion_tex       [QS_PACK_MAX_PATH];
    char emissive_tex        [QS_PACK_MAX_PATH];
} QsMatRefs;

static bool write_qsmat(const char *path,
                        const Qs_ImportMaterial *im,
                        const QsMatRefs *refs)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", im->name);

    cJSON *bc = cJSON_CreateArray();
    for (int i = 0; i < 4; i++) cJSON_AddItemToArray(bc, cJSON_CreateNumber(im->base_color[i]));
    cJSON_AddItemToObject(root, "base_color_factor", bc);

    cJSON_AddNumberToObject(root, "metallic_factor",  im->metallic);
    cJSON_AddNumberToObject(root, "roughness_factor", im->roughness);
    cJSON_AddNumberToObject(root, "normal_scale",     im->normal_scale);
    cJSON_AddNumberToObject(root, "occlusion_strength", im->occlusion_strength);

    cJSON *em = cJSON_CreateArray();
    for (int i = 0; i < 3; i++) cJSON_AddItemToArray(em, cJSON_CreateNumber(im->emissive[i]));
    cJSON_AddItemToObject(root, "emissive_factor", em);

    cJSON_AddNumberToObject(root, "alpha_mode",   (double)im->alpha_mode);
    cJSON_AddNumberToObject(root, "alpha_cutoff", im->alpha_cutoff);
    cJSON_AddBoolToObject  (root, "double_sided", im->double_sided);

    cJSON *texs = cJSON_CreateObject();
    if (refs->base_color_tex      [0]) cJSON_AddStringToObject(texs, "base_color",         refs->base_color_tex);
    if (refs->metallic_roughness_tex[0]) cJSON_AddStringToObject(texs, "metallic_roughness", refs->metallic_roughness_tex);
    if (refs->normal_tex          [0]) cJSON_AddStringToObject(texs, "normal",             refs->normal_tex);
    if (refs->occlusion_tex       [0]) cJSON_AddStringToObject(texs, "occlusion",          refs->occlusion_tex);
    if (refs->emissive_tex        [0]) cJSON_AddStringToObject(texs, "emissive",           refs->emissive_tex);
    cJSON_AddItemToObject(root, "textures", texs);

    char *str = cJSON_Print(root);
    cJSON_Delete(root);
    if (!str) return false;

    FILE *f = fopen(path, "wb");
    if (!f) { free(str); return false; }
    fputs(str, f);
    fclose(f);
    free(str);
    return true;
}

/* ================================================================
   COOK STEP
   ================================================================ */

#define DEFAULT_TEX_OPTS  (Qs_TexImportOpts){ .include = true, .srgb = false, .generate_mips = true }
#define DEFAULT_MAT_OPTS  (Qs_MatImportOpts){ .include = true }
#define DEFAULT_MESH_OPTS (Qs_MeshImportOpts){ .include = true, .optimize = true }

static void progress_emit(const Qs_CookOptions *opts,
                          const char *stage,
                          uint32_t done, uint32_t total)
{
    if (opts && opts->progress)
        opts->progress(opts->progress_user, stage, done, total);
}

bool qs_asset_cook(const Qs_ImportResult *result,
                   const Qs_CookOptions  *options,
                   char                  *out_qproto_abs,
                   size_t                 out_qproto_size)
{
    if (!result || !options || !options->out_dir || !options->asset_name) {
        QS_LOG_ERROR("qs_asset_cook: invalid arguments");
        return false;
    }

    /* Output directory layout */
    char  base[QS_PACK_MAX_PATH];
    char  tex_dir[QS_PACK_MAX_PATH];
    char  mat_dir[QS_PACK_MAX_PATH];
    char  mesh_dir[QS_PACK_MAX_PATH];
    snprintf(base,     sizeof(base),     "%s",          options->out_dir);
    snprintf(tex_dir,  sizeof(tex_dir),  "%s/textures", options->out_dir);
    snprintf(mat_dir,  sizeof(mat_dir),  "%s/materials",options->out_dir);
    snprintf(mesh_dir, sizeof(mesh_dir), "%s/meshes",   options->out_dir);

    if (!ensure_dirs(base) ||
        !ensure_dir(tex_dir) || !ensure_dir(mat_dir) || !ensure_dir(mesh_dir))
    {
        QS_LOG_ERROR("Cook: could not create output directories under %s", base);
        return false;
    }

    /* ---------- TEXTURES ---------- */
    char (*tex_paths)[QS_PACK_MAX_PATH] = NULL;
    if (result->texture_count > 0) {
        tex_paths = calloc(result->texture_count, sizeof(*tex_paths));
        if (!tex_paths) return false;
    }
    for (uint32_t i = 0; i < result->texture_count; i++) {
        Qs_TexImportOpts opt = options->tex_opts ? options->tex_opts[i] : DEFAULT_TEX_OPTS;
        progress_emit(options, "Packing textures", i, result->texture_count);
        if (!opt.include) continue;
        const Qs_ImportTexture *t = &result->textures[i];
        if (!t->pixels) continue;

        char fname[160], safe[160];
        sanitize_filename(t->name[0] ? t->name : "tex", safe, sizeof(safe));
        snprintf(fname, sizeof(fname), "%s_%u", safe, i);

        char abs[QS_PACK_MAX_PATH];
        snprintf(abs, sizeof(abs), "%s/%s.qstex", tex_dir, fname);
        if (write_qstex(abs, t, opt.srgb, opt.generate_mips)) {
            snprintf(tex_paths[i], sizeof(tex_paths[i]), "%s", abs);
        }
    }

    /* ---------- MATERIALS ---------- */
    char (*mat_paths)[QS_PACK_MAX_PATH] = NULL;
    if (result->material_count > 0) {
        mat_paths = calloc(result->material_count, sizeof(*mat_paths));
        if (!mat_paths) { free(tex_paths); return false; }
    }
    for (uint32_t i = 0; i < result->material_count; i++) {
        Qs_MatImportOpts opt = options->mat_opts ? options->mat_opts[i] : DEFAULT_MAT_OPTS;
        progress_emit(options, "Packing materials", i, result->material_count);
        if (!opt.include) continue;
        const Qs_ImportMaterial *im = &result->materials[i];

        char safe[160], fname[200];
        sanitize_filename(im->name[0] ? im->name : "material", safe, sizeof(safe));
        snprintf(fname, sizeof(fname), "%s_%u", safe, i);
        char abs[QS_PACK_MAX_PATH];
        snprintf(abs, sizeof(abs), "%s/%s.qsmat", mat_dir, fname);

        /* Build texture refs (paths relative to the .qsmat file) */
        QsMatRefs refs;
        memset(&refs, 0, sizeof(refs));
        #define LINK_TEX(field, idx)                                            \
            do { if ((idx) < result->texture_count && tex_paths &&              \
                    tex_paths[(idx)][0]) {                                      \
                path_relative_to(abs, tex_paths[(idx)],                         \
                                 refs.field, sizeof(refs.field));               \
            } } while (0)
        LINK_TEX(base_color_tex,         im->base_color_tex);
        LINK_TEX(metallic_roughness_tex, im->metallic_roughness_tex);
        LINK_TEX(normal_tex,             im->normal_tex);
        LINK_TEX(occlusion_tex,          im->occlusion_tex);
        LINK_TEX(emissive_tex,           im->emissive_tex);
        #undef LINK_TEX

        if (write_qsmat(abs, im, &refs)) {
            snprintf(mat_paths[i], sizeof(mat_paths[i]), "%s", abs);
        }
    }

    /* ---------- MESHES ---------- */
    char (*mesh_paths)[QS_PACK_MAX_PATH] = NULL;
    if (result->mesh_count > 0) {
        mesh_paths = calloc(result->mesh_count, sizeof(*mesh_paths));
        if (!mesh_paths) { free(tex_paths); free(mat_paths); return false; }
    }
    for (uint32_t i = 0; i < result->mesh_count; i++) {
        Qs_MeshImportOpts opt = options->mesh_opts
            ? options->mesh_opts[i] : DEFAULT_MESH_OPTS;
        progress_emit(options, "Packing meshes", i, result->mesh_count);
        if (!opt.include) continue;
        const Qs_ImportMesh *im = &result->meshes[i];

        char safe[160], fname[200];
        sanitize_filename(im->name[0] ? im->name : "mesh", safe, sizeof(safe));
        snprintf(fname, sizeof(fname), "%s_%u", safe, i);
        char abs[QS_PACK_MAX_PATH];
        snprintf(abs, sizeof(abs), "%s/%s.qsmesh", mesh_dir, fname);

        if (write_qsmesh(abs, im, opt.optimize))
            snprintf(mesh_paths[i], sizeof(mesh_paths[i]), "%s", abs);
    }

    /* ---------- .QPROTO (build cJSON tree directly) ---------- */
    progress_emit(options, "Building prototype", 0, 1);

    char proto_path[QS_PACK_MAX_PATH];
    snprintf(proto_path, sizeof(proto_path), "%s/%s.qproto",
             base, options->asset_name);

    cJSON *root_json = cJSON_CreateObject();
    cJSON_AddStringToObject(root_json, "name", options->asset_name);
    cJSON_AddNumberToObject(root_json, "next_entity_id",
                            (double)(result->node_count + 2));

    cJSON *ents = cJSON_CreateArray();
    cJSON_AddItemToObject(root_json, "entities", ents);

    /* Helper: add a Transform component (TRS) to a components object */
    #define ADD_TRANSFORM(comps, px, py, pz, qx, qy, qz, qw, sx, sy, sz)       \
    do {                                                                       \
        cJSON *_t = cJSON_CreateObject();                                      \
        cJSON *_p = cJSON_CreateArray();                                       \
        cJSON_AddItemToArray(_p, cJSON_CreateNumber(px));                      \
        cJSON_AddItemToArray(_p, cJSON_CreateNumber(py));                      \
        cJSON_AddItemToArray(_p, cJSON_CreateNumber(pz));                      \
        cJSON_AddItemToObject(_t, "position", _p);                             \
        cJSON *_r = cJSON_CreateArray();                                       \
        cJSON_AddItemToArray(_r, cJSON_CreateNumber(qx));                      \
        cJSON_AddItemToArray(_r, cJSON_CreateNumber(qy));                      \
        cJSON_AddItemToArray(_r, cJSON_CreateNumber(qz));                      \
        cJSON_AddItemToArray(_r, cJSON_CreateNumber(qw));                      \
        cJSON_AddItemToObject(_t, "rotation", _r);                             \
        cJSON *_s = cJSON_CreateArray();                                       \
        cJSON_AddItemToArray(_s, cJSON_CreateNumber(sx));                      \
        cJSON_AddItemToArray(_s, cJSON_CreateNumber(sy));                      \
        cJSON_AddItemToArray(_s, cJSON_CreateNumber(sz));                      \
        cJSON_AddItemToObject(_t, "scale", _s);                                \
        cJSON_AddItemToObject(comps, "Transform", _t);                         \
    } while (0)

    /* Entity 0: root with identity transform */
    {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "name", options->asset_name);
        cJSON_AddBoolToObject(e, "enabled", 1);
        cJSON_AddNumberToObject(e, "parent", -1);
        cJSON *comps = cJSON_CreateObject();
        ADD_TRANSFORM(comps, 0,0,0,  0,0,0,1,  1,1,1);
        cJSON_AddItemToObject(e, "components", comps);
        cJSON_AddItemToArray(ents, e);
    }

    /* One entity per import node. Array index in serialized JSON:
       node `i` is at array index (i + 1).  Parent = root (0) for nodes
       with `parent_index == -1`, else (parent_index + 1). */
    for (uint32_t i = 0; i < result->node_count; i++) {
        const Qs_ImportNode *n = &result->nodes[i];
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "name", n->name[0] ? n->name : "node");
        cJSON_AddBoolToObject(e, "enabled", 1);
        int parent_idx = (n->parent_index >= 0 &&
                          (uint32_t)n->parent_index < result->node_count)
            ? (n->parent_index + 1) : 0;
        cJSON_AddNumberToObject(e, "parent", parent_idx);

        cJSON *comps = cJSON_CreateObject();
        ADD_TRANSFORM(comps,
                      n->position[0], n->position[1], n->position[2],
                      n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3],
                      n->scale[0],    n->scale[1],    n->scale[2]);

        if (n->mesh_index < result->mesh_count &&
            mesh_paths && mesh_paths[n->mesh_index][0])
        {
            cJSON *mc = cJSON_CreateObject();
            cJSON_AddBoolToObject(mc, "visible", 1);

            char rel[QS_PACK_MAX_PATH];
            path_relative_to(proto_path, mesh_paths[n->mesh_index],
                             rel, sizeof(rel));
            cJSON_AddStringToObject(mc, "mesh_path", rel);

            uint32_t mat_idx = result->meshes[n->mesh_index].material_index;
            if (mat_idx < result->material_count &&
                mat_paths && mat_paths[mat_idx][0])
            {
                path_relative_to(proto_path, mat_paths[mat_idx],
                                 rel, sizeof(rel));
                cJSON_AddStringToObject(mc, "material_path", rel);
            } else {
                cJSON_AddStringToObject(mc, "material_path", "");
            }

            cJSON_AddItemToObject(comps, "MeshComp", mc);
        }

        cJSON_AddItemToObject(e, "components", comps);
        cJSON_AddItemToArray(ents, e);
    }
    #undef ADD_TRANSFORM

    /* Write .qproto */
    bool ok = false;
    {
        char *str = cJSON_Print(root_json);
        if (str) {
            FILE *f = fopen(proto_path, "wb");
            if (f) {
                fputs(str, f);
                fclose(f);
                ok = true;
            }
            free(str);
        }
    }
    cJSON_Delete(root_json);

    free(tex_paths); free(mat_paths); free(mesh_paths);

    if (!ok) {
        QS_LOG_ERROR("Cook: failed to write .qproto: %s", proto_path);
        return false;
    }

    /* Register in project asset DB */
    if (options->project) {
        qs_project_register_prototype(options->project, proto_path);
        qs_project_save(options->project);
    }

    if (out_qproto_abs && out_qproto_size > 0)
        snprintf(out_qproto_abs, out_qproto_size, "%s", proto_path);

    progress_emit(options, "Done", 1, 1);
    QS_LOG_INFO("Cooked '%s' → %s", options->asset_name, proto_path);
    return true;
}

/* ================================================================
   ACTIVE ENGINE — set by the asset system at init time so the cook
   can reach engine-only APIs (scene creation) without changing every
   call site to thread `Qs_Engine *` through.
   ================================================================ */

static Qs_Engine *g_active_engine = NULL;

void qs_pack_set_active_engine(Qs_Engine *engine) { g_active_engine = engine; }
Qs_Engine *qs_pack_active_engine(void)            { return g_active_engine; }

/* ================================================================
   RUNTIME ASSET CACHE
   ================================================================ */

#define QS_PACK_CACHE_MAX 4096

typedef enum CacheKind {
    CACHE_MESH = 1,
    CACHE_MAT  = 2,
    CACHE_TEX  = 3,
} CacheKind;

typedef struct CacheEntry {
    char       path[QS_PACK_MAX_PATH];
    CacheKind  kind;
    uint32_t   ref_count;
    /* For CACHE_MAT: absolute paths of textures loaded via qs_asset_cache_texture
       at material creation time.  Each holds a ref; released when mat ref hits 0. */
    char       mat_tex_paths[5][QS_PACK_MAX_PATH];
    uint32_t   mat_tex_count;
    union {
        Qs_Mesh     *mesh;
        Qs_Material *material;
        Qs_Texture  *texture;
    } data;
} CacheEntry;

static CacheEntry *g_cache[QS_PACK_CACHE_MAX];
static uint32_t    g_cache_count;

static CacheEntry *cache_find(const char *path, CacheKind kind)
{
    for (uint32_t i = 0; i < g_cache_count; i++) {
        if (g_cache[i]->kind == kind &&
            strcmp(g_cache[i]->path, path) == 0)
            return g_cache[i];
    }
    return NULL;
}

static CacheEntry *cache_add(const char *path, CacheKind kind)
{
    if (g_cache_count >= QS_PACK_CACHE_MAX) {
        QS_LOG_ERROR("Asset cache full (%d)", QS_PACK_CACHE_MAX);
        return NULL;
    }
    CacheEntry *e = calloc(1, sizeof(CacheEntry));
    if (!e) return NULL;
    snprintf(e->path, sizeof(e->path), "%s", path);
    e->kind = kind;
    g_cache[g_cache_count++] = e;
    return e;
}

void qs_asset_cache_clear(void)
{
    for (uint32_t i = 0; i < g_cache_count; i++) {
        CacheEntry *e = g_cache[i];
        if (!e) continue;
        switch (e->kind) {
        case CACHE_MESH: if (e->data.mesh)     qs_mesh_destroy(e->data.mesh);         break;
        case CACHE_MAT:  if (e->data.material) qs_material_destroy(e->data.material); break;
        case CACHE_TEX:  if (e->data.texture)  qs_texture_destroy(e->data.texture);   break;
        }
        free(e);
    }
    g_cache_count = 0;
}

/* Internal: decrement ref and destroy + remove entry if it hits 0. */
static void cache_release(const char *abs_path, CacheKind kind)
{
    if (!abs_path || !*abs_path) return;
    for (uint32_t i = 0; i < g_cache_count; i++) {
        CacheEntry *e = g_cache[i];
        if (e->kind != kind || strcmp(e->path, abs_path) != 0) continue;
        if (e->ref_count > 0) e->ref_count--;
        if (e->ref_count == 0) {
            if (kind == CACHE_MAT) {
                /* Release owned texture refs before destroying material */
                for (uint32_t t = 0; t < e->mat_tex_count; t++)
                    cache_release(e->mat_tex_paths[t], CACHE_TEX);
            }
            switch (kind) {
            case CACHE_MESH: qs_mesh_destroy(e->data.mesh);         break;
            case CACHE_MAT:  qs_material_destroy(e->data.material); break;
            case CACHE_TEX:  qs_texture_destroy(e->data.texture);   break;
            }
            free(e);
            g_cache[i] = g_cache[--g_cache_count];
        }
        return;
    }
}

void qs_asset_cache_release_mesh(const char *abs_path)
{
    cache_release(abs_path, CACHE_MESH);
}

void qs_asset_cache_release_material(const char *abs_path)
{
    cache_release(abs_path, CACHE_MAT);
}

void qs_asset_cache_release_texture(const char *abs_path)
{
    cache_release(abs_path, CACHE_TEX);
}

Qs_Texture *qs_asset_cache_texture(Qs_Engine *engine, const char *abs_path)
{
    if (!engine || !abs_path || !*abs_path) return NULL;
    CacheEntry *hit = cache_find(abs_path, CACHE_TEX);
    if (hit) { hit->ref_count++; return hit->data.texture; }

    Qs_TexFileHeader h;
    void *pixels = NULL; uint32_t size = 0;
    if (!read_qstex(abs_path, &h, &pixels, &size)) return NULL;

    Qs_TextureDesc td = {
        .name          = abs_path,
        .width         = h.width,
        .height        = h.height,
        .format        = (Qs_TextureFormat)h.format,
        .pixels        = pixels,
        .generate_mips = (h.flags & (1u << 1)) != 0,
        .min_filter    = (Qs_TextureFilter)h.min_filter,
        .mag_filter    = (Qs_TextureFilter)h.mag_filter,
        .wrap_u        = (Qs_TextureWrap)h.wrap_u,
        .wrap_v        = (Qs_TextureWrap)h.wrap_v,
    };
    Qs_Texture *tex = qs_texture_create(engine, &td);
    free(pixels);
    if (!tex) return NULL;

    CacheEntry *e = cache_add(abs_path, CACHE_TEX);
    if (!e) { qs_texture_destroy(tex); return NULL; }
    e->ref_count = 1;
    e->data.texture = tex;
    return tex;
}

Qs_Mesh *qs_asset_cache_mesh(Qs_Engine *engine, const char *abs_path)
{
    if (!engine || !abs_path || !*abs_path) return NULL;
    CacheEntry *hit = cache_find(abs_path, CACHE_MESH);
    if (hit) { hit->ref_count++; return hit->data.mesh; }

    Qs_MeshFileHeader h;
    Qs_Vertex *v = NULL; uint32_t *idx = NULL;
    if (!read_qsmesh(abs_path, &h, &v, &idx)) return NULL;

    Qs_MeshDesc md = {
        .name         = h.surface_name[0] ? h.surface_name : abs_path,
        .vertices     = v,
        .vertex_count = h.vertex_count,
        .indices      = idx,
        .index_count  = h.index_count,
        .index_type   = QS_INDEX_TYPE_UINT32,
    };
    Qs_Mesh *mesh = qs_mesh_create(engine, &md);
    free(v); free(idx);
    if (!mesh) return NULL;

    CacheEntry *e = cache_add(abs_path, CACHE_MESH);
    if (!e) { qs_mesh_destroy(mesh); return NULL; }
    e->ref_count = 1;
    e->data.mesh = mesh;
    return mesh;
}

static double json_num(const cJSON *o, const char *k, double def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}
static bool json_bool(const cJSON *o, const char *k, bool def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsBool(v) ? cJSON_IsTrue(v) : def;
}
static void json_vecf(const cJSON *o, const char *k, float *out, int n, const float *def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (cJSON_IsArray(v) && cJSON_GetArraySize(v) >= n) {
        for (int i = 0; i < n; i++)
            out[i] = (float)cJSON_GetArrayItem(v, i)->valuedouble;
    } else if (def) {
        for (int i = 0; i < n; i++) out[i] = def[i];
    }
}

Qs_Material *qs_asset_cache_material(Qs_Engine *engine, const char *abs_path)
{
    if (!engine || !abs_path || !*abs_path) return NULL;
    CacheEntry *hit = cache_find(abs_path, CACHE_MAT);
    if (hit) { hit->ref_count++; return hit->data.material; }

    /* Read & parse JSON */
    FILE *f = fopen(abs_path, "rb");
    if (!f) { QS_LOG_ERROR("Failed to open .qsmat: %s", abs_path); return NULL; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, (size_t)len, f); buf[len] = '\0'; fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) { QS_LOG_ERROR("Failed to parse .qsmat: %s", abs_path); return NULL; }

    Qs_MaterialDesc md = qs_material_desc_defaults();
    const cJSON *name_val = cJSON_GetObjectItemCaseSensitive(root, "name");
    md.name = cJSON_IsString(name_val) ? name_val->valuestring : abs_path;

    static const float WHITE4[4] = {1,1,1,1};
    static const float BLACK3[3] = {0,0,0};
    json_vecf(root, "base_color_factor", md.base_color_factor, 4, WHITE4);
    md.metallic_factor    = (float)json_num(root, "metallic_factor", 1.0);
    md.roughness_factor   = (float)json_num(root, "roughness_factor", 1.0);
    md.normal_scale       = (float)json_num(root, "normal_scale", 1.0);
    md.occlusion_strength = (float)json_num(root, "occlusion_strength", 1.0);
    json_vecf(root, "emissive_factor", md.emissive_factor, 3, BLACK3);
    md.alpha_mode    = (Qs_AlphaMode)json_num(root, "alpha_mode", 0);
    md.alpha_cutoff  = (float)json_num(root, "alpha_cutoff", 0.5);
    md.double_sided  = json_bool(root, "double_sided", false);

    /* Resolve texture refs — collect abs paths so we can release them later */
    char mat_dir[QS_PACK_MAX_PATH];
    path_dirname(abs_path, mat_dir, sizeof(mat_dir));
    const cJSON *texs = cJSON_GetObjectItemCaseSensitive(root, "textures");

    char collected_tex_paths[5][QS_PACK_MAX_PATH];
    uint32_t collected_tex_count = 0;

    #define RESOLVE_TEX(field, json_key)                                        \
    do {                                                                        \
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(texs, json_key);      \
        if (cJSON_IsString(v) && v->valuestring[0]) {                           \
            char texabs[QS_PACK_MAX_PATH];                                      \
            if (path_is_absolute(v->valuestring))                               \
                snprintf(texabs, sizeof(texabs), "%s", v->valuestring);         \
            else                                                                \
                snprintf(texabs, sizeof(texabs), "%s/%s", mat_dir, v->valuestring); \
            md.field = qs_asset_cache_texture(engine, texabs);                  \
            if (md.field && collected_tex_count < 5)                            \
                snprintf(collected_tex_paths[collected_tex_count++],            \
                         QS_PACK_MAX_PATH, "%s", texabs);                       \
        }                                                                       \
    } while (0)

    if (cJSON_IsObject(texs)) {
        RESOLVE_TEX(base_color_texture,         "base_color");
        RESOLVE_TEX(metallic_roughness_texture, "metallic_roughness");
        RESOLVE_TEX(normal_texture,             "normal");
        RESOLVE_TEX(occlusion_texture,          "occlusion");
        RESOLVE_TEX(emissive_texture,           "emissive");
    }
    #undef RESOLVE_TEX

    Qs_Material *mat = qs_material_create(engine, &md);
    cJSON_Delete(root);
    if (!mat) {
        /* Roll back acquired texture refs */
        for (uint32_t t = 0; t < collected_tex_count; t++)
            cache_release(collected_tex_paths[t], CACHE_TEX);
        return NULL;
    }

    CacheEntry *e = cache_add(abs_path, CACHE_MAT);
    if (!e) {
        for (uint32_t t = 0; t < collected_tex_count; t++)
            cache_release(collected_tex_paths[t], CACHE_TEX);
        qs_material_destroy(mat);
        return NULL;
    }
    e->ref_count = 1;
    e->mat_tex_count = collected_tex_count;
    for (uint32_t t = 0; t < collected_tex_count; t++)
        snprintf(e->mat_tex_paths[t], QS_PACK_MAX_PATH, "%s", collected_tex_paths[t]);
    e->data.material = mat;
    return mat;
}
