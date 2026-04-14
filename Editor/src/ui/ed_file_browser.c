#include "ed_file_browser.h"
#include "ca_theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #define FB_PATH_SEP '\\'
  #define strtok_r strtok_s
  #define strcasecmp _stricmp
#else
  #include <dirent.h>
  #include <sys/stat.h>
  #include <unistd.h>
  #include <pwd.h>
  #define FB_PATH_SEP '/'
#endif

/* ============================================================
   Nerd Font icons (FontAwesome subset in Nerd Fonts)
   ============================================================ */
#define ICON_FOLDER       "\xEF\x81\xBB"   /* U+F07B  */
#define ICON_FILE         "\xEF\x85\x9B"   /* U+F15B  */
#define ICON_ARROW_LEFT   "\xEF\x81\xA0"   /* U+F060  */
#define ICON_ARROW_RIGHT  "\xEF\x81\xA1"   /* U+F061  */
#define ICON_LEVEL_UP     "\xEF\x85\x88"   /* U+F148  */
#define ICON_HOME         "\xEF\x80\x95"   /* U+F015  */
#define ICON_TIMES        "\xEF\x80\x8D"   /* U+F00D  */
#define ICON_FOLDER_OPEN  "\xEF\x81\xBC"   /* U+F07C  */
#define ICON_REFRESH      "\xEF\x80\xA1"   /* U+F021  */

/* ============================================================
   Constants
   ============================================================ */
#define FB_MAX_ENTRIES     200
#define FB_MAX_PATH        1024
#define FB_MAX_HISTORY     32
#define FB_MAX_FILTERS     8
#define FB_MAX_NAME        256
#define FB_MAX_FILTER_EXTS 16

/* ============================================================
   Internal types
   ============================================================ */
typedef struct {
    char    name[FB_MAX_NAME];
    bool    is_dir;
    int64_t size;
} FBEntry;

typedef struct {
    char label[128];
    char exts[FB_MAX_FILTER_EXTS][16];
    int  ext_count;
} FBParsedFilter;

/* ============================================================
   Static state
   ============================================================ */
static struct {
    Ca_Instance   *instance;
    Ca_Window     *window;
    bool           open;
    bool           dirty;
    EdFBMode       mode;
    char           title[128];
    char           current_path[FB_MAX_PATH];

    /* History */
    char           history[FB_MAX_HISTORY][FB_MAX_PATH];
    int            history_count;
    int            history_pos;

    /* Directory entries */
    FBEntry        entries[FB_MAX_ENTRIES];
    int            entry_count;
    int            selected;

    /* Filters */
    FBParsedFilter filters[FB_MAX_FILTERS];
    int            filter_count;
    int            active_filter;

    bool           show_hidden;

    /* Callback */
    EdFBCallback   on_confirm;
    void          *user_data;

    /* ---- Retained widget handles ---- */

    Ca_TextInput  *path_input;
    Ca_Button     *btn_back;
    Ca_Button     *btn_forward;
    Ca_Button     *entry_btns[FB_MAX_ENTRIES];
    Ca_Label      *empty_label;
    Ca_Label      *selected_label;
    Ca_Button     *confirm_btn;
} s_fb;

/* ============================================================
   Path helpers
   ============================================================ */
static void path_normalize(char *path)
{
#ifdef _WIN32
    for (char *p = path; *p; ++p)
        if (*p == '/') *p = '\\';
#else
    (void)path;
#endif
}

static void path_join(char *dst, size_t dst_sz,
                      const char *base, const char *child)
{
    size_t blen = strlen(base);
    if (blen > 0 && base[blen - 1] == FB_PATH_SEP)
        snprintf(dst, dst_sz, "%s%s", base, child);
    else
        snprintf(dst, dst_sz, "%s%c%s", base, FB_PATH_SEP, child);
}

static void path_parent(char *dst, size_t dst_sz, const char *path)
{
    strncpy(dst, path, dst_sz - 1);
    dst[dst_sz - 1] = '\0';
    size_t len = strlen(dst);
    while (len > 1 && dst[len - 1] == FB_PATH_SEP) dst[--len] = '\0';
    char *last = strrchr(dst, FB_PATH_SEP);
    if (last && last != dst)
        *last = '\0';
    else if (last == dst)
        dst[1] = '\0';
}

static void get_home_dir(char *buf, size_t sz)
{
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
    if (home) { strncpy(buf, home, sz - 1); buf[sz - 1] = '\0'; return; }
    strncpy(buf, "C:\\", sz - 1);
#else
    const char *home = getenv("HOME");
    if (home) { strncpy(buf, home, sz - 1); buf[sz - 1] = '\0'; return; }
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir) { strncpy(buf, pw->pw_dir, sz - 1); buf[sz - 1] = '\0'; return; }
    strncpy(buf, "/", sz - 1);
#endif
    buf[sz - 1] = '\0';
}

/* ============================================================
   Filter matching
   ============================================================ */
static void parse_filter(FBParsedFilter *pf, const EdFBFilter *src)
{
    strncpy(pf->label, src->label, sizeof(pf->label) - 1);
    pf->label[sizeof(pf->label) - 1] = '\0';
    pf->ext_count = 0;
    if (!src->extensions) return;

    char tmp[256];
    strncpy(tmp, src->extensions, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char *saveptr = NULL;
    char *tok = strtok_r(tmp, ",", &saveptr);
    while (tok && pf->ext_count < FB_MAX_FILTER_EXTS) {
        while (*tok == ' ') ++tok;
        strncpy(pf->exts[pf->ext_count], tok, 15);
        pf->exts[pf->ext_count][15] = '\0';
        pf->ext_count++;
        tok = strtok_r(NULL, ",", &saveptr);
    }
}

static bool matches_filter(const char *name, const FBParsedFilter *pf)
{
    if (pf->ext_count == 0) return true;
    size_t nlen = strlen(name);
    for (int i = 0; i < pf->ext_count; ++i) {
        size_t elen = strlen(pf->exts[i]);
        if (nlen >= elen && strcmp(name + nlen - elen, pf->exts[i]) == 0)
            return true;
    }
    return false;
}

/* ============================================================
   Sorting
   ============================================================ */
static int entry_compare(const void *a, const void *b)
{
    const FBEntry *ea = (const FBEntry *)a;
    const FBEntry *eb = (const FBEntry *)b;
    if (ea->is_dir != eb->is_dir) return ea->is_dir ? -1 : 1;
    return strcasecmp(ea->name, eb->name);
}

/* ============================================================
   Directory scanning
   ============================================================ */
static void scan_directory(const char *path)
{
    s_fb.entry_count = 0;
    s_fb.selected = -1;

#ifdef _WIN32
    char pattern[FB_MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        if (!s_fb.show_hidden && fd.cFileName[0] == '.') continue;
        if (s_fb.entry_count >= FB_MAX_ENTRIES) break;

        FBEntry *e = &s_fb.entries[s_fb.entry_count];
        strncpy(e->name, fd.cFileName, FB_MAX_NAME - 1);
        e->name[FB_MAX_NAME - 1] = '\0';
        e->is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e->size = ((int64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;

        if (!e->is_dir && s_fb.mode == ED_FB_OPEN_FILE && s_fb.active_filter > 0)
            if (!matches_filter(e->name, &s_fb.filters[s_fb.active_filter - 1]))
                continue;

        s_fb.entry_count++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *dir = opendir(path);
    if (!dir) return;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        if (!s_fb.show_hidden && ent->d_name[0] == '.') continue;
        if (s_fb.entry_count >= FB_MAX_ENTRIES) break;

        FBEntry *e = &s_fb.entries[s_fb.entry_count];
        strncpy(e->name, ent->d_name, FB_MAX_NAME - 1);
        e->name[FB_MAX_NAME - 1] = '\0';

        char fullpath[FB_MAX_PATH];
        path_join(fullpath, sizeof(fullpath), path, ent->d_name);
        struct stat st;
        if (stat(fullpath, &st) == 0) {
            e->is_dir = S_ISDIR(st.st_mode);
            e->size = (int64_t)st.st_size;
        } else {
            e->is_dir = (ent->d_type == DT_DIR);
            e->size = 0;
        }

        if (!e->is_dir && s_fb.mode == ED_FB_OPEN_FILE && s_fb.active_filter > 0)
            if (!matches_filter(e->name, &s_fb.filters[s_fb.active_filter - 1]))
                continue;

        s_fb.entry_count++;
    }
    closedir(dir);
#endif

    qsort(s_fb.entries, (size_t)s_fb.entry_count, sizeof(FBEntry), entry_compare);
    s_fb.dirty = true;
}

/* ============================================================
   Sync retained widgets with current data
   ============================================================ */
static void sync_widgets(void)
{
    if (!s_fb.window) return;

    /* Nav buttons */
    ca_set_disabled(s_fb.btn_back,    s_fb.history_pos <= 0);
    ca_set_disabled(s_fb.btn_forward,
                           s_fb.history_pos + 1 >= s_fb.history_count);

    /* Path input */
    ca_set_text(s_fb.path_input, s_fb.current_path);

    /* Empty label */
    ca_set_hidden(s_fb.empty_label, s_fb.entry_count > 0);

    /* Entry buttons */
    for (int i = 0; i < FB_MAX_ENTRIES; ++i) {
        if (i < s_fb.entry_count) {
            FBEntry *e = &s_fb.entries[i];
            char label[320];
            const char *icon = e->is_dir ? ICON_FOLDER : ICON_FILE;
            snprintf(label, sizeof(label), " %s  %s", icon, e->name);

            ca_set_text(s_fb.entry_btns[i], label);
            ca_set_hidden(s_fb.entry_btns[i], false);

            if (s_fb.selected == i)
                ca_set_background(s_fb.entry_btns[i], CA_THEME_BG_OVERLAY);
            else
                ca_set_background(s_fb.entry_btns[i], 0);
        } else {
            ca_set_hidden(s_fb.entry_btns[i], true);
        }
    }

    /* Selected file label */
    const char *sel_name = "";
    if (s_fb.selected >= 0 && s_fb.selected < s_fb.entry_count
        && !s_fb.entries[s_fb.selected].is_dir) {
        sel_name = s_fb.entries[s_fb.selected].name;
    }
    ca_set_text(s_fb.selected_label, sel_name);

    /* Confirm button */
    bool can_confirm = false;
    if (s_fb.mode == ED_FB_OPEN_FOLDER)
        can_confirm = true;
    else if (s_fb.selected >= 0 && s_fb.selected < s_fb.entry_count
             && !s_fb.entries[s_fb.selected].is_dir)
        can_confirm = true;

    ca_set_disabled(s_fb.confirm_btn, !can_confirm);

    const char *btn_text = (s_fb.mode == ED_FB_OPEN_FOLDER)
                         ? "Select Folder" : "Open";
    ca_set_text(s_fb.confirm_btn, btn_text);

    s_fb.dirty = false;
}

/* ============================================================
   Navigation helpers
   ============================================================ */
static void history_push(const char *path)
{
    s_fb.history_pos++;
    if (s_fb.history_pos >= FB_MAX_HISTORY)
        s_fb.history_pos = FB_MAX_HISTORY - 1;
    s_fb.history_count = s_fb.history_pos + 1;
    strncpy(s_fb.history[s_fb.history_pos], path, FB_MAX_PATH - 1);
    s_fb.history[s_fb.history_pos][FB_MAX_PATH - 1] = '\0';
}

static void navigate_to(const char *path)
{
    strncpy(s_fb.current_path, path, FB_MAX_PATH - 1);
    s_fb.current_path[FB_MAX_PATH - 1] = '\0';
    path_normalize(s_fb.current_path);
    history_push(s_fb.current_path);
    scan_directory(s_fb.current_path);
    if (s_fb.window)
        ca_scroll_to_top(s_fb.window, "fb-file-list");
    sync_widgets();
}

static void navigate_to_child(const char *child_name)
{
    char newpath[FB_MAX_PATH];
    path_join(newpath, sizeof(newpath), s_fb.current_path, child_name);
    navigate_to(newpath);
}

static void navigate_back(void)
{
    if (s_fb.history_pos <= 0) return;
    s_fb.history_pos--;
    strncpy(s_fb.current_path, s_fb.history[s_fb.history_pos], FB_MAX_PATH - 1);
    s_fb.current_path[FB_MAX_PATH - 1] = '\0';
    scan_directory(s_fb.current_path);
    if (s_fb.window)
        ca_scroll_to_top(s_fb.window, "fb-file-list");
    sync_widgets();
}

static void navigate_forward(void)
{
    if (s_fb.history_pos + 1 >= s_fb.history_count) return;
    s_fb.history_pos++;
    strncpy(s_fb.current_path, s_fb.history[s_fb.history_pos], FB_MAX_PATH - 1);
    s_fb.current_path[FB_MAX_PATH - 1] = '\0';
    scan_directory(s_fb.current_path);
    if (s_fb.window)
        ca_scroll_to_top(s_fb.window, "fb-file-list");
    sync_widgets();
}

static void navigate_up(void)
{
    char parent[FB_MAX_PATH];
    path_parent(parent, sizeof(parent), s_fb.current_path);
    if (strcmp(parent, s_fb.current_path) == 0) return;
    navigate_to(parent);
}

static void navigate_home(void)
{
    char home[FB_MAX_PATH];
    get_home_dir(home, sizeof(home));
    navigate_to(home);
}

/* ============================================================
   Confirm / close
   ============================================================ */
static void confirm_selection(void)
{
    char result[FB_MAX_PATH];

    if (s_fb.mode == ED_FB_OPEN_FOLDER) {
        strncpy(result, s_fb.current_path, FB_MAX_PATH - 1);
        result[FB_MAX_PATH - 1] = '\0';
    } else {
        if (s_fb.selected < 0 || s_fb.selected >= s_fb.entry_count) return;
        FBEntry *e = &s_fb.entries[s_fb.selected];
        if (e->is_dir) return;
        path_join(result, sizeof(result), s_fb.current_path, e->name);
    }

    EdFBCallback cb = s_fb.on_confirm;
    void *ud = s_fb.user_data;
    ed_file_browser_close();
    if (cb) cb(result, ud);
}

/* ============================================================
   Button callbacks
   ============================================================ */
static void on_nav_back(Ca_Button *btn, void *data)
    { (void)btn; (void)data; navigate_back(); }
static void on_nav_forward(Ca_Button *btn, void *data)
    { (void)btn; (void)data; navigate_forward(); }
static void on_nav_up(Ca_Button *btn, void *data)
    { (void)btn; (void)data; navigate_up(); }
static void on_nav_home(Ca_Button *btn, void *data)
    { (void)btn; (void)data; navigate_home(); }
static void on_nav_refresh(Ca_Button *btn, void *data)
{
    (void)btn; (void)data;
    scan_directory(s_fb.current_path);
    sync_widgets();
}

static void on_nav_go(Ca_Button *btn, void *data)
{
    (void)btn; (void)data;
    if (!s_fb.path_input) return;
    const char *text = ca_get_text(s_fb.path_input);
    if (text && text[0]) navigate_to(text);
}

static void on_cancel(Ca_Button *btn, void *data)
    { (void)btn; (void)data; ed_file_browser_close(); }
static void on_confirm_btn(Ca_Button *btn, void *data)
    { (void)btn; (void)data; confirm_selection(); }

static void on_entry_click(Ca_Button *btn, void *data)
{
    (void)btn;
    int index = (int)(intptr_t)data;
    if (index < 0 || index >= s_fb.entry_count) return;

    FBEntry *entry = &s_fb.entries[index];

    if (s_fb.selected == index) {
        /* Double-click: navigate into dir or confirm file */
        if (entry->is_dir)
            navigate_to_child(entry->name);
        else if (s_fb.mode == ED_FB_OPEN_FILE)
            confirm_selection();
    } else {
        s_fb.selected = index;
        sync_widgets();
    }
}

/* ============================================================
   Build the window's UI tree (called once per window creation)
   ============================================================ */
static void build_window_ui(void)
{
    Ca_Window *win = s_fb.window;

    ca_ui_begin(win, &(Ca_DivDesc){
        .direction = CA_VERTICAL,
        .style     = "fb-root",
    });
    {
        /* ---- Navigation bar ---- */
        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_HORIZONTAL,
            .style     = "fb-nav-bar",
        });
        s_fb.btn_back = ca_btn(&(Ca_BtnDesc){
            .text     = ICON_ARROW_LEFT,
            .on_click = on_nav_back,
            .style    = "fb-nav-btn",
            .disabled = true,
        });
        s_fb.btn_forward = ca_btn(&(Ca_BtnDesc){
            .text     = ICON_ARROW_RIGHT,
            .on_click = on_nav_forward,
            .style    = "fb-nav-btn",
            .disabled = true,
        });
        ca_btn(&(Ca_BtnDesc){
            .text     = ICON_LEVEL_UP,
            .on_click = on_nav_up,
            .style    = "fb-nav-btn",
        });
        ca_btn(&(Ca_BtnDesc){
            .text     = ICON_HOME,
            .on_click = on_nav_home,
            .style    = "fb-nav-btn",
        });
        ca_btn(&(Ca_BtnDesc){
            .text     = ICON_REFRESH,
            .on_click = on_nav_refresh,
            .style    = "fb-nav-btn",
        });
        s_fb.path_input = ca_input(&(Ca_InputDesc){
            .text        = s_fb.current_path,
            .placeholder = "Enter path...",
            .style       = "fb-path-input",
        });
        ca_btn(&(Ca_BtnDesc){
            .text     = "Go",
            .on_click = on_nav_go,
            .style    = "fb-nav-btn",
        });
        ca_div_end();

        /* ---- Column headers ---- */
        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_HORIZONTAL,
            .style     = "fb-col-header",
        });
        ca_text(&(Ca_TextDesc){ .text = "Name", .style = "fb-col-name" });
        ca_div_end();

        ca_hr(NULL);

        /* ---- File list (scrollable) ---- */
        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_VERTICAL,
            .id        = "fb-file-list",
            .style     = "fb-file-list",
        });

        s_fb.empty_label = ca_text(&(Ca_TextDesc){
            .text   = "  Empty folder",
            .style  = "fb-empty",
            .hidden = true,
        });

        for (int i = 0; i < FB_MAX_ENTRIES; ++i) {
            s_fb.entry_btns[i] = ca_btn(&(Ca_BtnDesc){
                .text       = "",
                .on_click   = on_entry_click,
                .click_data = (void *)(intptr_t)i,
                .style      = "fb-entry",
                .hidden     = true,
            });
        }
        ca_div_end();

        ca_hr(NULL);

        /* ---- Bottom bar ---- */
        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_VERTICAL,
            .style     = "fb-bottom",
        });
        {
            /* Selected file row */
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_HORIZONTAL,
                .style     = "fb-bottom-row",
            });
            ca_text(&(Ca_TextDesc){ .text = "File:", .style = "fb-label" });
            s_fb.selected_label = ca_text(&(Ca_TextDesc){
                .text  = "",
                .style = "fb-selected-name",
            });
            ca_div_end();

            /* Action row */
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_HORIZONTAL,
                .style     = "fb-bottom-row",
            });
            ca_spacer(&(Ca_SpacerDesc){ .style = "fb-spacer-grow" });
            ca_btn(&(Ca_BtnDesc){
                .text     = "Cancel",
                .on_click = on_cancel,
                .style    = "fb-btn",
            });
            s_fb.confirm_btn = ca_btn(&(Ca_BtnDesc){
                .text     = "Open",
                .on_click = on_confirm_btn,
                .style    = "fb-btn fb-btn-primary",
                .disabled = true,
            });
            ca_div_end();
        }
        ca_div_end();
    }
    ca_ui_end();
}

/* ============================================================
   Public API
   ============================================================ */
void ed_file_browser_init(Ca_Instance *instance)
{
    memset(&s_fb, 0, sizeof(s_fb));
    s_fb.instance = instance;
    s_fb.selected = -1;
}

void ed_file_browser_open(const EdFBDesc *desc)
{
    if (!s_fb.instance) return;

    /* Close existing window if already open */
    if (s_fb.open && s_fb.window && ca_window_is_open(s_fb.window)) {
        ca_window_close(s_fb.window);
        s_fb.window = NULL;
    }

    s_fb.open = true;
    s_fb.dirty = true;
    s_fb.mode = desc->mode;
    s_fb.selected = -1;
    s_fb.history_pos = -1;
    s_fb.history_count = 0;
    s_fb.on_confirm = desc->on_confirm;
    s_fb.user_data = desc->user_data;

    /* Title */
    if (desc->title)
        strncpy(s_fb.title, desc->title, sizeof(s_fb.title) - 1);
    else if (desc->mode == ED_FB_OPEN_FOLDER)
        strncpy(s_fb.title, ICON_FOLDER_OPEN "  Select Folder", sizeof(s_fb.title) - 1);
    else
        strncpy(s_fb.title, ICON_FOLDER_OPEN "  Open File", sizeof(s_fb.title) - 1);
    s_fb.title[sizeof(s_fb.title) - 1] = '\0';

    /* Filters */
    s_fb.filter_count = 0;
    s_fb.active_filter = 0;
    if (desc->filters && desc->filter_count > 0) {
        int n = desc->filter_count;
        if (n > FB_MAX_FILTERS) n = FB_MAX_FILTERS;
        for (int i = 0; i < n; ++i)
            parse_filter(&s_fb.filters[i], &desc->filters[i]);
        s_fb.filter_count = n;
        s_fb.active_filter = 1;
    }

    /* Initial path */
    if (desc->initial_path && desc->initial_path[0])
        strncpy(s_fb.current_path, desc->initial_path, FB_MAX_PATH - 1);
    else
        get_home_dir(s_fb.current_path, FB_MAX_PATH);
    s_fb.current_path[FB_MAX_PATH - 1] = '\0';
    path_normalize(s_fb.current_path);

    history_push(s_fb.current_path);
    scan_directory(s_fb.current_path);

    /* Create a new window */
    int win_w = 640, win_h = 480;
    s_fb.window = ca_window_create(s_fb.instance, &(Ca_WindowDesc){
        .title  = s_fb.title,
        .width  = win_w,
        .height = win_h,
    });
    if (!s_fb.window) {
        fprintf(stderr, "[editor] failed to create file browser window\n");
        s_fb.open = false;
        return;
    }

    /* Build UI tree in the new window */
    build_window_ui();

    /* Initial sync */
    sync_widgets();
}

void ed_file_browser_close(void)
{
    if (s_fb.window && ca_window_is_open(s_fb.window))
        ca_window_close(s_fb.window);
    s_fb.open = false;
    s_fb.window = NULL;
}

bool ed_file_browser_is_open(void)
{
    return s_fb.open;
}

void ed_file_browser_update(void)
{
    if (!s_fb.open) return;

    /* Detect if the OS close button (or other external close) destroyed the window */
    if (s_fb.window && !ca_window_is_open(s_fb.window)) {
        s_fb.open = false;
        s_fb.window = NULL;
        return;
    }

    if (s_fb.dirty)
        sync_widgets();
}
