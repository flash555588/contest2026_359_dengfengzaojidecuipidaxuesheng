/* Shared v1/v3 file manager. Filesystem ownership stays in the worker. */
#include <errno.h>
#include <limits.h>
#include "glass_file_service.h"
#ifndef GLASS_FILES_ROOT
#define GLASS_FILES_ROOT "/data"
#endif
static char g_files_path[GLASS_FILE_PATH_CAP] = GLASS_FILES_ROOT;
static char g_files_selected[GLASS_FILE_PATH_CAP];
static char g_files_error[160];
static unsigned g_files_page;
static bool g_files_pending;
static lv_obj_t *g_files_input;
static lv_obj_t *g_files_shell;
static lv_obj_t *g_files_title;
static lv_obj_t *g_files_auxiliary;
static struct
{
  lv_obj_t *card;
  lv_obj_t *list;
  lv_obj_t *path;
  lv_obj_t *error;
  lv_obj_t *footer;
  lv_obj_t *up;
  lv_obj_t *previous;
  lv_obj_t *next;
  lv_obj_t *cancel;
  lv_obj_t *paste;
} g_files_browser;
static int g_files_operation;
static char g_files_clipboard[GLASS_FILE_PATH_CAP];
static bool g_files_clipboard_move;
static struct glass_file_job *g_files_job;
/* Rows borrow immutable entry names, which LV_LABEL_LONG_DOT cannot alter. */
static struct glass_file_job *g_files_list_job;
static lv_timer_t *g_files_timer;
static bool g_files_retry;
static struct
{
  enum glass_file_action action;
  unsigned page;
  char path[GLASS_FILE_PATH_CAP];
  char source[GLASS_FILE_PATH_CAP];
  char name[GLASS_FILE_NAME_CAP];
} g_files_request;
static lv_obj_t *g_files_copy_status;
static uint64_t g_files_copy_bytes;
static void glass_files_render(void *unused);
static void glass_files_details(void *unused);
static void glass_files_dialog_async(void *unused);
static void glass_files_copy_start(void *unused);
static void glass_files_select(lv_event_t *e);
static void glass_files_tick(lv_timer_t *timer);
static void glass_files_details_ready(const struct glass_file_result *result);

static void glass_files_cancel_calls(void)
{
  lv_async_call_cancel(glass_files_render, NULL);
  lv_async_call_cancel(glass_files_details, NULL);
  lv_async_call_cancel(glass_files_dialog_async, NULL);
  lv_async_call_cancel(glass_files_copy_start, NULL);
}

static void glass_files_stop(void)
{
  if (g_files_timer) lv_timer_delete(g_files_timer);
  g_files_timer = NULL;
  /* release only cancels and drops ownership; it never closes or joins. */
  glass_file_release(g_files_job);
  g_files_job = NULL;
  g_files_retry = false;
  g_files_pending = false;
  g_files_copy_status = NULL;
}

static void glass_files_list_clear(void)
{
  /* Delete every borrower before releasing the completed LIST result. */
  if (g_files_browser.list) lv_obj_clean(g_files_browser.list);
  glass_file_release(g_files_list_job);
  g_files_list_job = NULL;
}

static void glass_files_closed(lv_event_t *e)
{
  LV_UNUSED(e);
  glass_files_cancel_calls();
  glass_files_stop();
  glass_files_list_clear();
  g_files_input = NULL;
  g_files_shell = NULL;
  g_files_title = NULL;
  g_files_auxiliary = NULL;
  memset(&g_files_browser, 0, sizeof(g_files_browser));
}

static lv_obj_t *glass_files_card(const char *title)
{
  glass_files_cancel_calls();
  glass_files_stop();
  glass_files_list_clear();
  if (!g_files_shell)
    {
      g_files_shell = panel_card(title);
      if (!g_files_shell) return NULL;
      g_files_title = lv_obj_get_child(g_files_shell, 0);
      lv_obj_set_style_radius(g_files_shell, 0, 0);
      lv_obj_add_event_cb(g_files_shell, glass_files_closed, LV_EVENT_DELETE, NULL);
    }
  g_files_input = NULL;
  lv_label_set_text(g_files_title, title);
  if (g_files_browser.card) lv_obj_add_flag(g_files_browser.card, LV_OBJ_FLAG_HIDDEN);
  if (!g_files_auxiliary)
    {
      g_files_auxiliary = lv_obj_create(g_files_shell);
      lv_obj_remove_style_all(g_files_auxiliary);
      lv_obj_set_size(g_files_auxiliary, lv_pct(100), lv_pct(100));
      lv_obj_remove_flag(g_files_auxiliary, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }
  else lv_obj_clean(g_files_auxiliary);
  lv_obj_remove_flag(g_files_auxiliary, LV_OBJ_FLAG_HIDDEN);
  return g_files_auxiliary;
}

static lv_obj_t *glass_files_icon(lv_obj_t *parent, const char *symbol,
                                 int x, int y, lv_event_cb_t callback, void *data)
{
  lv_obj_t *button = lv_button_create(parent);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, 64, 48);
  style_button(button, theme_surface());
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, data);
  lv_obj_t *label = lv_label_create(button);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(theme_primary()), 0);
  lv_label_set_text(label, symbol);
  lv_obj_center(label);
  return button;
}

static bool glass_files_name(const char *name)
{
  /* The textarea's limit counts characters; the filesystem limit is bytes. */
  size_t length = strlen(name);
  if (!length || length >= GLASS_FILE_NAME_CAP ||
      !strcmp(name, ".") || !strcmp(name, "..")) return false;
  for (const unsigned char *p = (const unsigned char *)name; *p; p++)
    if (*p < 32 || *p == 127 || *p == '/' || *p == '\\') return false;
  return true;
}

/* Syntax only. The worker checks every existing component before using it. */
static bool glass_files_path_valid(const char *path)
{
  size_t root = strlen(GLASS_FILES_ROOT);
  if (strlen(path) >= GLASS_FILE_PATH_CAP ||
      strncmp(path, GLASS_FILES_ROOT, root) ||
      (path[root] && path[root] != '/')) return false;
  for (size_t i = root; path[i]; )
    {
      const char *component = path + ++i;
      size_t length = strcspn(component, "/");
      if (!length || length >= GLASS_FILE_NAME_CAP ||
          (length == 1 && component[0] == '.') ||
          (length == 2 && !strncmp(component, "..", 2))) return false;
      for (size_t j = 0; j < length; j++)
        if ((unsigned char)component[j] < 32 || component[j] == 127 ||
            component[j] == '\\') return false;
      i += length;
    }
  return true;
}

static void glass_files_refresh(lv_event_t *e)
{
  LV_UNUSED(e);
  if (!g_files_pending && lv_async_call(glass_files_render, NULL) == LV_RESULT_OK)
    g_files_pending = true;
}

static void glass_files_cancel(lv_event_t *e)
{
  LV_UNUSED(e);
  /* Cancellation must remain available while any request is pending. */
  glass_files_cancel_calls();
  glass_files_stop();
  glass_files_refresh(NULL);
}

static void glass_files_error(int error)
{
  snprintf(g_files_error, sizeof(g_files_error), "%s (%d)", strerror(error), error);
}

static void glass_files_list_ready(struct glass_file_job *job,
                                   const struct glass_file_result *result)
{
  glass_files_list_clear();
  g_files_list_job = job;
  lv_obj_t *list = g_files_browser.list;
  size_t shown = result ? result->count : 0;
  for (size_t i = 0; i < shown; i++)
    {
      const struct glass_file_entry *entry = &result->entries[i];
      lv_obj_t *row = glass_button(list, entry->name, 8, i * 52,
                                  820, theme_surface(), NULL, NULL);
      lv_obj_set_style_radius(row, 4, 0);
      lv_obj_set_user_data(row, (void *)entry);
      lv_obj_add_event_cb(row, glass_files_select, LV_EVENT_SHORT_CLICKED, NULL);
      lv_obj_add_event_cb(row, glass_files_select, LV_EVENT_LONG_PRESSED, NULL);
      lv_obj_t *label = lv_obj_get_child(row, 0);
      lv_obj_set_width(label, 745);
      lv_obj_align(label, LV_ALIGN_LEFT_MID, 48, 0);
      lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
      glass_symbol(row, entry->directory ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE,
                   12, 12, theme_primary());
    }
  if (!shown)
    glass_text(list, result ? "目录为空" : "目录无法读取",
               12, 12, 800, 20, theme_secondary());
  lv_obj_remove_state(g_files_browser.previous, LV_STATE_DISABLED);
  lv_obj_remove_state(g_files_browser.next, LV_STATE_DISABLED);
  if (!g_files_page) lv_obj_add_state(g_files_browser.previous, LV_STATE_DISABLED);
  if (!result || !result->has_next)
    lv_obj_add_state(g_files_browser.next, LV_STATE_DISABLED);
  char footer[160];
  if (result && !result->space_error)
    snprintf(footer, sizeof(footer), "第 %llu 页 · 可用 %llu KiB",
             (unsigned long long)g_files_page + 1,
             (unsigned long long)(result->free_bytes / 1024));
  else snprintf(footer, sizeof(footer), "第 %llu 页",
                (unsigned long long)g_files_page + 1);
  lv_label_set_text(g_files_browser.footer, footer);
  lv_label_set_text(g_files_browser.error, g_files_error);
}

static void glass_files_complete(const struct glass_file_status *status)
{
  struct glass_file_job *job = g_files_job;
  enum glass_file_action action = g_files_request.action;
  const struct glass_file_result *result = glass_file_get_result(job);
  g_files_job = NULL;
  glass_files_stop();
  int error = status->error;
  if (!error && !result) error = EIO;
  if (error) glass_files_error(error);
  else if (action != GLASS_FILE_LIST) g_files_error[0] = 0;
  if (status->cleanup_error)
    snprintf(g_files_error, sizeof(g_files_error), "%s (%d)，清理失败 (%d)",
             strerror(error), error, status->cleanup_error);
  if (action == GLASS_FILE_LIST)
    {
      glass_files_list_ready(error ? NULL : job, error ? NULL : result);
      if (error) glass_file_release(job);
      return;
    }
  if (action == GLASS_FILE_DETAILS && !error)
    {
      glass_files_details_ready(result);
      /* LVGL copies the preview; no result remains owned by this view. */
      glass_file_release(job);
      return;
    }
  if (!error && (action == GLASS_FILE_MOVE ||
      ((action == GLASS_FILE_RENAME || action == GLASS_FILE_REMOVE) &&
       !strcmp(g_files_clipboard, g_files_request.source))))
    {
      g_files_clipboard[0] = 0;
      g_files_clipboard_move = false;
    }
  glass_file_release(job);
  if (action != GLASS_FILE_DETAILS) g_files_page = 0;
  glass_files_refresh(NULL);
}

static int glass_files_submit(void)
{
  struct glass_file_request request =
    {
      .action = g_files_request.action,
      .root = GLASS_FILES_ROOT,
      .path = g_files_request.path,
      .source = g_files_request.source,
      .name = g_files_request.name,
      .page = g_files_request.page
    };
  int error = glass_file_submit(&request, &g_files_job);
  g_files_retry = error == EBUSY;
  return error;
}

static void glass_files_tick(lv_timer_t *timer)
{
  LV_UNUSED(timer);
  struct glass_file_status status = {0};
  if (g_files_retry)
    {
      int error = glass_files_submit();
      if (error == EBUSY) return;
      if (error)
        {
          status.error = error;
          glass_files_complete(&status);
          return;
        }
    }
  int error = glass_file_poll(g_files_job, &status);
  if (error) status.error = error;
  if (status.error || status.done)
    {
      glass_files_complete(&status);
      return;
    }
  if (g_files_copy_status && g_files_copy_bytes != status.bytes)
    {
      g_files_copy_bytes = status.bytes;
      lv_label_set_text_fmt(g_files_copy_status, "%llu bytes",
                           (unsigned long long)status.bytes);
    }
}

static void glass_files_begin(enum glass_file_action action, const char *path,
                              const char *source, const char *name)
{
  g_files_request.action = action;
  g_files_request.page = g_files_page;
  int error = 0;
  if (strlcpy(g_files_request.path, path ? path : "", sizeof(g_files_request.path)) >=
      sizeof(g_files_request.path) ||
      strlcpy(g_files_request.source, source ? source : "", sizeof(g_files_request.source)) >=
      sizeof(g_files_request.source) ||
      strlcpy(g_files_request.name, name ? name : "", sizeof(g_files_request.name)) >=
      sizeof(g_files_request.name)) error = ENAMETOOLONG;
  g_files_pending = true;
  if (!error)
    {
      g_files_timer = lv_timer_create(glass_files_tick, 100, NULL);
      if (!g_files_timer) error = ENOMEM;
    }
  if (!error) error = glass_files_submit();
  /* A cancelled worker may still be closing or syncing. Retry without I/O. */
  if (error && error != EBUSY)
    {
      struct glass_file_status status = {.error = error};
      glass_files_complete(&status);
    }
}

static void glass_files_copy_choose(lv_event_t *e)
{
  if (g_files_pending) return;
  g_files_clipboard_move = e && lv_event_get_user_data(e) != NULL;
  strlcpy(g_files_clipboard, g_files_selected, sizeof(g_files_clipboard));
  glass_files_refresh(NULL);
}

static void glass_files_copy_start(void *unused)
{
  LV_UNUSED(unused);
  char target[GLASS_FILE_PATH_CAP];
  const char *name = strrchr(g_files_clipboard, '/');
  if (!glass_files_path_valid(g_files_path) ||
      !glass_files_path_valid(g_files_clipboard) || !name ||
      !glass_files_name(name + 1))
    { glass_files_error(EINVAL); glass_files_render(NULL); return; }
  if (snprintf(target, sizeof(target), "%s/%s", g_files_path, name + 1) >=
      (int)sizeof(target))
    { glass_files_error(ENAMETOOLONG); glass_files_render(NULL); return; }
  lv_obj_t *card = glass_files_card(g_files_clipboard_move ? "移动文件" : "复制文件");
  if (!card) return;
  glass_text(card, target, 24, 80, 850, 20, theme_primary());
  g_files_copy_status = glass_text(card, "0 bytes", 24, 140, 850, 20, theme_secondary());
  g_files_copy_bytes = 0;
  glass_button(card, "取消", 24, 200, 100, theme_surface(), glass_files_cancel, NULL);
  glass_files_begin(g_files_clipboard_move ? GLASS_FILE_MOVE : GLASS_FILE_COPY,
                    g_files_path, g_files_clipboard, NULL);
}

static void glass_files_paste(lv_event_t *e)
{
  LV_UNUSED(e);
  if (!g_files_pending && lv_async_call(glass_files_copy_start, NULL) == LV_RESULT_OK)
    g_files_pending = true;
}

static void glass_files_clipboard_clear(lv_event_t *e)
{
  LV_UNUSED(e);
  g_files_clipboard[0] = 0;
  g_files_clipboard_move = false;
  glass_files_cancel(NULL);
}

static void glass_files_up(lv_event_t *e)
{
  LV_UNUSED(e);
  if (g_files_pending) return;
  if (strcmp(g_files_path, GLASS_FILES_ROOT))
    {
      char *slash = strrchr(g_files_path, '/');
      if (slash) *slash = 0;
    }
  g_files_page = 0;
  glass_files_refresh(NULL);
}

static void glass_files_page(lv_event_t *e)
{
  if (g_files_pending) return;
  if ((intptr_t)lv_event_get_user_data(e) > 0)
    {
      if (g_files_page == UINT_MAX) return;
      g_files_page++;
    }
  else if (g_files_page) g_files_page--;
  glass_files_refresh(NULL);
}

static void glass_files_apply(lv_event_t *e)
{
  LV_UNUSED(e);
  if (g_files_pending) return;
  if (g_files_operation == 3)
    {
      glass_files_begin(GLASS_FILE_REMOVE, NULL, g_files_selected, NULL);
      return;
    }
  if (!g_files_input) return;
  const char *name = lv_textarea_get_text(g_files_input);
  if (!glass_files_name(name))
    {
      glass_files_error(strlen(name) >= GLASS_FILE_NAME_CAP ? ENAMETOOLONG : EINVAL);
      glass_files_refresh(NULL);
      return;
    }
  if (g_files_operation == 1)
    glass_files_begin(GLASS_FILE_MKDIR, g_files_path, NULL, name);
  else
    {
      char parent[GLASS_FILE_PATH_CAP];
      strlcpy(parent, g_files_selected, sizeof(parent));
      char *slash = strrchr(parent, '/');
      if (!slash) { glass_files_error(EINVAL); glass_files_refresh(NULL); return; }
      *slash = 0;
      glass_files_begin(GLASS_FILE_RENAME, parent, g_files_selected, name);
    }
}

static void glass_files_dialog_async(void *unused)
{
  LV_UNUSED(unused);
  lv_obj_t *card = glass_files_card(g_files_operation == 1 ? "新建目录" :
                            g_files_operation == 2 ? "重命名" : "确认删除");
  if (!card) return;
  glass_text(card, g_files_operation == 1 ? g_files_path : g_files_selected,
             24, 72, 850, 20, theme_secondary());
  if (g_files_operation != 3)
    {
      g_files_input = lv_textarea_create(card);
      lv_obj_set_pos(g_files_input, 24, 110);
      lv_obj_set_size(g_files_input, 640, 48);
      lv_textarea_set_one_line(g_files_input, true);
      lv_textarea_set_max_length(g_files_input, GLASS_FILE_NAME_CAP - 1);
      const char *name = strrchr(g_files_selected, '/');
      if (g_files_operation == 2 && name) lv_textarea_set_text(g_files_input, name + 1);
      lv_obj_t *keyboard = lv_keyboard_create(card);
      lv_obj_set_size(keyboard, 850, 230);
      lv_obj_align(keyboard, LV_ALIGN_TOP_LEFT, 24, 210);
      lv_keyboard_set_textarea(keyboard, g_files_input);
    }
  glass_button(card, "取消", 680, 110, 90, theme_surface(), glass_files_cancel, NULL);
  glass_button(card, "确认", 780, 110, 90, theme_surface(), glass_files_apply, NULL);
}

static void glass_files_dialog(lv_event_t *e)
{
  if (g_files_pending) return;
  g_files_operation = (intptr_t)lv_event_get_user_data(e);
  if (lv_async_call(glass_files_dialog_async, NULL) == LV_RESULT_OK)
    g_files_pending = true;
}

static void glass_files_details_ready(const struct glass_file_result *result)
{
  lv_obj_t *card = glass_files_card("文件详情");
  if (!card) return;
  glass_text(card, g_files_selected, 24, 72, 850, 20, theme_primary());
  glass_button(card, "返回", 24, 110, 100, theme_surface(), glass_files_cancel, NULL);
  glass_button(card, "重命名", 140, 110, 120, theme_surface(), glass_files_dialog, (void *)2);
  glass_button(card, "删除", 280, 110, 100, theme_surface(), glass_files_dialog, (void *)3);
  char size[64];
  snprintf(size, sizeof(size), "%llu bytes", (unsigned long long)result->size);
  glass_text(card, size, 410, 118, 200, 20, theme_secondary());
  if (!result->directory)
    {
      glass_button(card, "复制", 630, 110, 100, theme_surface(), glass_files_copy_choose, NULL);
      glass_button(card, "移动", 750, 110, 100, theme_surface(), glass_files_copy_choose, (void *)1);
      lv_obj_t *preview = lv_textarea_create(card);
      lv_obj_set_pos(preview, 24, 170);
      lv_obj_set_size(preview, 850, 270);
      lv_textarea_set_text(preview, result->binary ? "二进制文件" : result->preview);
      lv_obj_remove_flag(preview, LV_OBJ_FLAG_CLICKABLE);
      if (result->truncated && !result->binary)
        glass_text(card, "仅预览前 2048 字节", 24, 446, 850, 16, theme_secondary());
    }
}

static void glass_files_details(void *unused)
{
  LV_UNUSED(unused);
  lv_obj_t *card = glass_files_card("文件详情");
  if (!card) return;
  glass_text(card, g_files_selected, 24, 72, 850, 20, theme_primary());
  glass_button(card, "返回", 24, 110, 100, theme_surface(), glass_files_cancel, NULL);
  glass_text(card, "正在读取", 24, 170, 850, 20, theme_secondary());
  glass_files_begin(GLASS_FILE_DETAILS, NULL, g_files_selected, NULL);
}

static void glass_files_select(lv_event_t *e)
{
  if (g_files_pending) return;
  const struct glass_file_entry *entry = lv_obj_get_user_data(lv_event_get_target(e));
  if (!entry || !glass_files_name(entry->name)) return;
  if (snprintf(g_files_selected, sizeof(g_files_selected), "%s/%s",
               g_files_path, entry->name) >= (int)sizeof(g_files_selected))
    { glass_files_error(ENAMETOOLONG); glass_files_refresh(NULL); return; }
  if (entry->directory && lv_event_get_code(e) == LV_EVENT_SHORT_CLICKED)
    {
      strlcpy(g_files_path, g_files_selected, sizeof(g_files_path));
      g_files_page = 0;
      glass_files_refresh(NULL);
    }
  else if (lv_async_call(glass_files_details, NULL) == LV_RESULT_OK)
    g_files_pending = true;
}

static void glass_files_render(void *unused)
{
  LV_UNUSED(unused);
  glass_files_cancel_calls();
  glass_files_stop();
  if (!g_files_browser.card)
    {
      lv_obj_t *card = glass_files_card("文件管理");
      if (!card) return;
      g_files_browser.card = card;
      /* The first content container becomes the retained browser view. */
      g_files_auxiliary = NULL;
      lv_obj_set_style_radius(card, 0, 0);
      g_files_browser.path = glass_text(card, "", 24, 68, 850, 20, theme_primary());
      g_files_browser.up = glass_files_icon(card, LV_SYMBOL_UP, 24, 104, glass_files_up, NULL);
      glass_files_icon(card, LV_SYMBOL_REFRESH, 100, 104, glass_files_refresh, NULL);
      glass_button(card, "新建目录", 180, 104, 130, theme_surface(), glass_files_dialog, (void *)1);
      g_files_browser.error = glass_text(card, "", 330, 112, 540, 16, theme_secondary());
      g_files_browser.cancel = glass_button(card, "取消", 630, 424, 90, theme_surface(), glass_files_clipboard_clear, NULL);
      g_files_browser.paste = glass_button(card, "粘贴", 740, 424, 120, theme_surface(), glass_files_paste, NULL);
      g_files_browser.list = glass_box(card, 24, 156, 850, 250, theme_surface(), 0);
      lv_obj_add_flag(g_files_browser.list, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_scroll_dir(g_files_browser.list, LV_DIR_VER);
      g_files_browser.previous = glass_files_icon(card, LV_SYMBOL_LEFT, 24, 424, glass_files_page, (void *)-1);
      g_files_browser.next = glass_files_icon(card, LV_SYMBOL_RIGHT, 110, 424, glass_files_page, (void *)1);
      g_files_browser.footer = glass_text(card, "", 210, 438, 400, 16, theme_secondary());
    }
  g_files_input = NULL;
  if (g_files_auxiliary)
    {
      lv_obj_clean(g_files_auxiliary);
      lv_obj_add_flag(g_files_auxiliary, LV_OBJ_FLAG_HIDDEN);
    }
  lv_obj_remove_flag(g_files_browser.card, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(g_files_title, "文件管理");
  lv_label_set_text(g_files_browser.path, g_files_path);
  lv_label_set_text(g_files_browser.error, g_files_error);
  lv_label_set_text(lv_obj_get_child(g_files_browser.paste, 0),
                    g_files_clipboard_move ? "移到此处" : "粘贴");
  if (g_files_clipboard[0])
    {
      lv_obj_remove_flag(g_files_browser.cancel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(g_files_browser.paste, LV_OBJ_FLAG_HIDDEN);
    }
  else
    {
      lv_obj_add_flag(g_files_browser.cancel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(g_files_browser.paste, LV_OBJ_FLAG_HIDDEN);
    }
  if (!strcmp(g_files_path, GLASS_FILES_ROOT))
    lv_obj_add_state(g_files_browser.up, LV_STATE_DISABLED);
  else lv_obj_remove_state(g_files_browser.up, LV_STATE_DISABLED);
  /* Reuse browser/list containers; release only disposable rows and data. */
  glass_files_list_clear();
  lv_obj_scroll_to_y(g_files_browser.list, 0, LV_ANIM_OFF);
  glass_text(g_files_browser.list, "正在读取目录", 12, 12, 800, 20, theme_secondary());
  lv_obj_add_state(g_files_browser.previous, LV_STATE_DISABLED);
  lv_obj_add_state(g_files_browser.next, LV_STATE_DISABLED);
  lv_label_set_text(g_files_browser.footer, "");
  glass_files_begin(GLASS_FILE_LIST, g_files_path, NULL, NULL);
}
