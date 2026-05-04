#include "qs_project.h"
#include "qs_log.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
  #include <direct.h>
  #define qs_mkdir(p) _mkdir(p)
  #ifndef S_ISDIR
    #define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
  #endif
#else
  #include <dirent.h>
  #define qs_mkdir(p) mkdir((p), 0755)
#endif

/* ================================================================
   INTERNAL
   ================================================================ */

#define QS_MAX_PATH       1024
#define QS_MAX_PROTOTYPES 1024
#define QS_MAX_SCAN       4096   /* max scanned assets per type */

struct Qs_Project {
    char name[64];
    char path[QS_MAX_PATH];
    char file[QS_MAX_PATH];   /* full path to the .quasar file */

    /* Asset DB — project-relative paths of registered prototypes */
    char  *prototypes[QS_MAX_PROTOTYPES];
    uint32_t prototype_count;

    /* Scanned assets — populated by qs_project_scan_assets().
       Stored as project-relative paths (heap-allocated strings). */
    char    **scan_textures;
    uint32_t  scan_tex_count;
    uint32_t  scan_tex_cap;

    char    **scan_materials;
    uint32_t  scan_mat_count;
    uint32_t  scan_mat_cap;

    char    **scan_meshes;
    uint32_t  scan_mesh_count;
    uint32_t  scan_mesh_cap;
};

static bool path_is_absolute(const char *p)
{
    if (!p || !*p) return false;
    if (p[0] == '/' || p[0] == '\\') return true;
#ifdef _WIN32
    if (p[1] == ':') return true;
#endif
    return false;
}

static void normalize_slashes(char *s)
{
    for (; *s; s++) if (*s == '\\') *s = '/';
}

static bool dir_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool ensure_dir(const char *path)
{
    if (dir_exists(path)) return true;
    return qs_mkdir(path) == 0;
}

/// Finds the .quasar file in a project directory and returns the filename stem.
/// Writes the full .quasar path to out_file.  Returns true if found.
static bool find_quasar_file(const char *dir, char *out_file, size_t out_size)
{
    /* Convention: <ProjectName>.quasar lives at the root of the project dir.
       We look for any .quasar file.  For simplicity, use the directory name
       as the expected project name. */
    char path[QS_MAX_PATH];

    /* Try <dir>/basename.quasar first */
    const char *slash = strrchr(dir, '/');
#ifdef _WIN32
    const char *bslash = strrchr(dir, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
    const char *basename = slash ? slash + 1 : dir;

    snprintf(path, sizeof(path), "%s/%s.quasar", dir, basename);
    FILE *f = fopen(path, "r");
    if (f) {
        fclose(f);
        snprintf(out_file, out_size, "%s", path);
        return true;
    }

    return false;
}

static char *read_file_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t read = fread(buf, 1, (size_t)len, f);
    buf[read] = '\0';
    fclose(f);
    return buf;
}

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && !S_ISDIR(st.st_mode);
}

static bool write_project_file(const Qs_Project *p)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", p->name);
    cJSON_AddStringToObject(root, "version", "0.1.0");
    cJSON_AddStringToObject(root, "engine_version", "0.1.0");

    /* asset_db */
    cJSON *asset_db = cJSON_CreateObject();
    cJSON *protos   = cJSON_CreateArray();
    for (uint32_t i = 0; i < p->prototype_count; i++)
        cJSON_AddItemToArray(protos, cJSON_CreateString(p->prototypes[i]));
    cJSON_AddItemToObject(asset_db, "prototypes", protos);
    cJSON_AddItemToObject(root, "asset_db", asset_db);

    char *json = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json) return false;

    FILE *f = fopen(p->file, "wb");
    if (!f) { free(json); return false; }
    fputs(json, f);
    fclose(f);
    free(json);
    return true;
}

/* ================================================================
   PUBLIC API
   ================================================================ */

Qs_Project *qs_project_create(const Qs_ProjectDesc *desc)
{
    if (!desc || !desc->name || !desc->path) return NULL;

    /* Create project directory */
    if (!ensure_dir(desc->path)) {
        QS_LOG_ERROR("Failed to create project directory: %s", desc->path);
        return NULL;
    }

    /* Create subdirectories */
    char sub[QS_MAX_PATH];
    snprintf(sub, sizeof(sub), "%s/Assets", desc->path);
    ensure_dir(sub);
    snprintf(sub, sizeof(sub), "%s/scenes", desc->path);
    ensure_dir(sub);
    snprintf(sub, sizeof(sub), "%s/scripts", desc->path);
    ensure_dir(sub);

    /* Stage a project struct then write it */
    Qs_Project staged = {0};
    snprintf(staged.name, sizeof(staged.name), "%s", desc->name);
    snprintf(staged.path, sizeof(staged.path), "%s", desc->path);
    snprintf(staged.file, sizeof(staged.file), "%s/%s.quasar",
             desc->path, desc->name);
    normalize_slashes(staged.path);
    normalize_slashes(staged.file);

    if (!write_project_file(&staged)) {
        QS_LOG_ERROR("Failed to write project file: %s", staged.file);
        return NULL;
    }

    QS_LOG_INFO("Project '%s' created at %s", desc->name, desc->path);
    return qs_project_open(desc->path);
}

Qs_Project *qs_project_open(const char *project_dir)
{
    if (!project_dir) return NULL;

    char quasar_path[QS_MAX_PATH];
    if (!find_quasar_file(project_dir, quasar_path, sizeof(quasar_path))) {
        QS_LOG_ERROR("No .quasar file found in: %s", project_dir);
        return NULL;
    }

    char *json_text = read_file_text(quasar_path);
    if (!json_text) {
        QS_LOG_ERROR("Failed to read project file: %s", quasar_path);
        return NULL;
    }

    cJSON *root = cJSON_Parse(json_text);
    free(json_text);
    if (!root) {
        QS_LOG_ERROR("Failed to parse project file: %s", quasar_path);
        return NULL;
    }

    const cJSON *name_val = cJSON_GetObjectItemCaseSensitive(root, "name");
    const char *name = cJSON_IsString(name_val) ? name_val->valuestring : "Untitled";

    Qs_Project *proj = calloc(1, sizeof(Qs_Project));
    if (!proj) { cJSON_Delete(root); return NULL; }

    snprintf(proj->name, sizeof(proj->name), "%s", name);
    snprintf(proj->path, sizeof(proj->path), "%s", project_dir);
    snprintf(proj->file, sizeof(proj->file), "%s", quasar_path);
    normalize_slashes(proj->path);
    normalize_slashes(proj->file);

    /* asset_db.prototypes */
    const cJSON *asset_db = cJSON_GetObjectItemCaseSensitive(root, "asset_db");
    if (cJSON_IsObject(asset_db)) {
        const cJSON *protos = cJSON_GetObjectItemCaseSensitive(asset_db, "prototypes");
        if (cJSON_IsArray(protos)) {
            const cJSON *p;
            cJSON_ArrayForEach(p, protos) {
                if (cJSON_IsString(p) &&
                    proj->prototype_count < QS_MAX_PROTOTYPES)
                {
                    proj->prototypes[proj->prototype_count++] =
                        strdup(p->valuestring);
                }
            }
        }
    }

    cJSON_Delete(root);
    QS_LOG_INFO("Project '%s' opened from %s (%u prototypes)",
                proj->name, proj->path, proj->prototype_count);
    return proj;
}

bool qs_project_save(const Qs_Project *project)
{
    if (!project) return false;
    if (!write_project_file(project)) {
        QS_LOG_ERROR("Failed to save project file: %s", project->file);
        return false;
    }
    return true;
}

const char *qs_project_name(const Qs_Project *project)
{
    return project ? project->name : NULL;
}

const char *qs_project_path(const Qs_Project *project)
{
    return project ? project->path : NULL;
}

void qs_project_destroy(Qs_Project *project)
{
    if (!project) return;
    for (uint32_t i = 0; i < project->prototype_count; i++)
        free(project->prototypes[i]);

    /* Free scan arrays */
    for (uint32_t i = 0; i < project->scan_tex_count;  i++) free(project->scan_textures[i]);
    for (uint32_t i = 0; i < project->scan_mat_count;  i++) free(project->scan_materials[i]);
    for (uint32_t i = 0; i < project->scan_mesh_count; i++) free(project->scan_meshes[i]);
    free(project->scan_textures);
    free(project->scan_materials);
    free(project->scan_meshes);

    free(project);
}

/* ================================================================
   ASSET DB
   ================================================================ */

uint32_t qs_project_prototype_count(const Qs_Project *project)
{
    return project ? project->prototype_count : 0;
}

const char *qs_project_prototype_path(const Qs_Project *project, uint32_t index)
{
    if (!project || index >= project->prototype_count) return NULL;
    return project->prototypes[index];
}

void qs_project_make_relative(const Qs_Project *project,
                              const char *path,
                              char *out, size_t out_size)
{
    if (!project || !path || !out || out_size == 0) return;
    char buf[QS_MAX_PATH];
    snprintf(buf, sizeof(buf), "%s", path);
    normalize_slashes(buf);

    size_t plen = strlen(project->path);
    if (path_is_absolute(buf) &&
        strncmp(buf, project->path, plen) == 0 &&
        (buf[plen] == '/' || buf[plen] == '\0'))
    {
        const char *rel = buf + plen;
        while (*rel == '/') rel++;
        snprintf(out, out_size, "%s", rel);
    } else {
        snprintf(out, out_size, "%s", buf);
    }
}

void qs_project_resolve(const Qs_Project *project,
                        const char *path,
                        char *out, size_t out_size)
{
    if (!project || !path || !out || out_size == 0) return;
    if (path_is_absolute(path))
        snprintf(out, out_size, "%s", path);
    else
        snprintf(out, out_size, "%s/%s", project->path, path);
    normalize_slashes(out);
}

bool qs_project_register_prototype(Qs_Project *project, const char *path)
{
    if (!project || !path || !*path) return false;

    char rel[QS_MAX_PATH];
    qs_project_make_relative(project, path, rel, sizeof(rel));

    /* Dedup */
    for (uint32_t i = 0; i < project->prototype_count; i++)
        if (strcmp(project->prototypes[i], rel) == 0) return true;

    if (project->prototype_count >= QS_MAX_PROTOTYPES) {
        QS_LOG_ERROR("Project asset_db full (%d)", QS_MAX_PROTOTYPES);
        return false;
    }
    project->prototypes[project->prototype_count++] = strdup(rel);
    QS_LOG_INFO("Registered prototype: %s", rel);
    return true;
}

bool qs_project_unregister_prototype(Qs_Project *project, const char *path)
{
    if (!project || !path) return false;
    char rel[QS_MAX_PATH];
    qs_project_make_relative(project, path, rel, sizeof(rel));

    for (uint32_t i = 0; i < project->prototype_count; i++) {
        if (strcmp(project->prototypes[i], rel) == 0) {
            free(project->prototypes[i]);
            project->prototypes[i] =
                project->prototypes[--project->prototype_count];
            return true;
        }
    }
    return false;
}

/* ================================================================
   ASSET SCAN
   ================================================================ */

static bool str_ends_with(const char *s, const char *suffix)
{
    size_t slen = strlen(s);
    size_t sflen = strlen(suffix);
    if (sflen > slen) return false;
    return strcmp(s + slen - sflen, suffix) == 0;
}

static void scan_push(char ***arr, uint32_t *count, uint32_t *cap, const char *path)
{
    if (*count >= QS_MAX_SCAN) return;
    if (*count >= *cap) {
        uint32_t new_cap = *cap ? *cap * 2 : 64;
        if (new_cap > QS_MAX_SCAN) new_cap = QS_MAX_SCAN;
        char **tmp = (char **)realloc(*arr, new_cap * sizeof(char *));
        if (!tmp) return;
        *arr = tmp;
        *cap = new_cap;
    }
    (*arr)[(*count)++] = strdup(path);
}

static void scan_dir_recursive(Qs_Project *proj, const char *abs_dir)
{
#ifdef _WIN32
    /* Windows: use FindFirstFile / FindNextFile */
    char pattern[QS_MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", abs_dir);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        char child[QS_MAX_PATH];
        snprintf(child, sizeof(child), "%s/%s", abs_dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_dir_recursive(proj, child);
        } else {
            char rel[QS_MAX_PATH];
            qs_project_make_relative(proj, child, rel, sizeof(rel));
            if      (str_ends_with(child, ".qstex"))  scan_push(&proj->scan_textures,  &proj->scan_tex_count,  &proj->scan_tex_cap,  rel);
            else if (str_ends_with(child, ".qsmat"))  scan_push(&proj->scan_materials, &proj->scan_mat_count,  &proj->scan_mat_cap,  rel);
            else if (str_ends_with(child, ".qsmesh")) scan_push(&proj->scan_meshes,    &proj->scan_mesh_count, &proj->scan_mesh_cap, rel);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(abs_dir);
    if (!d) return;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue; /* skip . .. and hidden */

        char child[QS_MAX_PATH];
        snprintf(child, sizeof(child), "%s/%s", abs_dir, entry->d_name);

        struct stat st;
        if (stat(child, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            scan_dir_recursive(proj, child);
        } else {
            char rel[QS_MAX_PATH];
            qs_project_make_relative(proj, child, rel, sizeof(rel));
            if      (str_ends_with(child, ".qstex"))  scan_push(&proj->scan_textures,  &proj->scan_tex_count,  &proj->scan_tex_cap,  rel);
            else if (str_ends_with(child, ".qsmat"))  scan_push(&proj->scan_materials, &proj->scan_mat_count,  &proj->scan_mat_cap,  rel);
            else if (str_ends_with(child, ".qsmesh")) scan_push(&proj->scan_meshes,    &proj->scan_mesh_count, &proj->scan_mesh_cap, rel);
        }
    }
    closedir(d);
#endif
}

void qs_project_scan_assets(Qs_Project *project)
{
    if (!project) return;

    /* Clear previous results */
    for (uint32_t i = 0; i < project->scan_tex_count;  i++) free(project->scan_textures[i]);
    for (uint32_t i = 0; i < project->scan_mat_count;  i++) free(project->scan_materials[i]);
    for (uint32_t i = 0; i < project->scan_mesh_count; i++) free(project->scan_meshes[i]);
    project->scan_tex_count  = 0;
    project->scan_mat_count  = 0;
    project->scan_mesh_count = 0;

    scan_dir_recursive(project, project->path);

    QS_LOG_INFO("Asset scan: %u textures, %u materials, %u meshes",
                project->scan_tex_count, project->scan_mat_count,
                project->scan_mesh_count);
}

uint32_t qs_project_texture_count(const Qs_Project *p)
{
    return p ? p->scan_tex_count : 0;
}
const char *qs_project_texture_path(const Qs_Project *p, uint32_t i)
{
    if (!p || i >= p->scan_tex_count) return NULL;
    return p->scan_textures[i];
}

uint32_t qs_project_material_count(const Qs_Project *p)
{
    return p ? p->scan_mat_count : 0;
}
const char *qs_project_material_path(const Qs_Project *p, uint32_t i)
{
    if (!p || i >= p->scan_mat_count) return NULL;
    return p->scan_materials[i];
}

uint32_t qs_project_mesh_count(const Qs_Project *p)
{
    return p ? p->scan_mesh_count : 0;
}
const char *qs_project_mesh_path(const Qs_Project *p, uint32_t i)
{
    if (!p || i >= p->scan_mesh_count) return NULL;
    return p->scan_meshes[i];
}

uint32_t qs_project_check_assets(const Qs_Project *project)
{
    uint32_t missing = 0;
    char abspath[QS_MAX_PATH];
    for (uint32_t i = 0; i < project->prototype_count; i++) {
        qs_project_resolve(project, project->prototypes[i], abspath, sizeof(abspath));
        if (!file_exists(abspath)) {
            QS_LOG_WARN("Asset DB: missing prototype '%s'",
                        project->prototypes[i]);
            missing++;
        }
    }
    if (missing == 0)
        QS_LOG_INFO("Asset DB OK: %u prototypes", project->prototype_count);
    return missing;
}
