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
  #define qs_mkdir(p) mkdir((p), 0755)
#endif

/* ================================================================
   INTERNAL
   ================================================================ */

#define QS_MAX_PATH 1024

struct Qs_Project {
    char name[64];
    char path[QS_MAX_PATH];
};

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
    FILE *f = fopen(path, "r");
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

static bool write_quasar_file(const char *path, const char *name)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", name);
    cJSON_AddStringToObject(root, "version", "0.1.0");
    cJSON_AddStringToObject(root, "engine_version", "0.1.0");

    char *json = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json) return false;

    FILE *f = fopen(path, "w");
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
    snprintf(sub, sizeof(sub), "%s/assets", desc->path);
    ensure_dir(sub);
    snprintf(sub, sizeof(sub), "%s/scenes", desc->path);
    ensure_dir(sub);
    snprintf(sub, sizeof(sub), "%s/scripts", desc->path);
    ensure_dir(sub);

    /* Write .quasar file */
    char quasar_path[QS_MAX_PATH];
    snprintf(quasar_path, sizeof(quasar_path), "%s/%s.quasar",
             desc->path, desc->name);

    if (!write_quasar_file(quasar_path, desc->name)) {
        QS_LOG_ERROR("Failed to write project file: %s", quasar_path);
        return NULL;
    }

    QS_LOG_INFO("Project '%s' created at %s", desc->name, desc->path);

    /* Open the just-created project */
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

    cJSON_Delete(root);
    QS_LOG_INFO("Project '%s' opened from %s", proj->name, proj->path);
    return proj;
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
    free(project);
}
