#include "ed_project_launcher.h"
#include "ed_file_browser.h"
#include "../ed_style.h"
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

/* CSS is defined in ed_style.c via g_launcher_css (see ed_style.h). */


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
    Ca_Label      *tab_labels[TAB_COUNT];

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
        ca_set_style(s_launcher.tab_labels[i],
            i == (int)tab ? "launcher-tab-label launcher-tab-label-active"
                          : "launcher-tab-label");
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
    s_launcher.active_tab = TAB_PROJECTS;

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
        s_launcher.tab_btns[TAB_PROJECTS] = ca_btn_begin(&(Ca_BtnDesc){
            .on_click   = on_tab_projects,
            .style      = "launcher-tab launcher-tab-active",
        });
        s_launcher.tab_labels[TAB_PROJECTS] = ca_text(&(Ca_TextDesc){
            .text  = "Projects",
            .style = "launcher-tab-label launcher-tab-label-active",
        });
        ca_btn_end();
        s_launcher.tab_btns[TAB_NEW_PROJECT] = ca_btn_begin(&(Ca_BtnDesc){
            .on_click   = on_tab_new_project,
            .style      = "launcher-tab",
        });
        s_launcher.tab_labels[TAB_NEW_PROJECT] = ca_text(&(Ca_TextDesc){
            .text  = "New Project",
            .style = "launcher-tab-label",
        });
        ca_btn_end();

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
            ca_btn_begin(&(Ca_BtnDesc){
                .text     = "Open Project",
                .on_click = on_open_project,
                .style    = "launcher-btn",
            });
            ca_btn_end();
            ca_div_end();

            /* Page body */
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_VERTICAL,
                .style     = "launcher-page-body",
            });

            /* Project list — rebuilt dynamically */
            s_launcher.project_list = ca_div_begin(&(Ca_DivDesc){
                .direction = CA_VERTICAL,
                .style     = "launcher-list",
            });
            ca_div_end();
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
            ca_btn_begin(&(Ca_BtnDesc){
                .text     = "Create",
                .on_click = on_create_project,
                .style    = "launcher-btn launcher-btn-primary",
            });
            ca_btn_end();
            ca_div_end();

            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_VERTICAL,
                .style     = "launcher-page-body",
            });

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
            ca_btn_begin(&(Ca_BtnDesc){
                .text     = "Browse",
                .on_click = on_browse_path,
                .style    = "launcher-btn-sm",
            });
            ca_btn_end();
            ca_div_end();

            ca_div_end(); /* form */
            ca_div_end(); /* page body */
            ca_div_end(); /* page_new_project */
        }
        ca_div_end(); /* content */
    }
    ca_ui_end();
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
        .font_size_px = ED_FONT_SIZE_PX,
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

    ca_window_set_scale(s_launcher.window, ED_UI_SCALE);
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
