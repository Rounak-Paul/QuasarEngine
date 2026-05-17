#include "ed_layout.h"
#include "ed_hierarchy.h"
#include "ed_inspector.h"
#include "ed_system_panel.h"
#include "ed_import_dialog.h"
#include "editor.h"
#include "ed_thumbnail.h"
#include "ca_theme.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#ifndef _WIN32
    #include <strings.h>
    #include <unistd.h>
    #include <sys/wait.h>
#else
    #define strcasecmp _stricmp
#endif

#define ICON_FOLDER    "\xEF\x81\xBB"   /* fa-folder       U+F07B */
#define ICON_FILE      "\xEF\x85\x9B"   /* fa-file         U+F15B */
#define ICON_IMAGE     "\xEF\x80\xBE"   /* fa-picture-o    U+F03E */
#define ICON_SCRIPT    "\xEF\x84\xA1"   /* fa-code         U+F121 */
#define ICON_MATERIAL  "\xEF\x81\x82"   /* fa-adjust       U+F042 */
#define ICON_UP        "\xEF\x81\xB7"   /* fa-chevron-up   U+F077 */
#define ICON_REFRESH   "\xEF\x80\xA1"   /* fa-refresh      U+F021 */
#define ICON_VIEW_LIST "\xEF\x80\xBA"   /* fa-list         U+F03A */
#define ICON_VIEW_GRID "\xEF\x80\x8A"   /* fa-th           U+F00A */
#define ICON_VIEW_THUMB "\xEF\x80\x89"  /* fa-th-large     U+F009 */

#define ICON_LOG_DEBUG "\xEF\x86\x88"   /* fa-bug                  U+F188 */
#define ICON_LOG_TRACE "\xEF\x80\xBA"   /* fa-list                 U+F03A */
#define ICON_LOG_INFO  "\xEF\x81\x9A"   /* fa-info-circle          U+F05A */
#define ICON_LOG_WARN  "\xEF\x81\xB1"   /* fa-exclamation-triangle U+F071 */
#define ICON_LOG_ERROR "\xEF\x81\x97"   /* fa-times-circle         U+F057 */
#define ICON_LOG_FATAL "\xEF\x81\xAA"   /* fa-exclamation-circle   U+F06A */
#define ICON_FPS       "\xEF\x83\xA4"   /* fa-tachometer           U+F0E4 */
#define ICON_PROJECT   "\xEF\x81\xBC"   /* fa-folder-open          U+F07C */
#define ICON_RENDER    "\xEF\x84\x88"   /* fa-desktop              U+F108 */
#define ICON_LOCK      "\xEF\x80\xA3"   /* fa-lock                 U+F023 */
#define ICON_UNLOCK    "\xEF\x82\x9C"   /* fa-unlock               U+F09C */
#define ICON_COPY      "\xEF\x83\x85"   /* fa-copy                 U+F0C5 */
#define ICON_SEARCH    "\xEF\x80\x82"   /* fa-search               U+F002 */

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #define ED_PATH_SEP '\\'
#else
  #include <dirent.h>
  #include <sys/stat.h>
  #define ED_PATH_SEP '/'
#endif

/* ---- Console ---- */
#define CONSOLE_MAX_LINES 100
#define CONSOLE_LINE_MAX  1280
#define ASSETS_MAX_ENTRIES 220
#define ASSETS_MAX_PATH    1024
#define ASSETS_THUMB_SIZE  72
#define ASSETS_TREE_CTX_MAX 512

typedef struct ConsoleLineCtx {
    uint32_t entry_index;
    bool     has_entry;
} ConsoleLineCtx;

static Ca_Window *s_console_window;
static Ca_Label  *s_console_lines[CONSOLE_MAX_LINES];
static ConsoleLineCtx s_console_line_ctx[CONSOLE_MAX_LINES];
static Ca_Label  *s_console_count_icons[QS_LOG_LEVEL_COUNT];
static Ca_Label  *s_console_count_labels[QS_LOG_LEVEL_COUNT];
static Ca_Label  *s_console_total_label;
static Ca_TextInput *s_console_search_input;
static Ca_Button *s_console_copy_btn;
static Ca_Button *s_console_lock_btn;
static Ca_Button *s_console_clear_btn;
static uint32_t   s_prev_log_count;
static bool        s_needs_scroll;
static bool        s_console_autoscroll_locked;
static char        s_console_search[128];
static int         s_bottom_tab_active;
static int         s_right_tab_active;
static Ca_Div     *s_inspector_panel;
static Ca_Div     *s_system_panel_div;

static Ca_Label   *s_status_fps_icon;
static Ca_Label   *s_status_project_name;
static Ca_Label   *s_status_scene_count;
static Ca_Label   *s_status_render_count;
static Ca_Label   *s_status_log_icon;
static Ca_Label   *s_status_log_count;
static Ca_Tooltip *s_status_fps_tip;
static Ca_Tooltip *s_status_project_tip;
static Ca_Tooltip *s_status_scene_tip;
static Ca_Tooltip *s_status_render_tip;
static Ca_Tooltip *s_status_log_tip;
static float       s_status_fps;
static bool        s_status_has_fps;

typedef struct AssetEntry {
    char rel_path[ASSETS_MAX_PATH];
    bool is_dir;
    uint64_t size_bytes;
    int64_t mtime_sec;
} AssetEntry;

static char      s_assets_root[ASSETS_MAX_PATH];
static char      s_assets_folder[ASSETS_MAX_PATH];
static AssetEntry s_assets_entries[ASSETS_MAX_ENTRIES];
static int       s_assets_entry_count;
static int       s_assets_selected;
static bool      s_assets_needs_scan;
static bool      s_assets_dirty;
static bool      s_assets_truncated;

typedef enum AssetsFilter {
    ASSETS_FILTER_ALL = 0,
    ASSETS_FILTER_MODEL,
    ASSETS_FILTER_TEXTURE,
    ASSETS_FILTER_MATERIAL,
    ASSETS_FILTER_MESH,
    ASSETS_FILTER_SCENE,
    ASSETS_FILTER_SCRIPT,
} AssetsFilter;

static Ca_Div    *s_console_panel;
static Ca_Div    *s_assets_panel;
static Ca_Label  *s_assets_path_label;
static Ca_Div    *s_assets_tree_div;
static Ca_Div    *s_assets_scroll_div;
static Ca_TextInput  *s_assets_search_input;
static int        s_assets_visible_indices[ASSETS_MAX_ENTRIES];
static int        s_assets_visible_count;
static AssetsFilter s_assets_filter;
static char       s_assets_search[128];
static int        s_assets_last_click_index;
static uint64_t   s_assets_last_click_time_ms;

/* Pointers to toolbar buttons so we can update active-state colours every frame */
static Ca_Button *s_assets_up_btn;
static Ca_Select *s_assets_filter_select;  /* filter dropdown         */
static Ca_Button *s_assets_view_btns[3];   /* indexed by AssetsViewMode */

typedef enum AssetsViewMode {
    ASSETS_VIEW_LIST = 0,
    ASSETS_VIEW_GRID,
    ASSETS_VIEW_THUMBNAIL,
} AssetsViewMode;

static AssetsViewMode s_assets_view_mode;

typedef struct AssetsTreeClickCtx {
    char abs_path[ASSETS_MAX_PATH];
} AssetsTreeClickCtx;

static AssetsTreeClickCtx s_assets_tree_click_ctx[ASSETS_TREE_CTX_MAX];
static int                s_assets_tree_click_count;

static void on_assets_row_click(Ca_Button *btn, void *user_data);
static void on_console_lock_click(Ca_Button *btn, void *user_data);
static void on_console_clear_click(Ca_Button *btn, void *user_data);
static void on_console_copy_click(Ca_Button *btn, void *user_data);
static void on_console_ctx_menu(int item_index, void *user_data);
static void on_assets_ctx_menu(int item_index, void *user_data);
static const char *s_console_ctx_items[] = { "Copy Message", "Copy All", "-", "Clear" };
static const char *s_assets_ctx_items[] = { "Import", "Rename", "Delete", "Reveal" };
static void assets_entry_abs_path(int idx, char *out, size_t out_size);
static void on_assets_tree_toggle(Ca_TreeNode *tn, void *user_data);
static void on_assets_filter_select(Ca_Select *sel, void *user_data);

static bool path_is_prefix(const char *prefix, const char *path)
{
    size_t n = strlen(prefix);
    return n > 0 && strncmp(prefix, path, n) == 0;
}

static void path_join(char *dst, size_t dst_size, const char *a, const char *b)
{
    size_t alen = strlen(a);
    if (alen > 0 && a[alen - 1] == ED_PATH_SEP)
        snprintf(dst, dst_size, "%s%s", a, b);
    else
        snprintf(dst, dst_size, "%s%c%s", a, ED_PATH_SEP, b);
}

static void path_parent(char *dst, size_t dst_size, const char *src)
{
    snprintf(dst, dst_size, "%s", src ? src : "");
    size_t n = strlen(dst);
    while (n > 1 && dst[n - 1] == ED_PATH_SEP) dst[--n] = '\0';
    char *p = strrchr(dst, ED_PATH_SEP);
    if (!p) return;
    if (p == dst) dst[1] = '\0';
    else *p = '\0';
}

static int asset_entry_cmp(const void *a, const void *b)
{
    const AssetEntry *ea = (const AssetEntry *)a;
    const AssetEntry *eb = (const AssetEntry *)b;
    if (ea->is_dir != eb->is_dir) return ea->is_dir ? -1 : 1;
    return strcasecmp(ea->rel_path, eb->rel_path);
}

static bool has_ext_ci(const char *path, const char *ext)
{
    size_t n = strlen(path), m = strlen(ext);
    if (n < m) return false;
    return strcasecmp(path + n - m, ext) == 0;
}

static bool assets_matches_filter(const AssetEntry *e)
{
    if (!e) return false;
    if (e->is_dir || s_assets_filter == ASSETS_FILTER_ALL) return true;

    switch (s_assets_filter) {
    case ASSETS_FILTER_MODEL:
        return has_ext_ci(e->rel_path, ".gltf") || has_ext_ci(e->rel_path, ".glb") || has_ext_ci(e->rel_path, ".fbx") || has_ext_ci(e->rel_path, ".obj");
    case ASSETS_FILTER_TEXTURE:
        return has_ext_ci(e->rel_path, ".qstex") || has_ext_ci(e->rel_path, ".png") || has_ext_ci(e->rel_path, ".jpg") || has_ext_ci(e->rel_path, ".jpeg") || has_ext_ci(e->rel_path, ".ktx");
    case ASSETS_FILTER_MATERIAL:
        return has_ext_ci(e->rel_path, ".qsmat");
    case ASSETS_FILTER_MESH:
        return has_ext_ci(e->rel_path, ".qsmesh");
    case ASSETS_FILTER_SCENE:
        return has_ext_ci(e->rel_path, ".qscene") || has_ext_ci(e->rel_path, ".qproto");
    case ASSETS_FILTER_SCRIPT:
        return has_ext_ci(e->rel_path, ".lua") || has_ext_ci(e->rel_path, ".py") || has_ext_ci(e->rel_path, ".js") || has_ext_ci(e->rel_path, ".ts") || has_ext_ci(e->rel_path, ".cs");
    default:
        return true;
    }
}

/* Returns a font-awesome icon string for a given asset entry. */
static const char *entry_icon(const AssetEntry *e)
{
    if (e->is_dir) return ICON_FOLDER;
    const char *p = e->rel_path;
    if (has_ext_ci(p, ".qstex") || has_ext_ci(p, ".png") ||
        has_ext_ci(p, ".jpg")   || has_ext_ci(p, ".jpeg") ||
        has_ext_ci(p, ".ktx"))
        return ICON_IMAGE;
    if (has_ext_ci(p, ".qsmesh") || has_ext_ci(p, ".gltf") ||
        has_ext_ci(p, ".glb")    || has_ext_ci(p, ".fbx")  ||
        has_ext_ci(p, ".obj"))
        return ICON_MESH;
    if (has_ext_ci(p, ".qscene") || has_ext_ci(p, ".qproto"))
        return ICON_SCENE;
    if (has_ext_ci(p, ".lua") || has_ext_ci(p, ".py") ||
        has_ext_ci(p, ".js")  || has_ext_ci(p, ".ts") ||
        has_ext_ci(p, ".cs"))
        return ICON_SCRIPT;
    if (has_ext_ci(p, ".qsmat"))
        return ICON_MATERIAL;
    return ICON_FILE;
}

/* Returns the accent colour for a given asset entry. */
static uint32_t entry_color(const AssetEntry *e)
{
    if (e->is_dir) return CA_THEME_ACCENT;
    const char *p = e->rel_path;
    if (has_ext_ci(p, ".qstex") || has_ext_ci(p, ".png") ||
        has_ext_ci(p, ".jpg")   || has_ext_ci(p, ".jpeg") ||
        has_ext_ci(p, ".ktx"))
        return CA_THEME_SUCCESS;
    if (has_ext_ci(p, ".qsmesh") || has_ext_ci(p, ".gltf") ||
        has_ext_ci(p, ".glb")    || has_ext_ci(p, ".fbx")  ||
        has_ext_ci(p, ".obj"))
        return CA_THEME_WARNING;
    if (has_ext_ci(p, ".qscene") || has_ext_ci(p, ".qproto"))
        return CA_THEME_ACCENT;
    if (has_ext_ci(p, ".lua") || has_ext_ci(p, ".py") ||
        has_ext_ci(p, ".js")  || has_ext_ci(p, ".ts") ||
        has_ext_ci(p, ".cs"))
        return CA_THEME_WARNING;
    if (has_ext_ci(p, ".qsmat"))
        return CA_THEME_DANGER;
    return CA_THEME_TEXT_MUTED;
}

static bool contains_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle || !needle[0]) return true;
    size_t nh = strlen(haystack);
    size_t nn = strlen(needle);
    if (nn > nh) return false;
    for (size_t i = 0; i + nn <= nh; ++i) {
        size_t j = 0;
        for (; j < nn; ++j) {
            char a = haystack[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
        }
        if (j == nn) return true;
    }
    return false;
}

static bool assets_matches_search(const AssetEntry *e)
{
    if (!e) return false;
    if (!s_assets_search[0]) return true;
    return contains_ci(e->rel_path, s_assets_search);
}

static void format_size(uint64_t bytes, char *out, size_t out_size)
{
    const char *units[] = { "B", "KB", "MB", "GB", "TB" };
    double v = (double)bytes;
    int ui = 0;
    while (v >= 1024.0 && ui < 4) {
        v /= 1024.0;
        ui++;
    }
    if (ui == 0) snprintf(out, out_size, "%llu %s", (unsigned long long)bytes, units[ui]);
    else         snprintf(out, out_size, "%.1f %s", v, units[ui]);
}

static void format_mtime(int64_t mtime_sec, char *out, size_t out_size)
{
    if (mtime_sec <= 0) {
        snprintf(out, out_size, "-");
        return;
    }

    time_t t = (time_t)mtime_sec;
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    strftime(out, out_size, "%Y-%m-%d %H:%M", &tmv);
}

static void assets_entry_abs_path(int idx, char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (idx < 0 || idx >= s_assets_entry_count) return;
    path_join(out, out_size, s_assets_folder, s_assets_entries[idx].rel_path);
}

static void assets_scan_folder(void)
{
    s_assets_entry_count = 0;
    s_assets_selected = -1;
    s_assets_truncated = false;

#ifdef _WIN32
    char pattern[ASSETS_MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", s_assets_folder);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        s_assets_dirty = true;
        return;
    }
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        if (s_assets_entry_count >= ASSETS_MAX_ENTRIES) {
            s_assets_truncated = true;
            break;
        }

        AssetEntry *e = &s_assets_entries[s_assets_entry_count++];
        snprintf(e->rel_path, sizeof(e->rel_path), "%s", fd.cFileName);
        e->is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e->size_bytes = e->is_dir ? 0u : (uint64_t)(((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow);
        ULARGE_INTEGER ull;
        ull.LowPart = fd.ftLastWriteTime.dwLowDateTime;
        ull.HighPart = fd.ftLastWriteTime.dwHighDateTime;
        e->mtime_sec = (int64_t)((ull.QuadPart / 10000000ULL) - 11644473600ULL);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(s_assets_folder);
    if (!d) {
        s_assets_dirty = true;
        return;
    }

    struct dirent *ent = NULL;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        if (s_assets_entry_count >= ASSETS_MAX_ENTRIES) {
            s_assets_truncated = true;
            break;
        }

        AssetEntry *e = &s_assets_entries[s_assets_entry_count++];
        snprintf(e->rel_path, sizeof(e->rel_path), "%s", ent->d_name);

        char full[ASSETS_MAX_PATH];
        path_join(full, sizeof(full), s_assets_folder, ent->d_name);
        struct stat st;
        if (stat(full, &st) == 0) {
            e->is_dir = S_ISDIR(st.st_mode);
            e->size_bytes = e->is_dir ? 0u : (uint64_t)st.st_size;
            e->mtime_sec = (int64_t)st.st_mtime;
        } else {
            e->is_dir = false;
            e->size_bytes = 0u;
            e->mtime_sec = 0;
        }
    }

    closedir(d);
#endif

    qsort(s_assets_entries, (size_t)s_assets_entry_count, sizeof(s_assets_entries[0]),
          asset_entry_cmp);
    s_assets_dirty = true;
}

static void assets_set_path_label(void)
{
    if (!s_assets_path_label) return;
    if (s_assets_root[0] == '\0' || s_assets_folder[0] == '\0') {
        ca_set_text(s_assets_path_label, "No project open");
        return;
    }

    const char *rel = s_assets_folder;
    size_t root_len = strlen(s_assets_root);
    if (path_is_prefix(s_assets_root, s_assets_folder) && strlen(s_assets_folder) >= root_len)
        rel = s_assets_folder + root_len;

    while (*rel == ED_PATH_SEP) rel++;

    char buf[ASSETS_MAX_PATH + 16];
    if (*rel == '\0')
        ca_set_text(s_assets_path_label, "assets");
    else {
        snprintf(buf, sizeof(buf), "assets/%s", rel);
        ca_set_text(s_assets_path_label, buf);
    }
}

/* FNV-1a hash of a path → short stable ID that always fits in CA_NODE_ID_MAX. */
static void path_stable_id(const char *path, char *out, size_t out_size)
{
    uint32_t h = 2166136261u;
    for (const char *p = path; *p; p++) {
        h ^= (uint8_t)*p;
        h *= 16777619u;
    }
    snprintf(out, out_size, "atr-%08x", h);
}

static int child_path_cmp(const void *a, const void *b)
{
#ifdef _WIN32
    return _stricmp(*(const char *const *)a, *(const char *const *)b);
#else
    return strcasecmp(*(const char *const *)a, *(const char *const *)b);
#endif
}

/* Builds one tree node for abs_dir and recurses into subdirectories.
   Uses a single directory scan (one opendir/FindFirstFile call) to both
   detect has_children and collect child paths, avoiding the previous
   two-pass approach that opened each directory twice. */
static void assets_build_tree_node(const char *abs_dir)
{
    if (!abs_dir || !abs_dir[0]) return;

    /* Collect child subdirectory absolute paths in one scan. */
    char  **child_paths = NULL;
    int     child_count = 0;
    int     child_cap   = 0;

#ifdef _WIN32
    {
        char pattern[ASSETS_MAX_PATH];
        snprintf(pattern, sizeof(pattern), "%s\\*", abs_dir);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
                    continue;
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    continue;
                if (child_count == child_cap) {
                    child_cap = child_cap ? child_cap * 2 : 8;
                    char **tmp = (char **)qs_realloc(child_paths,
                                                  (size_t)child_cap * sizeof(char *), QS_MEM_EDITOR);
                    if (!tmp) break;
                    child_paths = tmp;
                }
                child_paths[child_count] = (char *)qs_malloc(ASSETS_MAX_PATH, QS_MEM_EDITOR);
                if (child_paths[child_count])
                    path_join(child_paths[child_count++], ASSETS_MAX_PATH,
                              abs_dir, fd.cFileName);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
#else
    {
        DIR *d = opendir(abs_dir);
        if (d) {
            struct dirent *ent;
            while ((ent = readdir(d)) != NULL) {
                if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                    continue;
                char full[ASSETS_MAX_PATH];
                path_join(full, sizeof(full), abs_dir, ent->d_name);
                struct stat st;
                if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode))
                    continue;
                if (child_count == child_cap) {
                    child_cap = child_cap ? child_cap * 2 : 8;
                    char **tmp = (char **)qs_realloc(child_paths,
                                                  (size_t)child_cap * sizeof(char *), QS_MEM_EDITOR);
                    if (!tmp) break;
                    child_paths = tmp;
                }
                child_paths[child_count] = (char *)qs_malloc(ASSETS_MAX_PATH, QS_MEM_EDITOR);
                if (child_paths[child_count])
                    snprintf(child_paths[child_count++], ASSETS_MAX_PATH, "%s", full);
            }
            closedir(d);
        }
    }
#endif

    /* Sort children for deterministic display order. */
    if (child_count > 1)
        qsort(child_paths, (size_t)child_count, sizeof(char *), child_path_cmp);

    const bool has_children = (child_count > 0);
    const bool is_root      = (strcmp(abs_dir, s_assets_root) == 0);
    const bool selected     = (strcmp(abs_dir, s_assets_folder) == 0);

    const char *label = abs_dir;
    if (is_root) {
        label = "assets";
    } else {
        const char *p = strrchr(abs_dir, ED_PATH_SEP);
        if (p && p[1]) label = p + 1;
    }

    char node_id[16];
    path_stable_id(abs_dir, node_id, sizeof(node_id));

    AssetsTreeClickCtx *ctx = NULL;
    if (s_assets_tree_click_count < ASSETS_TREE_CTX_MAX) {
        ctx = &s_assets_tree_click_ctx[s_assets_tree_click_count++];
        snprintf(ctx->abs_path, sizeof(ctx->abs_path), "%s", abs_dir);
    }

    Ca_TreeNode *tn = ca_tree_node_begin(&(Ca_TreeNodeDesc){
        .text        = label,
        .id          = node_id,
        .style       = selected ? "assets-tree-node assets-tree-node-selected"
                                : "assets-tree-node",
        .icon        = ICON_FOLDER,
        .icon_color  = selected ? CA_THEME_TEXT_BRIGHT : CA_THEME_ACCENT,
        .is_leaf     = !has_children,
        .expanded    = is_root,
        .on_toggle   = on_assets_tree_toggle,
        .toggle_data = ctx,
    });

    for (int i = 0; i < child_count; i++) {
        assets_build_tree_node(child_paths[i]);
        qs_free(child_paths[i]);
    }
    qs_free(child_paths);

    (void)tn;
    ca_tree_node_end();
}

static void on_assets_tree_toggle(Ca_TreeNode *tn, void *user_data)
{
    (void)tn;
    AssetsTreeClickCtx *ctx = (AssetsTreeClickCtx *)user_data;
    if (!ctx || !ctx->abs_path[0]) return;
    if (strcmp(s_assets_folder, ctx->abs_path) == 0) return;
    snprintf(s_assets_folder, sizeof(s_assets_folder), "%s", ctx->abs_path);
    assets_scan_folder();
}

static void assets_sync_widgets(void)
{
    /* Reset per-frame mesh GPU budget before any thumbnail lookups. */
    ed_thumbnail_begin_frame();

    if (s_assets_search_input) {
        const char *txt = ca_get_text(s_assets_search_input);
        if (!txt) txt = "";
        if (strncmp(s_assets_search, txt, sizeof(s_assets_search) - 1) != 0) {
            snprintf(s_assets_search, sizeof(s_assets_search), "%s", txt);
            s_assets_dirty = true;
        }
    }

    /* Update toolbar button active-state colours every frame */
    if (s_assets_up_btn) {
        ca_set_color(s_assets_up_btn,
            (strcmp(s_assets_folder, s_assets_root) == 0)
                ? CA_THEME_TEXT_DIM : CA_THEME_TEXT_MUTED);
    }
    for (int vi = 0; vi < 3; vi++) {
        if (s_assets_view_btns[vi])
            ca_set_color(s_assets_view_btns[vi],
                (s_assets_view_mode == (AssetsViewMode)vi)
                    ? CA_THEME_ACCENT : CA_THEME_TEXT_DIM);
    }

    /* While thumbnails are loading or waiting for GPU budget, force a
       reconcile on each frame so newly-ready images appear immediately
       without requiring any user interaction to trigger a redraw. */
    if (!s_assets_dirty && s_assets_view_mode == ASSETS_VIEW_THUMBNAIL)
        s_assets_dirty = ed_thumbnail_any_ready();

    if (!s_assets_dirty) return;

    assets_set_path_label();

    s_assets_visible_count = 0;
    for (int i = 0; i < s_assets_entry_count && s_assets_visible_count < ASSETS_MAX_ENTRIES; ++i) {
        if (!assets_matches_filter(&s_assets_entries[i])) continue;
        if (!assets_matches_search(&s_assets_entries[i])) continue;
        s_assets_visible_indices[s_assets_visible_count++] = i;
    }

    if (s_assets_tree_div) {
        s_assets_tree_click_count = 0;
        ca_reconcile_begin(s_assets_tree_div);
        if (s_assets_root[0]) {
            ca_tree_begin(&(Ca_DivDesc){
                .direction = CA_VERTICAL,
                .style = "assets-tree",
            });
            assets_build_tree_node(s_assets_root);
            ca_tree_end();
        } else {
            ca_text(&(Ca_TextDesc){ .text = "No folders", .style = "assets-empty" });
        }
        ca_div_end();
    }

    if (s_assets_scroll_div) {
        if (s_assets_view_mode == ASSETS_VIEW_LIST)
            ca_set_style(s_assets_scroll_div, "assets-scroll assets-scroll-list");
        else
            ca_set_style(s_assets_scroll_div, "assets-scroll assets-scroll-grid");

        ca_div_clear(s_assets_scroll_div);

        if (s_assets_visible_count == 0) {
            ca_text(&(Ca_TextDesc){
                .text = "No assets found in this folder.",
                .style = "assets-empty",
            });
        } else {
            for (int row = 0; row < s_assets_visible_count; ++row) {
                int idx = s_assets_visible_indices[row];
                const AssetEntry *e = &s_assets_entries[idx];

                const char *icon  = entry_icon(e);
                uint32_t    color = entry_color(e);

                char icon_name[ASSETS_MAX_PATH + 16];
                snprintf(icon_name, sizeof(icon_name), "%s %s", icon, e->rel_path);

                const char *style = (s_assets_view_mode == ASSETS_VIEW_LIST)
                    ? "assets-entry assets-entry-list"
                    : (s_assets_view_mode == ASSETS_VIEW_GRID)
                        ? "assets-entry assets-entry-grid"
                        : "assets-entry assets-entry-thumb";

                Ca_Button *btn = ca_btn_begin(&(Ca_BtnDesc){
                    .text       = (s_assets_view_mode == ASSETS_VIEW_GRID) ? icon_name : "",
                    .style      = style,
                    .on_click   = on_assets_row_click,
                    .click_data = &s_assets_visible_indices[row],
                });

                ca_set_color(btn, color);
                ca_set_background(btn, (s_assets_selected == idx) ? CA_THEME_BG_OVERLAY : 0);

                if (s_assets_view_mode == ASSETS_VIEW_LIST) {
                    char size_txt[32], mt_txt[32], meta[96];
                    format_size(e->size_bytes, size_txt, sizeof(size_txt));
                    format_mtime(e->mtime_sec, mt_txt, sizeof(mt_txt));
                    snprintf(meta, sizeof(meta), "%s  %s", size_txt, mt_txt);
                    Ca_Label *name_widget = ca_text(&(Ca_TextDesc){
                        .text = icon_name,
                        .style = "assets-entry-name",
                    });
                    ca_set_color(name_widget, color);
                    ca_text(&(Ca_TextDesc){
                        .text = meta,
                        .style = "assets-entry-meta",
                    });
                } else if (s_assets_view_mode == ASSETS_VIEW_THUMBNAIL) {
                    char abs_buf[ASSETS_MAX_PATH];
                    assets_entry_abs_path(idx, abs_buf, sizeof(abs_buf));
                    Ca_Image *thumb = ed_thumbnail_get(abs_buf, e->mtime_sec);
                    if (thumb) {
                        ca_image(&(Ca_ImageDesc){
                            .image = thumb,
                            .width = (float)ASSETS_THUMB_SIZE,
                            .height = (float)ASSETS_THUMB_SIZE,
                            .style = "assets-thumb-image",
                        });
                    } else {
                        Ca_Label *icon_widget = ca_text(&(Ca_TextDesc){
                            .text = icon,
                            .style = "assets-thumb-icon",
                        });
                        ca_set_color(icon_widget, color);
                    }
                    ca_text(&(Ca_TextDesc){
                        .text = e->rel_path,
                        .style = "assets-thumb-name",
                    });
                }

                ca_context_menu(&(Ca_CtxMenuDesc){
                    .items = s_assets_ctx_items,
                    .item_count = 4,
                    .on_select = on_assets_ctx_menu,
                    .select_data = &s_assets_visible_indices[row],
                });
                ca_btn_end();
            }
        }

        if (s_assets_truncated) {
            ca_text(&(Ca_TextDesc){
                .text = "Showing first 220 entries.",
                .style = "assets-empty",
            });
        }

        ca_div_end();
    }

    s_assets_dirty = false;
}

static void assets_import_entry(int idx)
{
    char abs_path[ASSETS_MAX_PATH];
    assets_entry_abs_path(idx, abs_path, sizeof(abs_path));
    if (!abs_path[0]) return;
    ed_import_dialog_open(abs_path);
}

static void on_assets_row_click(Ca_Button *btn, void *user_data)
{
    (void)btn;
    int idx = *(int *)user_data;
    if (idx < 0 || idx >= s_assets_entry_count) return;

    /* Two clicks on the same item within 400 ms = double-click. */
#ifdef _WIN32
    uint64_t now_ms = (uint64_t)GetTickCount64();
#else
    struct timespec _ts;
    clock_gettime(CLOCK_MONOTONIC, &_ts);
    uint64_t now_ms = (uint64_t)_ts.tv_sec * 1000u
                    + (uint64_t)_ts.tv_nsec / 1000000u;
#endif
    bool second_click_same = (s_assets_selected == idx
                              && s_assets_last_click_index == idx
                              && (now_ms - s_assets_last_click_time_ms) < 400u);
    s_assets_selected = idx;
    s_assets_last_click_index = idx;
    s_assets_last_click_time_ms = now_ms;

    if (second_click_same) {
        if (s_assets_entries[idx].is_dir) {
            char next[ASSETS_MAX_PATH];
            path_join(next, sizeof(next), s_assets_folder, s_assets_entries[idx].rel_path);
            snprintf(s_assets_folder, sizeof(s_assets_folder), "%s", next);
            assets_scan_folder();
        } else {
            assets_import_entry(idx);
        }
    } else {
        s_assets_dirty = true;
    }
}

static void on_assets_up_click(Ca_Button *btn, void *user_data)
{
    (void)btn;
    (void)user_data;
    if (s_assets_root[0] == '\0' || s_assets_folder[0] == '\0') return;
    if (strcmp(s_assets_folder, s_assets_root) == 0) return;

    char parent[ASSETS_MAX_PATH];
    path_parent(parent, sizeof(parent), s_assets_folder);
    if (!path_is_prefix(s_assets_root, parent))
        snprintf(parent, sizeof(parent), "%s", s_assets_root);
    snprintf(s_assets_folder, sizeof(s_assets_folder), "%s", parent);
    assets_scan_folder();
}

static void on_assets_refresh_click(Ca_Button *btn, void *user_data)
{
    (void)btn;
    (void)user_data;
    assets_scan_folder();
}

static void on_assets_filter_select(Ca_Select *sel, void *user_data)
{
    (void)user_data;
    s_assets_filter = (AssetsFilter)ca_select_get(sel);
    s_assets_dirty = true;
}

static void on_assets_view_mode_click(Ca_Button *btn, void *user_data)
{
    (void)btn;
    s_assets_view_mode = (AssetsViewMode)(intptr_t)user_data;
    s_assets_dirty = true;
}

static void on_assets_ctx_menu(int item_index, void *user_data)
{
    int idx = *(int *)user_data;
    if (idx < 0 || idx >= s_assets_entry_count) return;

    char abs_path[ASSETS_MAX_PATH];
    assets_entry_abs_path(idx, abs_path, sizeof(abs_path));
    if (!abs_path[0]) return;

    switch (item_index) {
    case 0: /* Import */
        if (!s_assets_entries[idx].is_dir)
            assets_import_entry(idx);
        break;
    case 1: { /* Rename */
        char renamed[ASSETS_MAX_PATH];
        const char *name = s_assets_entries[idx].rel_path;
        const char *dot = strrchr(name, '.');
        if (dot) {
            char stem[ASSETS_MAX_PATH];
            size_t n = (size_t)(dot - name);
            if (n >= sizeof(stem)) n = sizeof(stem) - 1;
            memcpy(stem, name, n);
            stem[n] = '\0';
            snprintf(renamed, sizeof(renamed), "%s%c%s_renamed%s", s_assets_folder, ED_PATH_SEP, stem, dot);
        } else {
            snprintf(renamed, sizeof(renamed), "%s%c%s_renamed", s_assets_folder, ED_PATH_SEP, name);
        }
        (void)rename(abs_path, renamed);
        assets_scan_folder();
        break;
    }
    case 2: /* Delete */
        if (s_assets_entries[idx].is_dir) {
#ifdef _WIN32
            if (!RemoveDirectoryA(abs_path))
                QS_LOG_WARN("Delete: cannot remove directory (may not be empty): %s", abs_path);
#else
            if (rmdir(abs_path) != 0)
                QS_LOG_WARN("Delete: cannot remove directory (may not be empty): %s", abs_path);
#endif
        } else {
            if (remove(abs_path) != 0)
                QS_LOG_WARN("Delete: cannot remove file: %s", abs_path);
        }
        assets_scan_folder();
        break;
    case 3: { /* Reveal — use OS API directly; no shell string interpolation */
#ifdef _WIN32
        {
            char parent_dir[ASSETS_MAX_PATH];
            path_parent(parent_dir, sizeof(parent_dir), abs_path);
            ShellExecuteA(NULL, "explore", parent_dir, NULL, NULL, SW_SHOWDEFAULT);
        }
#else
        {
            pid_t _pid = fork();
            if (_pid == 0) {
                const char *_args[] = { "open", "-R", abs_path, NULL };
                execvp("open", (char *const *)_args);
                _exit(1);
            } else if (_pid > 0) {
                (void)waitpid(_pid, NULL, 0);
            }
        }
#endif
        break;
    }
    default:
        break;
    }
}

static void on_right_tab_change(Ca_TabBar *tabs, void *user_data)
{
    (void)user_data;
    s_right_tab_active = ca_tabs_active(tabs);
    if (s_inspector_panel)
        ca_set_hidden(s_inspector_panel, s_right_tab_active != 0);
    if (s_system_panel_div)
        ca_set_hidden(s_system_panel_div, s_right_tab_active != 1);
}

static void on_bottom_tab_change(Ca_TabBar *tabs, void *user_data)
{
    (void)user_data;
    s_bottom_tab_active = ca_tabs_active(tabs);
    if (s_console_panel)
        ca_set_hidden(s_console_panel, s_bottom_tab_active != 0);
    if (s_assets_panel)
        ca_set_hidden(s_assets_panel, s_bottom_tab_active != 1);

    if (s_bottom_tab_active == 1)
        s_assets_needs_scan = true;
    else if (!s_console_autoscroll_locked)
        s_needs_scroll = true;
}

static void assets_init(Editor *editor)
{
    s_assets_root[0] = '\0';
    s_assets_folder[0] = '\0';
    s_assets_entry_count = 0;
    s_assets_selected = -1;
    s_assets_needs_scan = false;
    s_assets_dirty = true;
    s_assets_truncated = false;
    s_assets_filter = ASSETS_FILTER_ALL;
    s_assets_view_mode = ASSETS_VIEW_LIST;
    s_assets_search[0] = '\0';
    s_assets_last_click_index = -1;

    Qs_Project *project = editor_project(editor);
    if (!project) return;

    /* Invalidate stale thumbnails from any previous project. */
    ed_thumbnail_flush();

    const char *proj_path = qs_project_path(project);

    /* Root the browser at project/assets/ */
    char root[ASSETS_MAX_PATH];
    snprintf(root, sizeof(root), "%s%cassets", proj_path, ED_PATH_SEP);

#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(root);
    bool exists = (attrs != INVALID_FILE_ATTRIBUTES) && ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
#else
    struct stat st;
    bool exists = (stat(root, &st) == 0) && S_ISDIR(st.st_mode);
#endif

    if (!exists) {
        /* Fallback: try capitalised variant */
        snprintf(root, sizeof(root), "%s%cAssets", proj_path, ED_PATH_SEP);
#ifdef _WIN32
        attrs = GetFileAttributesA(root);
        exists = (attrs != INVALID_FILE_ATTRIBUTES) && ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
#else
        exists = (stat(root, &st) == 0) && S_ISDIR(st.st_mode);
#endif
    }

    if (exists) {
        snprintf(s_assets_root,   sizeof(s_assets_root),   "%s", root);
        snprintf(s_assets_folder, sizeof(s_assets_folder), "%s", root);
        s_assets_needs_scan = true;
    }
}

static void panel_tabs(const char **labels, int count, int active)
{
    ca_tabs(&(Ca_TabBarDesc){
        .labels        = labels,
        .count         = count,
        .active        = active,
        .style         = "panel-tab-bar",
        .active_text   = CA_THEME_TEXT_BRIGHT,
        .inactive_text = CA_THEME_TEXT_DIM,
        .active_bg     = CA_THEME_BG_OVERLAY,
        .inactive_bg   = CA_THEME_TRANSPARENT,
        .tab_padding_x = 0.0f,
        .tabs_fill     = (count == 1),
        .tabs_left_align = true,
    });
}

static uint32_t log_level_color(Qs_LogLevel level)
{
    switch (level) {
    case QS_LOG_DEBUG: return CA_THEME_TEXT_DIM;
    case QS_LOG_TRACE: return CA_THEME_TEXT_MUTED;
    case QS_LOG_INFO:  return CA_THEME_SUCCESS;
    case QS_LOG_WARN:  return CA_THEME_WARNING;
    case QS_LOG_ERROR: return CA_THEME_DANGER;
    case QS_LOG_FATAL: return CA_THEME_FATAL;
    default:           return CA_THEME_TEXT_DIM;
    }
}

static const char *log_level_icon(Qs_LogLevel level)
{
    switch (level) {
    case QS_LOG_DEBUG: return ICON_LOG_DEBUG;
    case QS_LOG_TRACE: return ICON_LOG_TRACE;
    case QS_LOG_INFO:  return ICON_LOG_INFO;
    case QS_LOG_WARN:  return ICON_LOG_WARN;
    case QS_LOG_ERROR: return ICON_LOG_ERROR;
    case QS_LOG_FATAL: return ICON_LOG_FATAL;
    default:           return ICON_LOG_INFO;
    }
}

static const char *status_log_style(Qs_LogLevel level)
{
    switch (level) {
    case QS_LOG_DEBUG: return "status-icon status-log-debug";
    case QS_LOG_TRACE: return "status-icon status-log-trace";
    case QS_LOG_INFO:  return "status-icon status-log-info";
    case QS_LOG_WARN:  return "status-icon status-log-warn";
    case QS_LOG_ERROR: return "status-icon status-log-error";
    case QS_LOG_FATAL: return "status-icon status-log-fatal";
    default:           return "status-icon status-log-empty";
    }
}

static Qs_LogLevel status_most_severe_log_level(const Qs_LogCounts *counts)
{
    if (!counts || counts->total == 0) return QS_LOG_INFO;
    for (int level_index = (int)QS_LOG_LEVEL_COUNT - 1; level_index >= 0; level_index--) {
        if (counts->levels[level_index] > 0)
            return (Qs_LogLevel)level_index;
    }
    return QS_LOG_INFO;
}

static void console_format_log_entry(const Qs_LogEntry *entry,
                                     char *out,
                                     size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!entry) {
        out[0] = '\0';
        return;
    }

    int hours = (int)(entry->timestamp / 3600.0);
    int minutes = (int)(entry->timestamp / 60.0) % 60;
    int seconds = (int)entry->timestamp % 60;
    int millis = (int)((entry->timestamp - (int)entry->timestamp) * 1000.0);

    snprintf(out, out_size,
             "[%02d:%02d:%02d.%03d] [%s] %s",
             hours, minutes, seconds, millis,
             qs_log_level_str(entry->level),
             entry->message ? entry->message : "");
}

static bool console_entry_matches_search(const Qs_LogEntry *entry)
{
    if (!entry) return false;
    if (!s_console_search[0]) return true;
    if (entry->message && contains_ci(entry->message, s_console_search)) return true;
    if (contains_ci(qs_log_level_str(entry->level), s_console_search)) return true;

    char line_buf[CONSOLE_LINE_MAX];
    console_format_log_entry(entry, line_buf, sizeof(line_buf));
    return contains_ci(line_buf, s_console_search);
}

static void console_sync_toolbar(const Qs_LogEntry *entries, uint32_t count)
{
    uint32_t level_counts[QS_LOG_LEVEL_COUNT] = {0};
    uint32_t matched_count = 0;
    for (uint32_t entry_index = 0; entry_index < count; entry_index++) {
        if (!entries || !console_entry_matches_search(&entries[entry_index])) continue;
        matched_count++;
        Qs_LogLevel level = entries[entry_index].level;
        if (level >= 0 && level < QS_LOG_LEVEL_COUNT)
            level_counts[level]++;
    }

    for (uint32_t level_index = 0; level_index < QS_LOG_LEVEL_COUNT; level_index++) {
        if (!s_console_count_labels[level_index]) continue;
        char count_text[32];
        Qs_LogLevel level = (Qs_LogLevel)level_index;
        snprintf(count_text, sizeof(count_text), "%u", level_counts[level_index]);
        if (s_console_count_icons[level_index])
            ca_set_color(s_console_count_icons[level_index], log_level_color(level));
        ca_set_text(s_console_count_labels[level_index], count_text);
        ca_set_color(s_console_count_labels[level_index], log_level_color(level));
    }

    if (s_console_total_label) {
        char total_text[32];
        snprintf(total_text, sizeof(total_text), "%u / %u", matched_count, count);
        ca_set_text(s_console_total_label, total_text);
    }

    if (s_console_clear_btn)
        ca_set_disabled(s_console_clear_btn, count == 0);
    if (s_console_copy_btn)
        ca_set_disabled(s_console_copy_btn, matched_count == 0);

    if (s_console_lock_btn) {
        ca_set_text(s_console_lock_btn,
                    s_console_autoscroll_locked ? ICON_LOCK : ICON_UNLOCK);
        ca_set_style(s_console_lock_btn,
                     s_console_autoscroll_locked
                         ? "console-toolbar-btn active"
                         : "console-toolbar-btn");
    }
}

static void console_copy_entry(uint32_t entry_index)
{
    uint32_t count = 0;
    const Qs_LogEntry *entries = qs_log_entries(&count);
    if (!entries || entry_index >= count) return;

    char line_buf[CONSOLE_LINE_MAX];
    console_format_log_entry(&entries[entry_index], line_buf, sizeof(line_buf));
    ca_clipboard_set_text(s_console_window, line_buf);
}

static void console_copy_all(void)
{
    uint32_t count = 0;
    const Qs_LogEntry *entries = qs_log_entries(&count);
    if (!entries || count == 0) {
        ca_clipboard_set_text(s_console_window, "");
        return;
    }

    size_t total_size = 1;
    for (uint32_t entry_index = 0; entry_index < count; entry_index++) {
        if (!console_entry_matches_search(&entries[entry_index])) continue;
        const char *message = entries[entry_index].message ? entries[entry_index].message : "";
        size_t needed = strlen(message) + 64;
        if (total_size > SIZE_MAX - needed) return;
        total_size += needed;
    }

    char *copy_text = (char *)qs_malloc(total_size, QS_MEM_EDITOR);
    if (!copy_text) return;

    size_t cursor = 0;
    copy_text[0] = '\0';
    bool wrote_line = false;
    for (uint32_t entry_index = 0; entry_index < count; entry_index++) {
        if (!console_entry_matches_search(&entries[entry_index])) continue;
        char line_buf[CONSOLE_LINE_MAX];
        console_format_log_entry(&entries[entry_index], line_buf, sizeof(line_buf));
        size_t line_len = strlen(line_buf);
        size_t separator_len = wrote_line ? 1u : 0u;
        if (cursor + separator_len + line_len + 1 > total_size) break;
        if (separator_len)
            copy_text[cursor++] = '\n';
        memcpy(copy_text + cursor, line_buf, line_len);
        cursor += line_len;
        copy_text[cursor] = '\0';
        wrote_line = true;
    }

    ca_clipboard_set_text(s_console_window, copy_text);
    qs_free(copy_text);
}

static void console_clear(void)
{
    qs_log_clear();
    s_prev_log_count = UINT32_MAX;
    s_needs_scroll = false;

    for (uint32_t line_index = 0; line_index < CONSOLE_MAX_LINES; line_index++) {
        s_console_line_ctx[line_index].has_entry = false;
        s_console_line_ctx[line_index].entry_index = 0;
        if (s_console_lines[line_index])
            ca_set_hidden(s_console_lines[line_index], true);
    }
    console_sync_toolbar(NULL, 0);
}

static void on_console_lock_click(Ca_Button *btn, void *user_data)
{
    (void)btn;
    (void)user_data;
    s_console_autoscroll_locked = !s_console_autoscroll_locked;
    if (!s_console_autoscroll_locked)
        s_needs_scroll = true;
    uint32_t count = 0;
    const Qs_LogEntry *entries = qs_log_entries(&count);
    console_sync_toolbar(entries, count);
}

static void on_console_clear_click(Ca_Button *btn, void *user_data)
{
    (void)btn;
    (void)user_data;
    console_clear();
}

static void on_console_copy_click(Ca_Button *btn, void *user_data)
{
    (void)btn;
    (void)user_data;
    console_copy_all();
}

static void on_console_ctx_menu(int item_index, void *user_data)
{
    ConsoleLineCtx *ctx = (ConsoleLineCtx *)user_data;
    switch (item_index) {
    case 0:
        if (ctx && ctx->has_entry)
            console_copy_entry(ctx->entry_index);
        break;
    case 1:
        console_copy_all();
        break;
    case 3:
        console_clear();
        break;
    default:
        break;
    }
}

void ed_layout(Ca_Window *window, void *editor)
{
    Editor *ed = (Editor *)editor;

    s_console_window = window;
    s_bottom_tab_active = 0;

    ed_thumbnail_init(editor_engine(ed), window);
    assets_init(ed);

    /* Vertical split: top three-panel area | bottom panel (full width) */
    ca_split_begin(&(Ca_SplitDesc){
        .direction       = CA_VERTICAL,
        .ratio           = 0.72f,
        .min_ratio       = 0.40f,
        .max_ratio       = 0.90f,
        .bar_size        = 1.0f,
        .bar_color       = CA_THEME_BG_VOID,
        .bar_hover_color = CA_THEME_ACCENT,
    });
    {
        /* ---- Top: three-column horizontal split ---- */
        ca_split_begin(&(Ca_SplitDesc){
            .direction       = CA_HORIZONTAL,
            .ratio           = 0.15f,
            .min_ratio       = 0.10f,
            .max_ratio       = 0.30f,
            .bar_size        = 1.0f,
            .bar_color       = CA_THEME_BG_VOID,
            .bar_hover_color = CA_THEME_ACCENT,
        });
        {
            /* Left panel — Hierarchy */
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_VERTICAL,
                .style     = "panel",
            });
            ca_div_begin(&(Ca_DivDesc){
                .direction = CA_HORIZONTAL,
                .style     = "panel-tab-bar",
            });
            {
                static const char *tabs[] = { "Hierarchy" };
                panel_tabs(tabs, 1, 0);
            }
            ca_div_end();
            ed_hierarchy(editor);
            ca_div_end();

            /* Center + Right split */
            ca_split_begin(&(Ca_SplitDesc){
                .direction       = CA_HORIZONTAL,
                .ratio           = 0.75f,
                .min_ratio       = 0.40f,
                .max_ratio       = 0.88f,
                .bar_size        = 1.0f,
                .bar_color       = CA_THEME_BG_VOID,
                .bar_hover_color = CA_THEME_ACCENT,
            });
            {
                /* Center — Scene viewport */
                ca_div_begin(&(Ca_DivDesc){
                    .direction = CA_VERTICAL,
                    .style     = "panel panel-viewport",
                });
                ca_div_begin(&(Ca_DivDesc){
                    .direction = CA_HORIZONTAL,
                    .style     = "panel-tab-bar",
                });
                {
                    static const char *tabs[] = { "Scene" };
                    panel_tabs(tabs, 1, 0);
                }
                ca_div_end();
                {
                    Ca_Viewport *vp = ca_viewport(&(Ca_ViewportDesc){ 0 });
                    qs_renderer_bind(editor_scene_renderer(editor), (Qs_Viewport *)vp);
                    editor_set_scene_viewport(editor, vp);
                }
                ca_div_end();

                /* Right panel — Inspector / System */
                ca_div_begin(&(Ca_DivDesc){
                    .direction = CA_VERTICAL,
                    .style     = "panel",
                });
                ca_div_begin(&(Ca_DivDesc){
                    .direction = CA_HORIZONTAL,
                    .style     = "panel-tab-bar",
                });
                {
                    static const char *right_tabs[] = { "Inspector", "System" };
                    ca_tabs(&(Ca_TabBarDesc){
                        .labels        = right_tabs,
                        .count         = 2,
                        .active        = s_right_tab_active,
                        .on_change     = on_right_tab_change,
                        .style         = "panel-tab-bar",
                        .active_text   = CA_THEME_TEXT_BRIGHT,
                        .inactive_text = CA_THEME_TEXT_DIM,
                        .active_bg     = CA_THEME_BG_OVERLAY,
                        .inactive_bg   = CA_THEME_TRANSPARENT,
                        .tab_padding_x = 0.0f,
                        .tabs_fill     = false,
                        .tabs_left_align = true,
                    });
                }
                ca_div_end();
                /* Inspector panel */
                s_inspector_panel = ca_div_begin(&(Ca_DivDesc){
                    .direction = CA_VERTICAL,
                    .style     = "panel-content-fill",
                    .hidden    = (s_right_tab_active != 0),
                });
                ed_inspector(editor);
                ca_div_end(); /* s_inspector_panel */
                /* System panel */
                s_system_panel_div = ca_div_begin(&(Ca_DivDesc){
                    .direction = CA_VERTICAL,
                    .style     = "panel-content-fill",
                    .hidden    = (s_right_tab_active != 1),
                });
                ed_system_panel();
                ca_div_end(); /* s_system_panel_div */
                ca_div_end();
            }
            ca_split_end();
        }
        ca_split_end();

        /* ---- Bottom: Console / Assets (full width) ---- */
        ca_div_begin(&(Ca_DivDesc){
            .direction = CA_VERTICAL,
            .style     = "panel panel-bottom",
        });
        {
            static const char *bottom_tabs[] = { "Console", "Assets" };
            ca_tabs(&(Ca_TabBarDesc){
                .labels        = bottom_tabs,
                .count         = 2,
                .active        = s_bottom_tab_active,
                .on_change     = on_bottom_tab_change,
                .style         = "panel-tab-bar",
                .active_text   = CA_THEME_TEXT_BRIGHT,
                .inactive_text = CA_THEME_TEXT_DIM,
                .active_bg     = CA_THEME_BG_OVERLAY,
                .inactive_bg   = CA_THEME_TRANSPARENT,
                .tab_padding_x = 0.0f,
                .tabs_fill     = false,
                .tabs_left_align = true,
            });

            /* Console content */
            s_console_panel = ca_div_begin(&(Ca_DivDesc){
                .direction = CA_VERTICAL,
                .style     = "console-root",
                .hidden    = (s_bottom_tab_active != 0),
            });
            {
                ca_div_begin(&(Ca_DivDesc){
                    .direction = CA_HORIZONTAL,
                    .style     = "console-toolbar",
                });
                {
                    s_console_clear_btn = ca_btn_begin(&(Ca_BtnDesc){
                        .text       = ICON_TRASH,
                        .style      = "console-toolbar-btn console-clear-btn",
                        .on_click   = on_console_clear_click,
                        .click_data = NULL,
                        .disabled   = true,
                    });
                    ca_btn_end();
                    ca_tooltip(&(Ca_TooltipDesc){ .text = "Clear console" });

                    s_console_copy_btn = ca_btn_begin(&(Ca_BtnDesc){
                        .text       = ICON_COPY,
                        .style      = "console-toolbar-btn",
                        .on_click   = on_console_copy_click,
                        .click_data = NULL,
                        .disabled   = true,
                    });
                    ca_btn_end();
                    ca_tooltip(&(Ca_TooltipDesc){ .text = "Copy shown logs" });

                    ca_div_begin(&(Ca_DivDesc){
                        .direction = CA_HORIZONTAL,
                        .style     = "console-search-box",
                    });
                    {
                        ca_text(&(Ca_TextDesc){
                            .text  = ICON_SEARCH,
                            .style = "console-search-icon",
                        });
                        s_console_search_input = ca_input(&(Ca_InputDesc){
                            .text        = s_console_search,
                            .placeholder = "Search logs...",
                            .style       = "console-search-input",
                        });
                    }
                    ca_div_end();

                    static const char *level_tooltips[QS_LOG_LEVEL_COUNT] = {
                        "Debug messages", "Trace messages", "Info messages",
                        "Warning messages", "Error messages", "Fatal messages",
                    };
                    ca_div_begin(&(Ca_DivDesc){ .direction = CA_HORIZONTAL, .style = "console-counts-bar" });
                    for (uint32_t level_index = 0; level_index < QS_LOG_LEVEL_COUNT; level_index++) {
                        Qs_LogLevel level = (Qs_LogLevel)level_index;
                        ca_div_begin(&(Ca_DivDesc){
                            .direction = CA_HORIZONTAL,
                            .style     = "console-count-group",
                        });
                        s_console_count_icons[level_index] = ca_text(&(Ca_TextDesc){
                            .text  = log_level_icon(level),
                            .style = "console-count-icon",
                        });
                        ca_set_color(s_console_count_icons[level_index], log_level_color(level));
                        s_console_count_labels[level_index] = ca_text(&(Ca_TextDesc){
                            .text  = "0",
                            .style = "console-count-value",
                        });
                        ca_set_color(s_console_count_labels[level_index], log_level_color(level));
                        ca_div_end();
                        ca_tooltip(&(Ca_TooltipDesc){ .text = level_tooltips[level_index] });
                    }
                    ca_div_end();

                    ca_spacer(&(Ca_SpacerDesc){ .style = "console-toolbar-spacer" });

                    s_console_total_label = ca_text(&(Ca_TextDesc){
                        .text  = "0 / 0",
                        .style = "console-total",
                    });

                    s_console_lock_btn = ca_btn_begin(&(Ca_BtnDesc){
                        .text       = ICON_UNLOCK,
                        .style      = "console-toolbar-btn",
                        .on_click   = on_console_lock_click,
                        .click_data = NULL,
                    });
                    ca_btn_end();
                    ca_tooltip(&(Ca_TooltipDesc){ .text = "Toggle auto-scroll" });
                }
                ca_div_end();

                ca_div_begin(&(Ca_DivDesc){
                    .direction = CA_VERTICAL,
                    .style     = "console-scroll",
                    .id        = "console",
                });
                for (uint32_t i = 0; i < CONSOLE_MAX_LINES; i++) {
                    s_console_line_ctx[i].has_entry = false;
                    s_console_line_ctx[i].entry_index = 0;
                    s_console_lines[i] = ca_text(&(Ca_TextDesc){
                        .text   = "",
                        .style  = "console-line",
                        .hidden = true,
                    });
                    ca_context_menu(&(Ca_CtxMenuDesc){
                        .items       = s_console_ctx_items,
                        .item_count  = 4,
                        .on_select   = on_console_ctx_menu,
                        .select_data = &s_console_line_ctx[i],
                    });
                }
                ca_div_end();
            }
            ca_div_end();

            /* Assets content */
            s_assets_panel = ca_div_begin(&(Ca_DivDesc){
                .direction = CA_VERTICAL,
                .style     = "assets-root",
                .id        = "assets-panel",
                .hidden    = (s_bottom_tab_active != 1),
            });
            {
                /* ── Single toolbar row ─────────────────────────────────── */
                ca_div_begin(&(Ca_DivDesc){
                    .direction = CA_HORIZONTAL,
                    .style     = "assets-toolbar",
                });
                {
                    /* Navigation icons */
                    s_assets_up_btn = ca_btn_begin(&(Ca_BtnDesc){
                        .text       = ICON_UP,
                        .style      = "assets-btn",
                        .on_click   = on_assets_up_click,
                        .click_data = NULL,
                    });
                    ca_btn_end();

                    ca_btn_begin(&(Ca_BtnDesc){
                        .text       = ICON_REFRESH,
                        .style      = "assets-btn",
                        .on_click   = on_assets_refresh_click,
                        .click_data = NULL,
                    });
                    ca_btn_end();

                    /* Current path — grows to fill available space */
                    s_assets_path_label = ca_text(&(Ca_TextDesc){
                        .text  = "assets",
                        .style = "assets-path",
                    });

                    /* Filter dropdown */
                    static const char *filter_options[] = {
                        "All", "Model", "Texture", "Material",
                        "Mesh", "Scene", "Script",
                    };
                    s_assets_filter_select = ca_select(&(Ca_SelectDesc){
                        .options      = filter_options,
                        .option_count = 7,
                        .selected     = 0,
                        .id           = "assets-filter-select",
                        .style        = "assets-filter-select",
                        .on_change    = on_assets_filter_select,
                    });

                    /* View mode buttons — icon-only, one active at a time */
                    s_assets_view_btns[ASSETS_VIEW_LIST] = ca_btn_begin(&(Ca_BtnDesc){
                        .text       = ICON_VIEW_LIST,
                        .style      = "assets-btn",
                        .on_click   = on_assets_view_mode_click,
                        .click_data = (void *)(intptr_t)ASSETS_VIEW_LIST,
                    });
                    ca_btn_end();

                    s_assets_view_btns[ASSETS_VIEW_GRID] = ca_btn_begin(&(Ca_BtnDesc){
                        .text       = ICON_VIEW_GRID,
                        .style      = "assets-btn",
                        .on_click   = on_assets_view_mode_click,
                        .click_data = (void *)(intptr_t)ASSETS_VIEW_GRID,
                    });
                    ca_btn_end();

                    s_assets_view_btns[ASSETS_VIEW_THUMBNAIL] = ca_btn_begin(&(Ca_BtnDesc){
                        .text       = ICON_VIEW_THUMB,
                        .style      = "assets-btn",
                        .on_click   = on_assets_view_mode_click,
                        .click_data = (void *)(intptr_t)ASSETS_VIEW_THUMBNAIL,
                    });
                    ca_btn_end();

                    /* Search — styled box with icon (mirrors console search) */
                    ca_div_begin(&(Ca_DivDesc){
                        .direction = CA_HORIZONTAL,
                        .style     = "assets-search-box",
                    });
                    {
                        ca_text(&(Ca_TextDesc){
                            .text  = ICON_SEARCH,
                            .style = "assets-search-icon",
                        });
                        s_assets_search_input = ca_input(&(Ca_InputDesc){
                            .text        = s_assets_search,
                            .placeholder = "Search...",
                            .style       = "assets-search-input",
                        });
                    }
                    ca_div_end();
                }
                ca_div_end();

                ca_split_begin(&(Ca_SplitDesc){
                    .direction = CA_HORIZONTAL,
                    .ratio = 0.24f,
                    .min_ratio = 0.14f,
                    .max_ratio = 0.45f,
                    .bar_size = 1.0f,
                    .bar_color = CA_THEME_BG_VOID,
                    .bar_hover_color = CA_THEME_ACCENT,
                });
                {
                    s_assets_tree_div = ca_div_begin(&(Ca_DivDesc){
                        .direction = CA_VERTICAL,
                        .style = "assets-tree-pane",
                        .id = "assets-tree",
                    });
                    ca_div_end();

                    s_assets_scroll_div = ca_div_begin(&(Ca_DivDesc){
                        .direction = CA_VERTICAL,
                        .style     = "assets-scroll",
                        .id        = "assets-scroll",
                    });
                    ca_div_end();
                }
                ca_split_end();
            }
            ca_div_end();
        }
        ca_div_end();
    }
    ca_split_end();

    if (s_assets_needs_scan) {
        s_assets_needs_scan = false;
        assets_scan_folder();
    }
    assets_sync_widgets();
}

void ed_console_update(void *editor)
{
    (void)editor;

    if (s_assets_needs_scan) {
        s_assets_needs_scan = false;
        assets_scan_folder();
    }
    assets_sync_widgets();

    uint32_t count = 0;
    const Qs_LogEntry *entries = qs_log_entries(&count);

    bool search_changed = false;
    if (s_console_search_input) {
        const char *search_text = ca_get_text(s_console_search_input);
        if (!search_text) search_text = "";
        if (strncmp(s_console_search, search_text, sizeof(s_console_search) - 1) != 0) {
            snprintf(s_console_search, sizeof(s_console_search), "%s", search_text);
            search_changed = true;
        }
    }

    /* Scroll to bottom on the NEXT frame so content_h is up to date */
    if (s_needs_scroll && !s_console_autoscroll_locked) {
        s_needs_scroll = false;
        ca_scroll_to_bottom(s_console_window, "console");
    }

    if (count == s_prev_log_count && !search_changed) return;
    s_prev_log_count = count;
    console_sync_toolbar(entries, count);
    s_needs_scroll = !s_console_autoscroll_locked;   /* scroll after next layout pass */

    uint32_t visible_indices[CONSOLE_MAX_LINES];
    uint32_t visible = 0;
    if (entries) {
        for (uint32_t reverse_index = count; reverse_index > 0; reverse_index--) {
            uint32_t entry_index = reverse_index - 1;
            if (!console_entry_matches_search(&entries[entry_index])) continue;
            visible_indices[visible++] = entry_index;
            if (visible == CONSOLE_MAX_LINES) break;
        }
    }

    char line_buf[CONSOLE_LINE_MAX];
    for (uint32_t i = 0; i < CONSOLE_MAX_LINES; i++) {
        if (i < visible) {
            uint32_t entry_index = visible_indices[visible - 1 - i];
            const Qs_LogEntry *e = &entries[entry_index];
            console_format_log_entry(e, line_buf, sizeof(line_buf));

            s_console_line_ctx[i].entry_index = entry_index;
            s_console_line_ctx[i].has_entry = true;
            ca_set_text(s_console_lines[i], line_buf);
            ca_set_color(s_console_lines[i], log_level_color(e->level));
            ca_set_hidden(s_console_lines[i], false);
        } else {
            s_console_line_ctx[i].entry_index = 0;
            s_console_line_ctx[i].has_entry = false;
            ca_set_hidden(s_console_lines[i], true);
        }
    }
}

/* ================================================================
   STATUS BAR
   ================================================================ */

static void status_metric_begin(const char *style, const char *id)
{
    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .style     = style,
        .id        = id,
    });
}

static void status_format_counts_tooltip(const Qs_LogCounts *counts,
                                         char *out,
                                         size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!counts) {
        snprintf(out, out_size, "Logs\nInfo: 0\nWarnings: 0\nErrors: 0");
        return;
    }

    snprintf(out, out_size,
             "Logs\nDebug: %u\nTrace: %u\nInfo: %u\nWarnings: %u\nErrors: %u\nFatal: %u\nTotal: %u",
             counts->levels[QS_LOG_DEBUG],
             counts->levels[QS_LOG_TRACE],
             counts->levels[QS_LOG_INFO],
             counts->levels[QS_LOG_WARN],
             counts->levels[QS_LOG_ERROR],
             counts->levels[QS_LOG_FATAL],
             counts->total);
}

void ed_status_bar_update(void *editor_ptr)
{
    Editor *editor = (Editor *)editor_ptr;
    if (!editor) return;

    Qs_Engine *engine = editor_engine(editor);
    float dt = engine ? qs_engine_dt(engine) : 0.0f;
    float fps = (dt > 0.000001f) ? (1.0f / dt) : 0.0f;
    if (!s_status_has_fps) {
        s_status_fps = fps;
        s_status_has_fps = true;
    } else {
        s_status_fps += (fps - s_status_fps) * 0.15f;
    }

    if (s_status_fps_icon) {
        const char *fps_style = s_status_fps > 60.0f ? "status-icon status-fps-good"
                              : s_status_fps >= 30.0f ? "status-icon status-fps-warn"
                                                       : "status-icon status-fps-bad";
        ca_set_style(s_status_fps_icon, fps_style);
    }
    if (s_status_fps_tip) {
        char fps_tip[64];
        snprintf(fps_tip, sizeof(fps_tip), "FPS: %.1f\nFrame: %.2f ms",
                 s_status_fps, dt * 1000.0f);
        ca_tooltip_set_text(s_status_fps_tip, fps_tip);
    }

    Qs_Project *project = editor_project(editor);
    const char *project_name = qs_project_name(project);
    if (!project_name || !project_name[0]) project_name = "No Project";
    if (s_status_project_name)
        ca_set_text(s_status_project_name, project_name);
    if (s_status_project_tip) {
        char project_tip[1024];
        const char *project_path = qs_project_path(project);
        snprintf(project_tip, sizeof(project_tip), "%s\n%s",
                 project_name,
                 (project_path && project_path[0]) ? project_path : "No project path");
        ca_tooltip_set_text(s_status_project_tip, project_tip);
    }

    Qs_Scene *scene = qs_scene_active();
    uint32_t entity_count = qs_scene_entity_count(scene);
    if (s_status_scene_count) {
        char scene_count_text[32];
        snprintf(scene_count_text, sizeof(scene_count_text), "%u", entity_count);
        ca_set_text(s_status_scene_count, scene_count_text);
    }
    if (s_status_scene_tip) {
        char scene_tip[128];
        const char *scene_name = qs_scene_name(scene);
        snprintf(scene_tip, sizeof(scene_tip), "%s\nEntities: %u",
                 (scene_name && scene_name[0]) ? scene_name : "No active scene",
                 entity_count);
        ca_tooltip_set_text(s_status_scene_tip, scene_tip);
    }

    uint32_t renderable_count = 0;
    Qs_Renderer *renderer = editor_scene_renderer(editor);
    qs_renderer_renderables(renderer, &renderable_count);
    if (s_status_render_count) {
        char render_count_text[32];
        snprintf(render_count_text, sizeof(render_count_text), "%u", renderable_count);
        ca_set_text(s_status_render_count, render_count_text);
    }
    if (s_status_render_tip) {
        char render_tip[96];
        snprintf(render_tip, sizeof(render_tip), "Renderables: %u", renderable_count);
        ca_tooltip_set_text(s_status_render_tip, render_tip);
    }

    Qs_LogCounts log_counts;
    qs_log_counts(&log_counts);
    Qs_LogLevel severe_level = status_most_severe_log_level(&log_counts);
    uint32_t severe_count = log_counts.total > 0 ? log_counts.levels[severe_level] : 0;
    if (s_status_log_icon) {
        ca_set_text(s_status_log_icon, log_level_icon(severe_level));
        ca_set_style(s_status_log_icon,
                     log_counts.total > 0 ? status_log_style(severe_level)
                                          : "status-icon status-log-empty");
    }
    if (s_status_log_count) {
        char log_count_text[32];
        snprintf(log_count_text, sizeof(log_count_text), "%u", severe_count);
        ca_set_text(s_status_log_count, log_count_text);
        ca_set_style(s_status_log_count,
                     log_counts.total > 0 ? "status-value status-log-value"
                                          : "status-value status-log-empty");
    }
    if (s_status_log_tip) {
        char log_tip[256];
        status_format_counts_tooltip(&log_counts, log_tip, sizeof(log_tip));
        ca_tooltip_set_text(s_status_log_tip, log_tip);
    }
}

void ed_status_bar(Ca_Window *window, void *editor)
{
    (void)window;

    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .style     = "status-bar",
    });

    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .style     = "status-left",
    });
    {
        status_metric_begin("status-item", "status-fps");
        s_status_fps_icon = ca_text(&(Ca_TextDesc){
            .text  = ICON_FPS,
            .style = "status-icon status-fps-bad",
        });
        ca_div_end();
        s_status_fps_tip = ca_tooltip(&(Ca_TooltipDesc){ .text = "FPS: 0.0" });

        status_metric_begin("status-item", "status-project");
        ca_text(&(Ca_TextDesc){ .text = ICON_PROJECT, .style = "status-icon status-project-icon" });
        s_status_project_name = ca_text(&(Ca_TextDesc){
            .text  = "No Project",
            .style = "status-label",
        });
        ca_div_end();
        s_status_project_tip = ca_tooltip(&(Ca_TooltipDesc){ .text = "No Project" });
    }
    ca_div_end();

    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .style     = "status-spacer",
    });
    ca_div_end();

    ca_div_begin(&(Ca_DivDesc){
        .direction = CA_HORIZONTAL,
        .style     = "status-right",
    });
    {
        status_metric_begin("status-item", "status-scene");
        ca_text(&(Ca_TextDesc){ .text = ICON_SCENE, .style = "status-icon status-scene-icon" });
        s_status_scene_count = ca_text(&(Ca_TextDesc){
            .text  = "0",
            .style = "status-value",
        });
        ca_div_end();
        s_status_scene_tip = ca_tooltip(&(Ca_TooltipDesc){ .text = "Entities: 0" });

        status_metric_begin("status-item", "status-render");
        ca_text(&(Ca_TextDesc){ .text = ICON_RENDER, .style = "status-icon status-render-icon" });
        s_status_render_count = ca_text(&(Ca_TextDesc){
            .text  = "0",
            .style = "status-value",
        });
        ca_div_end();
        s_status_render_tip = ca_tooltip(&(Ca_TooltipDesc){ .text = "Renderables: 0" });

        status_metric_begin("status-item", "status-log");
        s_status_log_icon = ca_text(&(Ca_TextDesc){
            .text  = ICON_LOG_INFO,
            .style = "status-icon status-log-empty",
        });
        s_status_log_count = ca_text(&(Ca_TextDesc){
            .text  = "0",
            .style = "status-value status-log-empty",
        });
        ca_div_end();
        s_status_log_tip = ca_tooltip(&(Ca_TooltipDesc){ .text = "Logs\nInfo: 0" });
    }
    ca_div_end();

    ca_div_end();

    ed_status_bar_update(editor);
}

/* ================================================================
   TOOLBAR ICON BUTTON
   ================================================================ */

Ca_Button *ed_icon_btn(const EdIconBtnDesc *desc)
{
    if (!desc || !desc->icon) return NULL;

    Ca_Button *btn = ca_btn_begin(&(Ca_BtnDesc){
        .text       = desc->icon,
        .id         = desc->id,
        .style      = "toolbar-icon-btn",
        .on_click   = desc->on_click,
        .click_data = desc->click_data,
    });
    ca_btn_end();

    if (desc->tooltip)
        ca_tooltip(&(Ca_TooltipDesc){ .text = desc->tooltip });

    return btn;
}

void ed_icon_btn_set_active(Ca_Button *btn, bool active)
{
    if (!btn) return;
    ca_set_style(btn, active ? "toolbar-icon-btn active" : "toolbar-icon-btn");
}
