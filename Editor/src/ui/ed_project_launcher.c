#include "ed_project_launcher.h"
#include "ed_file_browser.h"
#include "quasar.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
  #include <direct.h>
  #define launcher_mkdir(p) _mkdir(p)
  #ifndef S_ISDIR
    #define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
  #endif
#else
  #define launcher_mkdir(p) mkdir((p), 0755)
#endif

/* ================================================================
   LIMITS
   ================================================================ */

#define MAX_RECENT       32
#define MAX_PATH_LEN     1024
#define MAX_NAME_LEN     64

/* ================================================================
   RECENT PROJECTS PERSISTENCE
   ================================================================ */

typedef struct RecentProject {
    char name[MAX_NAME_LEN];
    char path[MAX_PATH_LEN];
} RecentProject;

static void get_config_dir(char *buf, size_t size)
{
#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    if (appdata) {
        snprintf(buf, size, "%s/QuasarEngine", appdata);
        return;
    }
    const char *home = getenv("USERPROFILE");
    if (!home) home = ".";
    snprintf(buf, size, "%s/AppData/Roaming/QuasarEngine", home);
#elif defined(__APPLE__)
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(buf, size, "%s/Library/Application Support/QuasarEngine", home);
#else
    /* Linux / other POSIX — follow XDG Base Directory spec */
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        snprintf(buf, size, "%s/QuasarEngine", xdg);
        return;
    }
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(buf, size, "%s/.config/QuasarEngine", home);
#endif
}

static void get_recent_path(char *buf, size_t size)
{
    char config[MAX_PATH_LEN];
    get_config_dir(config, sizeof(config));
    snprintf(buf, size, "%s/recent_projects.json", config);
}

static int load_recent_projects(RecentProject *out, int max)
{
    char path[MAX_PATH_LEN];
    get_recent_path(path, sizeof(path));

    FILE *f = fopen(path, "r");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return 0; }

    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return 0; }
    size_t read = fread(buf, 1, (size_t)len, f);
    buf[read] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return 0;

    int count = 0;
    cJSON *projects = cJSON_GetObjectItemCaseSensitive(root, "projects");
    if (cJSON_IsArray(projects)) {
        const cJSON *item;
        cJSON_ArrayForEach(item, projects) {
            if (count >= max) break;
            const cJSON *n = cJSON_GetObjectItemCaseSensitive(item, "name");
            const cJSON *p = cJSON_GetObjectItemCaseSensitive(item, "path");
            if (cJSON_IsString(n) && cJSON_IsString(p)) {
                snprintf(out[count].name, MAX_NAME_LEN, "%s", n->valuestring);
                snprintf(out[count].path, MAX_PATH_LEN, "%s", p->valuestring);
                count++;
            }
        }
    }
    cJSON_Delete(root);
    return count;
}

static void save_recent_projects(const RecentProject *list, int count)
{
    char config_dir[MAX_PATH_LEN];
    get_config_dir(config_dir, sizeof(config_dir));

    struct stat st;
    if (stat(config_dir, &st) != 0)
        launcher_mkdir(config_dir);

    char path[MAX_PATH_LEN];
    get_recent_path(path, sizeof(path));

    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_CreateArray();
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", list[i].name);
        cJSON_AddStringToObject(item, "path", list[i].path);
        cJSON_AddItemToArray(arr, item);
    }
    cJSON_AddItemToObject(root, "projects", arr);

    char *json = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json) return;

    FILE *f = fopen(path, "w");
    if (f) {
        fputs(json, f);
        fclose(f);
    }
    free(json);
}

/// Adds a project to the front of the recent list (deduplicating).
static void add_to_recent(RecentProject *list, int *count,
                          const char *name, const char *path)
{
    /* Remove existing entry with same path */
    for (int i = 0; i < *count; i++) {
        if (strcmp(list[i].path, path) == 0) {
            memmove(&list[i], &list[i + 1],
                    (size_t)(*count - i - 1) * sizeof(RecentProject));
            (*count)--;
            break;
        }
    }

    /* Shift everything down and insert at front */
    if (*count >= MAX_RECENT) *count = MAX_RECENT - 1;
    memmove(&list[1], &list[0], (size_t)*count * sizeof(RecentProject));
    snprintf(list[0].name, MAX_NAME_LEN, "%s", name);
    snprintf(list[0].path, MAX_PATH_LEN, "%s", path);
    (*count)++;

    save_recent_projects(list, *count);
}

/* ================================================================
   DEFAULT PROJECTS PATH
   ================================================================ */

static void get_default_projects_dir(char *buf, size_t size)
{
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
    if (!home) home = getenv("HOME");
    if (!home) home = ".";
    snprintf(buf, size, "%s/Documents/QuasarProjects", home);
#elif defined(__APPLE__)
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(buf, size, "%s/Documents/QuasarProjects", home);
#else
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(buf, size, "%s/QuasarProjects", home);
#endif
}

/* ================================================================
   CSS
   ================================================================ */

static const char *g_launcher_css =

    /* Root */
    ".launcher-root {"
    "  background: #111114;"
    "  gap: 0px;"
    "  overflow: hidden;"
    "}"

    /* ---- Left sidebar ---- */

    ".launcher-sidebar {"
    "  width: 200px;"
    "  background: #16161a;"
    "  padding-top: 0px;"
    "  gap: 0px;"
    "  overflow: hidden;"
    "}"

    ".launcher-sidebar-header {"
    "  width: 100%;"
    "  padding: 24px 18px 18px 18px;"
    "  gap: 4px;"
    "  background: #0d0d0f;"
    "}"

    /* Title */
    ".launcher-title {"
    "  color: #6e8aff;"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "  text-align: left;"
    "}"

    /* Version */
    ".launcher-version {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  text-align: left;"
    "}"

    /* Tab buttons */
    ".launcher-tab {"
    "  width: 100%;"
    "  height: 34px;"
    "  background: transparent;"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  text-align: left;"
    "  padding-left: 18px;"
    "  corner-radius: 0px;"
    "}"

    ".launcher-tab-active {"
    "  background: #242430;"
    "  color: #6e8aff;"
    "}"

    /* ---- Right content area ---- */

    ".launcher-content {"
    "  flex-grow: 1;"
    "  background: #111114;"
    "  overflow: hidden;"
    "}"

    ".launcher-page {"
    "  flex-grow: 1;"
    "  width: 100%;"
    "}"

    /* ---- Projects page ---- */

    /* Page header */
    ".launcher-page-header {"
    "  width: 100%;"
    "  height: 44px;"
    "  padding: 0px 16px;"
    "  align-items: center;"
    "  gap: 10px;"
    "  background: #16161a;"
    "}"

    /* Page title */
    ".launcher-page-title {"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "  flex-grow: 1;"
    "  text-align: left;"
    "}"

    /* Scrollable project list */
    ".launcher-list {"
    "  flex-grow: 1;"
    "  width: 100%;"
    "  overflow-y: scroll;"
    "  padding: 8px 12px;"
    "  gap: 4px;"
    "  align-items: flex-start;"
    "}"

    /* Project entry card */
    ".launcher-entry {"
    "  width: 100%;"
    "  height: 48px;"
    "  background: #16161a;"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  text-align: left;"
    "  padding-left: 14px;"
    "  corner-radius: 4px;"
    "}"

    /* Project name */
    ".launcher-entry-name {"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  text-align: left;"
    "}"

    /* Project path */
    ".launcher-entry-path {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  text-align: left;"
    "}"

    /* Empty state */
    ".launcher-empty {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  padding: 20px;"
    "}"

    /* ---- New Project form ---- */

    ".launcher-form {"
    "  width: 100%;"
    "  padding: 20px;"
    "  gap: 14px;"
    "}"

    ".launcher-form-row {"
    "  width: 100%;"
    "  height: 30px;"
    "  align-items: center;"
    "  gap: 10px;"
    "}"

    /* Form label */
    ".launcher-form-label {"
    "  color: #8890b0;"
    "  font-size: 12px;"
    "  width: 60px;"
    "  text-align: right;"
    "}"

    /* Text input */
    ".launcher-input {"
    "  flex-grow: 1;"
    "  height: 28px;"
    "  background: #0d0d0f;"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  padding-left: 10px;"
    "  corner-radius: 4px;"
    "}"

    /* Path display */
    ".launcher-path-display {"
    "  flex-grow: 1;"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  text-align: left;"
    "}"

    /* Standard button */
    ".launcher-btn {"
    "  width: 90px;"
    "  height: 28px;"
    "  background: #1c1c22;"
    "  color: #8890b0;"
    "  font-size: 12px;"
    "  corner-radius: 4px;"
    "}"

    ".launcher-btn-sm {"
    "  width: 70px;"
    "  height: 28px;"
    "  background: #1c1c22;"
    "  color: #8890b0;"
    "  font-size: 12px;"
    "  corner-radius: 4px;"
    "}"

    /* Primary button */
    ".launcher-btn-primary {"
    "  background: #6e8aff;"
    "  color: #0d0d0f;"
    "}"

    ".launcher-form-actions {"
    "  width: 100%;"
    "  height: 34px;"
    "  align-items: center;"
    "  padding-top: 6px;"
    "  gap: 8px;"
    "}"

    /* ---- File browser ---- */
    ".fb-root {"
    "  width: 100%;"
    "  overflow: hidden;"
    "}"

    ".fb-title-bar {"
    "  background: #1c1c22;"
    "  height: 36px;"
    "  width: 100%;"
    "  padding-left: 14px;"
    "  padding-right: 6px;"
    "  align-items: center;"
    "}"

    ".fb-title {"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  font-weight: bold;"
    "  flex-grow: 1;"
    "}"

    ".fb-spacer-grow {"
    "  flex-grow: 1;"
    "}"

    ".fb-close-btn {"
    "  width: 26px;"
    "  height: 26px;"
    "  background: transparent;"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  corner-radius: 4px;"
    "}"

    ".fb-nav-bar {"
    "  background: #111114;"
    "  height: 38px;"
    "  width: 100%;"
    "  flex-shrink: 0;"
    "  padding-left: 8px;"
    "  padding-right: 8px;"
    "  align-items: center;"
    "  gap: 4px;"
    "}"

    ".fb-nav-btn {"
    "  width: 28px;"
    "  height: 24px;"
    "  background: #1c1c22;"
    "  color: #8890b0;"
    "  font-size: 12px;"
    "  corner-radius: 4px;"
    "}"

    ".fb-path-input {"
    "  flex-grow: 1;"
    "  height: 26px;"
    "  background: #0d0d0f;"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  padding-left: 8px;"
    "  corner-radius: 4px;"
    "}"

    ".fb-col-header {"
    "  width: 100%;"
    "  height: 22px;"
    "  flex-shrink: 0;"
    "  padding-left: 14px;"
    "  padding-right: 14px;"
    "  align-items: center;"
    "  background: #1c1c22;"
    "}"

    ".fb-col-name {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  flex-grow: 1;"
    "  text-align: left;"
    "}"

    ".fb-col-size {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  width: 80px;"
    "  text-align: right;"
    "}"

    ".fb-file-list {"
    "  flex-grow: 1;"
    "  overflow-y: scroll;"
    "  padding: 4px;"
    "  gap: 2px;"
    "  align-items: flex-start;"
    "  background: #111114;"
    "}"

    ".fb-entry {"
    "  width: 100%;"
    "  height: 26px;"
    "  background: transparent;"
    "  color: #8890b0;"
    "  font-size: 12px;"
    "  text-align: left;"
    "  padding-left: 8px;"
    "  corner-radius: 3px;"
    "}"

    ".fb-entry-selected {"
    "  background: #242430;"
    "}"

    ".fb-entry-dir {"
    "  color: #6e8aff;"
    "}"

    ".fb-empty {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  padding: 14px;"
    "}"

    ".fb-bottom {"
    "  background: #1c1c22;"
    "  width: 100%;"
    "  min-height: 70px;"
    "  flex-shrink: 0;"
    "  padding: 6px 10px;"
    "  gap: 6px;"
    "}"

    ".fb-bottom-row {"
    "  width: 100%;"
    "  height: 26px;"
    "  align-items: center;"
    "  gap: 8px;"
    "}"

    ".fb-label {"
    "  color: #4a4e6a;"
    "  font-size: 12px;"
    "  width: 50px;"
    "  text-align: right;"
    "}"

    ".fb-selected-name {"
    "  color: #c8d0ff;"
    "  font-size: 12px;"
    "  flex-grow: 1;"
    "  text-align: left;"
    "}"

    ".fb-filter-select {"
    "  width: 200px;"
    "}"

    ".fb-btn {"
    "  width: 80px;"
    "  height: 26px;"
    "  background: #242430;"
    "  color: #8890b0;"
    "  font-size: 12px;"
    "  corner-radius: 4px;"
    "}"

    ".fb-btn-primary {"
    "  background: #6e8aff;"
    "  color: #0d0d0f;"
    "}"
;

/* ================================================================
   LAUNCHER STATE
   ================================================================ */

typedef enum LauncherTab {
    TAB_PROJECTS,
    TAB_NEW_PROJECT,
    TAB_COUNT,
} LauncherTab;

static struct {
    Ca_Instance   *instance;
    Ca_Window     *window;
    Ca_Stylesheet *stylesheet;

    bool           done;
    bool           selected;
    char           selected_path[MAX_PATH_LEN];

    RecentProject  recent[MAX_RECENT];
    int            recent_count;

    LauncherTab    active_tab;

    /* Sidebar tab buttons */
    Ca_Button     *tab_btns[TAB_COUNT];

    /* Projects page */
    Ca_Div        *page_projects;
    Ca_Div        *project_list;  /* rebuilt dynamically */

    /* New Project page */
    Ca_Div        *page_new_project;
    Ca_TextInput  *name_input;
    Ca_Label      *path_label;
    char           new_project_dir[MAX_PATH_LEN];
} s_launcher;

/* ================================================================
   TAB SWITCHING
   ================================================================ */

static void switch_tab(LauncherTab tab)
{
    s_launcher.active_tab = tab;

    ca_set_hidden(s_launcher.page_projects,    tab != TAB_PROJECTS);
    ca_set_hidden(s_launcher.page_new_project, tab != TAB_NEW_PROJECT);

    for (int i = 0; i < TAB_COUNT; i++) {
        ca_set_style(s_launcher.tab_btns[i],
            i == (int)tab ? "launcher-tab launcher-tab-active"
                          : "launcher-tab");
    }
}

/* ================================================================
   CALLBACKS
   ================================================================ */

static void on_tab_projects(Ca_Button *btn, void *data)
{
    (void)btn; (void)data;
    switch_tab(TAB_PROJECTS);
}

static void on_tab_new_project(Ca_Button *btn, void *data)
{
    (void)btn; (void)data;
    switch_tab(TAB_NEW_PROJECT);
}

static void on_entry_click(Ca_Button *btn, void *data)
{
    (void)btn;
    int idx = (int)(intptr_t)data;
    if (idx < 0 || idx >= s_launcher.recent_count) return;

    snprintf(s_launcher.selected_path, MAX_PATH_LEN, "%s",
             s_launcher.recent[idx].path);
    s_launcher.selected = true;
    s_launcher.done = true;

    ca_window_close(s_launcher.window);
}

static void on_open_confirm(const char *path, void *user_data)
{
    (void)user_data;
    if (!path) return;

    /* The user selected a .quasar file — derive the project dir */
    char dir[MAX_PATH_LEN];
    snprintf(dir, sizeof(dir), "%s", path);
    char *last_slash = strrchr(dir, '/');
#ifdef _WIN32
    char *last_bslash = strrchr(dir, '\\');
    if (last_bslash && (!last_slash || last_bslash > last_slash))
        last_slash = last_bslash;
#endif
    if (last_slash) *last_slash = '\0';

    Qs_Project *proj = qs_project_open(dir);
    if (!proj) return;

    snprintf(s_launcher.selected_path, MAX_PATH_LEN, "%s",
             qs_project_path(proj));
    add_to_recent(s_launcher.recent, &s_launcher.recent_count,
                  qs_project_name(proj), qs_project_path(proj));
    qs_project_destroy(proj);

    s_launcher.selected = true;
    s_launcher.done = true;
    ca_window_close(s_launcher.window);
}

static void on_open_project(Ca_Button *btn, void *data)
{
    (void)btn; (void)data;
    static const EdFBFilter filters[] = {
        { "Project Files (*.quasar)", ".quasar" },
    };
    ed_file_browser_open(&(EdFBDesc){
        .mode         = ED_FB_OPEN_FILE,
        .title        = "Open Project",
        .filters      = filters,
        .filter_count = 1,
        .on_confirm   = on_open_confirm,
    });
}

/* -- New Project: browse for location -- */

static void on_browse_path_confirm(const char *path, void *user_data)
{
    (void)user_data;
    if (!path) return;
    snprintf(s_launcher.new_project_dir, MAX_PATH_LEN, "%s", path);
    ca_set_text(s_launcher.path_label, s_launcher.new_project_dir);
}

static void on_browse_path(Ca_Button *btn, void *data)
{
    (void)btn; (void)data;
    ed_file_browser_open(&(EdFBDesc){
        .mode       = ED_FB_OPEN_FOLDER,
        .title      = "Choose Project Location",
        .on_confirm = on_browse_path_confirm,
    });
}

static void on_create_project(Ca_Button *btn, void *data)
{
    (void)btn; (void)data;
    const char *name = ca_get_text(s_launcher.name_input);
    if (!name || name[0] == '\0') return;
    if (s_launcher.new_project_dir[0] == '\0') return;

    char project_path[MAX_PATH_LEN];
    snprintf(project_path, sizeof(project_path), "%s/%s",
             s_launcher.new_project_dir, name);

    Qs_Project *proj = qs_project_create(&(Qs_ProjectDesc){
        .name = name,
        .path = project_path,
    });
    if (!proj) return;

    snprintf(s_launcher.selected_path, MAX_PATH_LEN, "%s",
             qs_project_path(proj));
    add_to_recent(s_launcher.recent, &s_launcher.recent_count,
                  qs_project_name(proj), qs_project_path(proj));
    qs_project_destroy(proj);

    s_launcher.selected = true;
    s_launcher.done = true;
    ca_window_close(s_launcher.window);
}

/* ================================================================
   UI BUILD
   ================================================================ */

static void rebuild_project_list(void)
{
    ca_div_clear(s_launcher.project_list);

    if (s_launcher.recent_count == 0) {
        ca_text(&(Ca_TextDesc){
            .text  = "No projects found.  Create a new project to get started.",
            .style = "launcher-empty",
        });
    }

    for (int i = 0; i < s_launcher.recent_count; i++) {
        ca_btn_begin(&(Ca_BtnDesc){
            .on_click   = on_entry_click,
            .click_data = (void *)(intptr_t)i,
            .style      = "launcher-entry",
            .direction  = CA_VERTICAL,
        });
        ca_text(&(Ca_TextDesc){
            .text  = s_launcher.recent[i].name,
            .style = "launcher-entry-name",
        });
        ca_text(&(Ca_TextDesc){
            .text  = s_launcher.recent[i].path,
            .style = "launcher-entry-path",
        });
        ca_btn_end();
    }

    ca_div_end();
}

static void build_launcher_ui(void)
{
    Ca_Window *win = s_launcher.window;

    /* Root: horizontal layout — sidebar | content */
    ca_ui_begin(win, &(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .style     = "launcher-root",
    });
    {
        /* ========== LEFT SIDEBAR ========== */
        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_VERTICAL,
            .style     = "launcher-sidebar",
        });

        /* Branding */
        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_VERTICAL,
            .style     = "launcher-sidebar-header",
        });
        ca_text(&(Ca_TextDesc){
            .text  = "QUASAR ENGINE",
            .style = "launcher-title",
        });
        ca_text(&(Ca_TextDesc){
            .text  = "v0.1.0",
            .style = "launcher-version",
        });
        ca_div_end();

        /* Tab buttons */
        s_launcher.tab_btns[TAB_PROJECTS] = ca_btn(&(Ca_BtnDesc){
            .text       = "Projects",
            .on_click   = on_tab_projects,
            .style      = "launcher-tab",
        });
        s_launcher.tab_btns[TAB_NEW_PROJECT] = ca_btn(&(Ca_BtnDesc){
            .text       = "New Project",
            .on_click   = on_tab_new_project,
            .style      = "launcher-tab",
        });

        ca_div_end(); /* sidebar */

        /* ========== RIGHT CONTENT ========== */
        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_VERTICAL,
            .style     = "launcher-content",
        });
        {
            /* ---- Projects page ---- */
            s_launcher.page_projects = ca_div_begin(&(Ca_DivDesc){
                .direction = CA_VERTICAL,
                .style     = "launcher-page",
            });

            /* Page header with Open Project button */
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_HORIZONTAL,
                .style     = "launcher-page-header",
            });
            ca_text(&(Ca_TextDesc){
                .text  = "Recent Projects",
                .style = "launcher-page-title",
            });
            ca_btn(&(Ca_BtnDesc){
                .text     = "Open Project",
                .on_click = on_open_project,
                .style    = "launcher-btn",
            });
            ca_div_end();

            /* Project list — rebuilt dynamically */
            s_launcher.project_list = ca_div_begin(&(Ca_DivDesc){
                .direction = CA_VERTICAL,
                .style     = "launcher-list",
            });
            ca_div_end();

            ca_div_end(); /* page_projects */

            /* ---- New Project page ---- */
            s_launcher.page_new_project = ca_div_begin(&(Ca_DivDesc){
                .direction = CA_VERTICAL,
                .style     = "launcher-page",
                .hidden    = true,
            });

            /* Page header */
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_HORIZONTAL,
                .style     = "launcher-page-header",
            });
            ca_text(&(Ca_TextDesc){
                .text  = "Create New Project",
                .style = "launcher-page-title",
            });
            ca_div_end();

            /* Form */
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_VERTICAL,
                .style     = "launcher-form",
            });

            /* Name row */
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_HORIZONTAL,
                .style     = "launcher-form-row",
            });
            ca_text(&(Ca_TextDesc){
                .text  = "Name",
                .style = "launcher-form-label",
            });
            s_launcher.name_input = ca_input(&(Ca_InputDesc){
                .placeholder = "My Project",
                .style       = "launcher-input",
            });
            ca_div_end();

            /* Path row */
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_HORIZONTAL,
                .style     = "launcher-form-row",
            });
            ca_text(&(Ca_TextDesc){
                .text  = "Path",
                .style = "launcher-form-label",
            });
            s_launcher.path_label = ca_text(&(Ca_TextDesc){
                .text  = s_launcher.new_project_dir,
                .style = "launcher-path-display",
            });
            ca_btn(&(Ca_BtnDesc){
                .text     = "Browse",
                .on_click = on_browse_path,
                .style    = "launcher-btn-sm",
            });
            ca_div_end();

            /* Actions */
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_HORIZONTAL,
                .style     = "launcher-form-actions",
            });
            ca_btn(&(Ca_BtnDesc){
                .text     = "Create",
                .on_click = on_create_project,
                .style    = "launcher-btn launcher-btn-primary",
            });
            ca_div_end();

            ca_div_end(); /* form */
            ca_div_end(); /* page_new_project */
        }
        ca_div_end(); /* content */
    }
    ca_ui_end();

    /* Highlight initial active tab */
    switch_tab(TAB_PROJECTS);
}

/* ================================================================
   PER-FRAME UPDATE
   ================================================================ */

static void launcher_on_frame(void *data)
{
    (void)data;
    ed_file_browser_update();
}

/* ================================================================
   PUBLIC
   ================================================================ */

bool ed_project_launcher_run(char *out_path, size_t out_path_size)
{
    memset(&s_launcher, 0, sizeof(s_launcher));

    /* Default new-project directory */
    get_default_projects_dir(s_launcher.new_project_dir,
                             sizeof(s_launcher.new_project_dir));

    /* Load recent projects */
    s_launcher.recent_count = load_recent_projects(
        s_launcher.recent, MAX_RECENT);

    /* Auto-create Sandbox if no recent projects */
    if (s_launcher.recent_count == 0) {
        char projects_dir[MAX_PATH_LEN];
        get_default_projects_dir(projects_dir, sizeof(projects_dir));

        char sandbox[MAX_PATH_LEN];
        snprintf(sandbox, sizeof(sandbox), "%s/Sandbox", projects_dir);

        struct stat st;
        bool exists = (stat(sandbox, &st) == 0 && S_ISDIR(st.st_mode));

        if (!exists) {
            Qs_Project *proj = qs_project_create(&(Qs_ProjectDesc){
                .name = "Sandbox",
                .path = sandbox,
            });
            if (proj) {
                add_to_recent(s_launcher.recent, &s_launcher.recent_count,
                              "Sandbox", sandbox);
                qs_project_destroy(proj);
            }
        } else {
            add_to_recent(s_launcher.recent, &s_launcher.recent_count,
                          "Sandbox", sandbox);
        }
    }

    /* Create Causality instance for the launcher */
    s_launcher.instance = ca_instance_create(&(Ca_InstanceDesc){
        .app_name     = "Quasar Engine",
        .font_size_px = 0.0f,
    });
    if (!s_launcher.instance) return false;

    /* Parse and apply stylesheet */
    s_launcher.stylesheet = ca_css_parse(g_launcher_css);
    if (s_launcher.stylesheet)
        ca_instance_set_stylesheet(s_launcher.instance, s_launcher.stylesheet);

    /* Create launcher window */
    s_launcher.window = ca_window_create(s_launcher.instance, &(Ca_WindowDesc){
        .title  = "Quasar Engine",
        .width  = 600,
        .height = 400,
    });
    if (!s_launcher.window) {
        if (s_launcher.stylesheet) ca_css_destroy(s_launcher.stylesheet);
        ca_instance_destroy(s_launcher.instance);
        return false;
    }

    ca_window_set_on_frame(s_launcher.window, launcher_on_frame, NULL);

    /* Init file browser for Open Project / Browse path */
    ed_file_browser_init(s_launcher.instance);

    /* Build UI then populate the project list */
    build_launcher_ui();
    rebuild_project_list();

    /* Run event loop */
    while (ca_instance_tick(s_launcher.instance)) {
        if (s_launcher.done) break;
    }

    bool result = s_launcher.selected;
    if (result && out_path)
        snprintf(out_path, out_path_size, "%s", s_launcher.selected_path);

    /* If a project was selected, add to recent */
    if (result) {
        const char *slash = strrchr(s_launcher.selected_path, '/');
        const char *name = slash ? slash + 1 : s_launcher.selected_path;
        add_to_recent(s_launcher.recent, &s_launcher.recent_count,
                      name, s_launcher.selected_path);
    }

    /* Cleanup */
    ed_file_browser_close();
    if (s_launcher.stylesheet) ca_css_destroy(s_launcher.stylesheet);
    ca_instance_destroy(s_launcher.instance);
    memset(&s_launcher, 0, sizeof(s_launcher));

    return result;
}
