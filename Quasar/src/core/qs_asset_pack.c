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
#include <stdatomic.h>
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

bool qs_asset_pack_read_texture(const char *path,
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
    void *pixels = qs_malloc(size, QS_MEM_ASSET);
    if (!pixels) { fclose(f); return false; }
    if (fread(pixels, 1, size, f) != size) {
        qs_free(pixels); fclose(f); return false;
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
    unsigned int *remap = qs_malloc(sizeof(unsigned int) * vc, QS_MEM_ASSET);
    if (!remap) return;
    size_t new_vc = meshopt_generateVertexRemap(remap,
                        idx, ic, verts, vc, sizeof(Qs_Vertex));

    Qs_Vertex *new_verts = qs_malloc(sizeof(Qs_Vertex) * new_vc, QS_MEM_ASSET);
    uint32_t  *new_idx   = qs_malloc(sizeof(uint32_t)  * ic, QS_MEM_ASSET);
    if (!new_verts || !new_idx) {
        qs_free(remap); qs_free(new_verts); qs_free(new_idx);
        return;
    }
    meshopt_remapVertexBuffer(new_verts, verts, vc, sizeof(Qs_Vertex), remap);
    meshopt_remapIndexBuffer(new_idx, idx, ic, remap);
    qs_free(remap);

    /* 2) Vertex cache optimisation */
    meshopt_optimizeVertexCache(new_idx, new_idx, ic, new_vc);

    /* 3) Overdraw — needs vertex positions */
    meshopt_optimizeOverdraw(new_idx, new_idx, ic,
                             (const float *)new_verts,
                             new_vc, sizeof(Qs_Vertex), 1.05f);

    /* 4) Vertex fetch */
    meshopt_optimizeVertexFetch(new_verts, new_idx, ic,
                                new_verts, new_vc, sizeof(Qs_Vertex));

    qs_free(verts);
    qs_free(idx);
    *verts_io = new_verts;
    *vc_io    = (uint32_t)new_vc;
    *idx_io   = new_idx;
}

static bool write_qsmesh(const char *path, const Qs_ImportMesh *m, bool optimize)
{
    if (!m || !m->vertices || !m->indices) return false;

    Qs_Vertex *verts = qs_malloc(sizeof(Qs_Vertex) * m->vertex_count, QS_MEM_ASSET);
    uint32_t  *idx   = qs_malloc(sizeof(uint32_t)  * m->index_count, QS_MEM_ASSET);
    if (!verts || !idx) { qs_free(verts); qs_free(idx); return false; }
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
        qs_free(verts); qs_free(idx);
        QS_LOG_ERROR("Failed to open .qsmesh for write: %s", path);
        return false;
    }
    fwrite(&h, sizeof(h), 1, f);
    fwrite(verts, sizeof(Qs_Vertex), vc, f);
    fwrite(idx,   sizeof(uint32_t),  ic, f);
    fclose(f);

    qs_free(verts); qs_free(idx);
    return true;
}

bool qs_asset_pack_read_mesh(const char *path,
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
    Qs_Vertex *v = qs_malloc(sizeof(Qs_Vertex) * h.vertex_count, QS_MEM_ASSET);
    uint32_t  *i = qs_malloc(sizeof(uint32_t)  * h.index_count, QS_MEM_ASSET);
    if (!v || !i) { qs_free(v); qs_free(i); fclose(f); return false; }
    if (fread(v, sizeof(Qs_Vertex), h.vertex_count, f) != h.vertex_count ||
        fread(i, sizeof(uint32_t),  h.index_count,  f) != h.index_count)
    {
        qs_free(v); qs_free(i); fclose(f); return false;
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
    if (!f) { qs_free(str); return false; }
    fputs(str, f);
    fclose(f);
    qs_free(str);
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
        tex_paths = qs_calloc(result->texture_count, sizeof(*tex_paths), QS_MEM_ASSET);
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
        mat_paths = qs_calloc(result->material_count, sizeof(*mat_paths), QS_MEM_ASSET);
        if (!mat_paths) { qs_free(tex_paths); return false; }
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
        mesh_paths = qs_calloc(result->mesh_count, sizeof(*mesh_paths), QS_MEM_ASSET);
        if (!mesh_paths) { qs_free(tex_paths); qs_free(mat_paths); return false; }
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
            qs_free(str);
        }
    }
    cJSON_Delete(root_json);

    qs_free(tex_paths); qs_free(mat_paths); qs_free(mesh_paths);

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
    /* For CACHE_MAT: slot-indexed texture paths (slots 0-4 = base_color, mr, normal,
       occlusion, emissive).  Each non-empty slot holds one texture ref. */
    char       mat_tex_paths[5][QS_PACK_MAX_PATH];
    union {
        Qs_Mesh     *mesh;
        Qs_Material *material;
        Qs_Texture  *texture;
    } data;
} CacheEntry;

static CacheEntry *g_cache[QS_PACK_CACHE_MAX];
static uint32_t    g_cache_count;

/* ================================================================
   ASYNC LOAD QUEUE
   ================================================================
   Background jobs read packed files from disk (CPU-only work).
   The main-thread pump walks completed requests, does GPU upload,
   and promotes entries into g_cache (ref_count = 0).

   Callers that receive a NULL from _async set mc->mesh/material = NULL
   and call _async again next frame; the guard on mc->mesh != NULL in
   mesh_comp_destroy prevents releasing a ref that was never acquired.
   ================================================================ */

typedef struct PendingLoad {
    char         path[QS_PACK_MAX_PATH];
    CacheKind    kind;
    /* Set main-thread before job fires; job discards staging data on true. */
    bool         abandoned;
    /* Set by the job thread when disk I/O is done; read by the pump. */
    atomic_bool  cpu_done;

    /* For CACHE_TEX: if mat_path is non-empty, swap texture into that
       material's slot after GPU upload. */
    char         mat_path[QS_PACK_MAX_PATH];
    uint32_t     mat_slot;

    union {
        struct {
            Qs_MeshFileHeader h;
            Qs_Vertex        *v;
            uint32_t         *idx;
            bool              ok;
        } mesh;
        struct {
            Qs_TexFileHeader  h;
            void             *pixels;
            bool              ok;
        } tex;
    } stage;
} PendingLoad;

/* Max simultaneous in-flight loads (mesh + tex combined). */
#define QS_PACK_PENDING_MAX 512

static PendingLoad *g_pending[QS_PACK_PENDING_MAX];
static uint32_t     g_pending_count;

static PendingLoad *pending_find(const char *path, CacheKind kind)
{
    for (uint32_t i = 0; i < g_pending_count; i++)
        if (g_pending[i]->kind == kind &&
            strcmp(g_pending[i]->path, path) == 0)
            return g_pending[i];
    return NULL;
}

static PendingLoad *pending_add(const char *path, CacheKind kind)
{
    if (g_pending_count >= QS_PACK_PENDING_MAX) {
        QS_LOG_WARN("Async load queue full (%d)", QS_PACK_PENDING_MAX);
        return NULL;
    }
    PendingLoad *req = qs_calloc(1, sizeof(PendingLoad), QS_MEM_ASSET);
    if (!req) return NULL;
    snprintf(req->path, sizeof(req->path), "%s", path);
    req->kind = kind;
    g_pending[g_pending_count++] = req;
    return req;
}

/* Job: read a .qsmesh from disk into req->stage.mesh (CPU only). */
static void job_load_mesh(void *data)
{
    PendingLoad *req = (PendingLoad *)data;
    if (!req->abandoned) {
        req->stage.mesh.ok = qs_asset_pack_read_mesh(req->path,
                                         &req->stage.mesh.h,
                                         &req->stage.mesh.v,
                                         &req->stage.mesh.idx);
    }
    atomic_store(&req->cpu_done, true);
}

/* Job: read a .qstex from disk into req->stage.tex (CPU only). */
static void job_load_tex(void *data)
{
    PendingLoad *req = (PendingLoad *)data;
    uint32_t pixel_size = 0;
    if (!req->abandoned) {
        req->stage.tex.ok = qs_asset_pack_read_texture(req->path,
                                        &req->stage.tex.h,
                                        &req->stage.tex.pixels,
                                        &pixel_size);
    }
    atomic_store(&req->cpu_done, true);
}

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
    CacheEntry *e = qs_calloc(1, sizeof(CacheEntry), QS_MEM_ASSET);
    if (!e) return NULL;
    snprintf(e->path, sizeof(e->path), "%s", path);
    e->kind = kind;
    g_cache[g_cache_count++] = e;
    return e;
}

/* Dispatch an async texture load; if mat_path/slot are given, the pump
   will call swap_texture when the load completes (material slot streaming). */
static void dispatch_tex_async(Qs_JobSystem *jobs,
                                const char   *abs_tex_path,
                                const char   *mat_path,
                                uint32_t      mat_slot)
{
    /* Already in main cache? Nothing to do — the pump's swap_texture call
       (or an earlier load) already placed it there. */
    if (cache_find(abs_tex_path, CACHE_TEX)) return;
    /* Already queued? */
    if (pending_find(abs_tex_path, CACHE_TEX)) return;

    PendingLoad *req = pending_add(abs_tex_path, CACHE_TEX);
    if (!req) return;
    if (mat_path && mat_path[0]) {
        snprintf(req->mat_path, sizeof(req->mat_path), "%s", mat_path);
        req->mat_slot = mat_slot;
    }
    qs_job_dispatch(jobs, &(Qs_JobDesc){ .fn = job_load_tex, .data = req }, NULL);
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
        qs_free(e);
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
            /* Remove from the live array BEFORE recursive texture releases.
               Each recursive cache_release does its own swap-compaction; if
               this entry is the last element, that compaction moves this
               pointer into a live slot, and the subsequent qs_free(e) leaves a
               dangling pointer inside g_cache[0..g_cache_count-1]. */
            g_cache[i] = g_cache[--g_cache_count];
            if (kind == CACHE_MAT) {
                /* Release all tracked texture refs (slots 0-4) */
                for (uint32_t t = 0; t < 5; t++) {
                    if (e->mat_tex_paths[t][0])
                        cache_release(e->mat_tex_paths[t], CACHE_TEX);
                }
            }
            switch (kind) {
            case CACHE_MESH: qs_mesh_destroy(e->data.mesh);         break;
            case CACHE_MAT:  qs_material_destroy(e->data.material); break;
            case CACHE_TEX:  qs_texture_destroy(e->data.texture);   break;
            }
            qs_free(e);
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

/* Forward declaration — defined below; used by swap_texture and the pump. */
static Qs_Texture *qs_asset_cache_texture(Qs_Engine *engine, const char *abs_path);

/// Atomically swap the texture in one slot of a cached material.
/// Releases the ref for the old texture at that slot, acquires one for the new,
/// updates the material's tracked slot path, and binds it via qs_material_set_texture.
/// abs_mat_path must be the same absolute path passed to qs_asset_cache_material.
/// Pass NULL/empty abs_new_tex_path to clear the slot (revert to default fallback).
Qs_Texture *qs_asset_cache_material_swap_texture(
    Qs_Engine  *engine,
    const char *abs_mat_path,
    uint32_t    slot,
    const char *abs_new_tex_path)
{
    if (!engine || !abs_mat_path || slot >= 5) return NULL;
    CacheEntry *mat_e = cache_find(abs_mat_path, CACHE_MAT);
    if (!mat_e) return NULL;

    /* Release the currently tracked texture for this slot */
    if (mat_e->mat_tex_paths[slot][0])
        cache_release(mat_e->mat_tex_paths[slot], CACHE_TEX);

    Qs_Texture *new_tex = NULL;
    if (abs_new_tex_path && *abs_new_tex_path) {
        new_tex = qs_asset_cache_texture(engine, abs_new_tex_path);   /* acquires ref */
        snprintf(mat_e->mat_tex_paths[slot], QS_PACK_MAX_PATH, "%s", abs_new_tex_path);
    } else {
        mat_e->mat_tex_paths[slot][0] = '\0';
    }

    qs_material_set_texture(mat_e->data.material, slot, new_tex);
    return new_tex;
}

static Qs_Texture *qs_asset_cache_texture(Qs_Engine *engine, const char *abs_path)
{
    if (!engine || !abs_path || !*abs_path) return NULL;
    CacheEntry *hit = cache_find(abs_path, CACHE_TEX);
    if (hit) { hit->ref_count++; return hit->data.texture; }

    Qs_TexFileHeader h;
    void *pixels = NULL; uint32_t size = 0;
    if (!qs_asset_pack_read_texture(abs_path, &h, &pixels, &size)) return NULL;

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
    qs_free(pixels);
    if (!tex) return NULL;

    CacheEntry *e = cache_add(abs_path, CACHE_TEX);
    if (!e) { qs_texture_destroy(tex); return NULL; }
    e->ref_count = 1;
    e->data.texture = tex;
    return tex;
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

/* ================================================================
   ASYNC PUBLIC API
   ================================================================ */

void qs_asset_cache_pump(Qs_Engine *engine)
{
    if (!engine) return;

    /* Walk pending list back-to-front so swap-remove doesn't skip entries. */
    for (uint32_t i = g_pending_count; i-- > 0; ) {
        PendingLoad *req = g_pending[i];
        if (!atomic_load(&req->cpu_done)) continue;

        /* Swap-remove from pending list. */
        g_pending[i] = g_pending[--g_pending_count];

        if (req->abandoned) {
            /* Discard staging data. */
            if (req->kind == CACHE_MESH) {
                qs_free(req->stage.mesh.v);
                qs_free(req->stage.mesh.idx);
            } else if (req->kind == CACHE_TEX) {
                qs_free(req->stage.tex.pixels);
            }
            qs_free(req);
            continue;
        }

        if (req->kind == CACHE_MESH) {
            if (!req->stage.mesh.ok) {
                QS_LOG_WARN("Async mesh load failed: %s", req->path);
                qs_free(req);
                continue;
            }
            Qs_MeshFileHeader *h = &req->stage.mesh.h;
            Qs_MeshDesc md = {
                .name         = h->surface_name[0] ? h->surface_name : req->path,
                .vertices     = req->stage.mesh.v,
                .vertex_count = h->vertex_count,
                .indices      = req->stage.mesh.idx,
                .index_count  = h->index_count,
                .index_type   = QS_INDEX_TYPE_UINT32,
            };
            Qs_Mesh *mesh = qs_mesh_create(engine, &md);
            qs_free(req->stage.mesh.v);
            qs_free(req->stage.mesh.idx);
            if (mesh) {
                /* ref_count = 0: first _async caller next frame increments to 1. */
                CacheEntry *e = cache_add(req->path, CACHE_MESH);
                if (e) { e->ref_count = 0; e->data.mesh = mesh; }
                else   { qs_mesh_destroy(mesh); }
            }

        } else if (req->kind == CACHE_TEX) {
            if (!req->stage.tex.ok) {
                QS_LOG_WARN("Async texture load failed: %s", req->path);
                qs_free(req);
                continue;
            }
            Qs_TexFileHeader *h = &req->stage.tex.h;
            Qs_TextureDesc td = {
                .name          = req->path,
                .width         = h->width,
                .height        = h->height,
                .format        = (Qs_TextureFormat)h->format,
                .pixels        = req->stage.tex.pixels,
                .generate_mips = (h->flags & (1u << 1)) != 0,
                .min_filter    = (Qs_TextureFilter)h->min_filter,
                .mag_filter    = (Qs_TextureFilter)h->mag_filter,
                .wrap_u        = (Qs_TextureWrap)h->wrap_u,
                .wrap_v        = (Qs_TextureWrap)h->wrap_v,
            };
            Qs_Texture *tex = qs_texture_create(engine, &td);
            qs_free(req->stage.tex.pixels);
            if (tex) {
                /* Add to cache before swap_texture so that swap_texture
                   finds it via cache_find (ref_count will be bumped to 1). */
                CacheEntry *e = cache_add(req->path, CACHE_TEX);
                if (e) {
                    e->ref_count = 0;
                    e->data.texture = tex;
                    /* If this texture was for a material slot, stream it in. */
                    if (req->mat_path[0]) {
                        qs_asset_cache_material_swap_texture(engine,
                                                             req->mat_path,
                                                             req->mat_slot,
                                                             req->path);
                    }
                } else {
                    qs_texture_destroy(tex);
                }
            }
        }
        qs_free(req);
    }
}

Qs_Mesh *qs_asset_cache_mesh_async(Qs_Engine    *engine,
                                   Qs_JobSystem *jobs,
                                   const char   *abs_path)
{
    if (!engine || !jobs || !abs_path || !*abs_path) return NULL;

    /* Cache hit (READY): bump ref and return. */
    CacheEntry *hit = cache_find(abs_path, CACHE_MESH);
    if (hit) { hit->ref_count++; return hit->data.mesh; }

    /* Already pending: wait for pump. */
    if (pending_find(abs_path, CACHE_MESH)) return NULL;

    /* New request: dispatch background read job. */
    PendingLoad *req = pending_add(abs_path, CACHE_MESH);
    if (!req) return NULL;
    qs_job_dispatch(jobs, &(Qs_JobDesc){ .fn = job_load_mesh, .data = req }, NULL);
    return NULL;
}

Qs_Material *qs_asset_cache_material_async(Qs_Engine    *engine,
                                           Qs_JobSystem *jobs,
                                           const char   *abs_path)
{
    if (!engine || !jobs || !abs_path || !*abs_path) return NULL;

    /* Cache hit: bump ref and return. */
    CacheEntry *hit = cache_find(abs_path, CACHE_MAT);
    if (hit) { hit->ref_count++; return hit->data.material; }

    /* ---- Parse .qsmat synchronously (sub-millisecond JSON read). ---- */
    FILE *f = fopen(abs_path, "rb");
    if (!f) { QS_LOG_ERROR("Failed to open .qsmat: %s", abs_path); return NULL; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return NULL; }
    char *buf = qs_malloc((size_t)len + 1, QS_MEM_ASSET);
    if (!buf) { fclose(f); return NULL; }
    size_t nread = fread(buf, 1, (size_t)len, f); buf[nread] = '\0'; fclose(f);

    cJSON *root = cJSON_Parse(buf);
    qs_free(buf);
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

    /* All texture slots start NULL → material system uses engine fallbacks. */

    /* Collect texture paths for async dispatch (after material is in cache). */
    char mat_dir[QS_PACK_MAX_PATH];
    path_dirname(abs_path, mat_dir, sizeof(mat_dir));
    const cJSON *texs = cJSON_GetObjectItemCaseSensitive(root, "textures");

    char slot_tex_paths[5][QS_PACK_MAX_PATH];
    memset(slot_tex_paths, 0, sizeof(slot_tex_paths));

    static const struct { const char *json_key; uint32_t slot; } TEX_SLOTS[] = {
        { "base_color",         0 },
        { "metallic_roughness", 1 },
        { "normal",             2 },
        { "occlusion",          3 },
        { "emissive",           4 },
    };
    if (cJSON_IsObject(texs)) {
        for (uint32_t s = 0; s < 5; s++) {
            const cJSON *v = cJSON_GetObjectItemCaseSensitive(texs, TEX_SLOTS[s].json_key);
            if (cJSON_IsString(v) && v->valuestring[0]) {
                if (path_is_absolute(v->valuestring))
                    snprintf(slot_tex_paths[s], QS_PACK_MAX_PATH, "%s", v->valuestring);
                else
                    snprintf(slot_tex_paths[s], QS_PACK_MAX_PATH,
                             "%s/%s", mat_dir, v->valuestring);
            }
        }
    }

    /* Create material with no textures (fallbacks). */
    Qs_Material *mat = qs_material_create(engine, &md);
    cJSON_Delete(root);
    if (!mat) return NULL;

    CacheEntry *e = cache_add(abs_path, CACHE_MAT);
    if (!e) { qs_material_destroy(mat); return NULL; }
    e->ref_count = 1;
    /* mat_tex_paths start empty — each slot will be filled by the pump via
       swap_texture as background texture loads complete. */
    e->data.material = mat;

    /* Dispatch background texture loads.  The pump's swap_texture call will
       bind each texture into the material slot when it finishes. */
    for (uint32_t s = 0; s < 5; s++) {
        if (slot_tex_paths[s][0])
            dispatch_tex_async(jobs, slot_tex_paths[s], abs_path, s);
    }

    return mat;
}

/* ================================================================
   IMPORT RESULT CLEANUP  (was qs_asset.c)
   ================================================================ */

void qs_import_result_free(Qs_ImportResult *result)
{
    if (!result) return;
    for (uint32_t i = 0; i < result->texture_count; i++)
        qs_free(result->textures[i].pixels);
    qs_free(result->textures);
    for (uint32_t i = 0; i < result->mesh_count; i++) {
        qs_free(result->meshes[i].vertices);
        qs_free(result->meshes[i].indices);
    }
    qs_free(result->meshes);
    qs_free(result->materials);
    qs_free(result->nodes);
    memset(result, 0, sizeof(*result));
}

/* ================================================================
   ASSET ENGINE SYSTEM  (was qs_asset.c)
   ================================================================ */

static bool asset_system_init(Qs_System *system, Qs_Engine *engine)
{
    (void)system;
    qs_pack_set_active_engine(engine);
    return true;
}

static void asset_system_shutdown(Qs_System *system, Qs_Engine *engine)
{
    (void)system; (void)engine;

    /* Abandon all in-flight async requests.  Jobs that haven't fired yet
       will check the flag and discard staging data without calling GPU APIs.
       Jobs already running will complete disk I/O normally; the pump won't
       be called again, so their staging data is freed here instead. */
    for (uint32_t i = 0; i < g_pending_count; i++) {
        PendingLoad *req = g_pending[i];
        req->abandoned = true;
        /* If the job already ran (cpu_done), free staging now.
           If it hasn't fired yet, the job will free nothing (just exits);
           we free here as well — safe because the job won't touch staging
           after setting cpu_done, and we're on the main thread. */
        if (atomic_load(&req->cpu_done)) {
            if (req->kind == CACHE_MESH) {
                qs_free(req->stage.mesh.v);
                qs_free(req->stage.mesh.idx);
            } else if (req->kind == CACHE_TEX) {
                qs_free(req->stage.tex.pixels);
            }
        }
        qs_free(req);
    }
    g_pending_count = 0;

    qs_asset_cache_clear();
    qs_pack_set_active_engine(NULL);
}

static void asset_system_update(Qs_System *system, Qs_Engine *engine, float dt)
{
    (void)system; (void)dt;
    qs_asset_cache_pump(engine);
}

Qs_SystemDesc qs_asset_system_desc(void)
{
    return (Qs_SystemDesc){
        .name      = "Asset",
        .data_size = 0,
        .init      = asset_system_init,
        .shutdown  = asset_system_shutdown,
        .update    = asset_system_update,
    };
}
