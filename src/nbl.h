/* NBL interface additions. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NBL_H
#define NBL_H

#define NBL_UPSTREAM_VERSION "9.3"

enum nbl_role {
	NBL_TEXT, NBL_PANE, NBL_CURRENT, NBL_ACCENT, NBL_SOFT,
	NBL_LIGHT, NBL_MUTED, NBL_COMMENT, NBL_STRING, NBL_KEYWORD,
	NBL_SUCCESS, NBL_ERROR, NBL_NUMBER, NBL_SELECTION, NBL_BACKGROUND, NBL_ROLES
};

enum nbl_pair {
	NBL_EDITOR_PAIR = 32, NBL_CURRENT_PAIR, NBL_PANE_PAIR,
	NBL_FOCUS_PAIR, NBL_DIVIDER_PAIR, NBL_SCROLL_PAIR, NBL_SELECTED_PAIR,
	NBL_CURRENT_NUMBER_PAIR, NBL_WARNING_PAIR, NBL_ERROR_ROW_PAIR, NBL_WARNING_ROW_PAIR, NBL_STATUS_PAIR
};

enum nbl_language { NBL_PLAIN, NBL_SH, NBL_PYTHON, NBL_C, NBL_CPP };
enum nbl_language nbl_language(void);
extern int nbl_theme_rgb[NBL_ROLES];
extern bool nbl_highlight_line, nbl_auto_pairs, nbl_explorer_left, nbl_show_hidden;
extern bool nbl_terminal_top, nbl_terminal_enabled;
extern int nbl_terminal_height;
int nbl_check_config(void);
int nbl_init_config(void);
void nbl_config_start(bool ignore);
void nbl_reload_config(void);
const char *nbl_tool_command(enum nbl_language language);
void mouse_init(void);

extern bool nbl_blue;
extern bool nbl_use_line_numbers, nbl_use_mouse, nbl_use_autoindent, nbl_use_spaces;

void nbl_init_terminal(void);
void nbl_init_theme(void);
void nbl_theme_windows(void);
void nbl_status(void);
bool nbl_title(void);
short nbl_named_color(const char *name);
int nbl_color_id(short color);
int nbl_background(void);
void nbl_init_pair(short pair, short fg, short bg, bool current);
int nbl_current_ink(int attributes, short pair);
bool nbl_highlight_current(void);

extern bool nbl_explorer_visible;
extern int nbl_explorer_percent;

int nbl_editor_width(void);
void nbl_explorer_layout(int y);
void nbl_modal_layout(bool entering);
bool nbl_project_argument(const char *name);
void nbl_set_default_project(void);
bool nbl_explorer_set_root(const char *path);
void nbl_explorer_open_path(const char *name, bool explicit_path);
void nbl_file_action(int key, const char *root, const char *selected, bool is_directory);
void nbl_explorer_draw(void);
bool nbl_explorer_has_focus(void);
bool nbl_in_modal_layout(void);
void nbl_focus_editor(void);
bool nbl_explorer_key(int input);
#ifdef ENABLE_MOUSE
bool nbl_mouse_event(const MEVENT *event);
bool nbl_editor_mouse(const MEVENT *event);
void nbl_cancel_drag(void);
#endif
void nbl_toggle_explorer(void);
void nbl_toggle_focus(void);
void nbl_refresh_explorer(void);
void nbl_toggle_hidden(void);
void nbl_narrow_explorer(void);
void nbl_widen_explorer(void);

extern bool nbl_check_on_save, nbl_check_on_open;
bool nbl_is_shell(void);
bool nbl_is_bash(void);
const char *nbl_mentor_hint(const char *message);
const char *nbl_language_label(void);
void nbl_clear_diagnostics(void);
void nbl_mentor_check(void);
void nbl_next_diagnostic(void);
void nbl_mentor_after_save(void);
bool nbl_write_snapshot(FILE *stream);
bool nbl_collect_diagnostics(FILE *input, bool announce);
void nbl_check_buffer(bool announce);
size_t nbl_problem_count(void);
bool nbl_problem(size_t index, ssize_t *line, ssize_t *col, const char **text, bool *warning);
void nbl_jump_problem(size_t index);
int nbl_line_problem(ssize_t line);
int nbl_problem_style(int severity, bool row);
void nbl_complete_shell(void);
struct nbl_closers *nbl_copy_closers(void);
void nbl_restore_closers(const struct nbl_closers *saved);
void nbl_closers_splice(size_t from, size_t removed, size_t inserted, bool split);
void nbl_closers_modified(void);
void nbl_closers_finish(void);
void nbl_type(char *text, size_t length);
bool nbl_backspace_pair(void);
size_t nbl_extra_indent(void);

void nbl_copy_selection(void);

bool nbl_tools_available(void);
int nbl_tools_height(int edit_rows);
void nbl_tools_layout(int start, int height);
void nbl_tools_draw(void);
void nbl_tools_show_problems(void);
bool nbl_tools_has_focus(void);
bool nbl_tools_key(int input);
bool nbl_tools_mouse(const MEVENT *event);
void nbl_toggle_terminal(void);
void nbl_toggle_problems(void);
void nbl_run_script(void);
void nbl_relayout(void);

void nbl_terminal_poll(void);
bool nbl_terminal_active(void);
bool nbl_terminal_start(enum nbl_language language, FILE *snapshot, const char *directory,
		const char *name);
bool nbl_start_buffer(enum nbl_language language, FILE *snapshot, const char *directory,
		const char *name);
void nbl_exec_buffer(enum nbl_language language, const char *name);
void nbl_run_finished(bool failed);
void nbl_run_cleanup(void);
void nbl_run_forget(openfilestruct *buffer);
void nbl_compiler_problems(FILE *log, bool failed);
void nbl_terminal_resize(int rows, int cols);
void nbl_terminal_draw(WINDOW *window, bool focus);
void nbl_terminal_key(int key);
void nbl_terminal_paste(bool starting);
const char *nbl_terminal_status(void);
void nbl_terminal_stop(void);

#endif
