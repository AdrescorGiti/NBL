/* Coding toolbar, problems, and terminal split. SPDX-License-Identifier: GPL-3.0-or-later */
#include "prototypes.h"
#include "nbl.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

enum tool_view { CLOSED, TERMINAL, PROBLEMS };
static enum tool_view view;
static WINDOW *toolbar, *split;
static bool focus, paste, run_blocked;
static openfilestruct *problem_buffer;
static size_t selected, top;

static const struct {
	const char *label;
	void (*action)(void);
} actions[] = {
	{"F5 Run", nbl_run_script},
	{"F6 Terminal", nbl_toggle_terminal},
	{"F7 Files", nbl_toggle_explorer},
	{"F8 Focus", nbl_toggle_focus},
	{"F9 Copy", nbl_copy_selection},
	{"F10 Check", nbl_mentor_check},
	{"F11 Next", nbl_next_diagnostic},
	{"F12 Problems", nbl_toggle_problems},
	{"F4 Reload", nbl_reload_config},
};

bool nbl_tools_available(void)
{
	return !nbl_in_modal_layout() && !ISSET(ZERO) && !ISSET(RESTRICTED) &&
			LINES >= 9 && COLS >= 25;
}

int nbl_tools_height(int edit_rows)
{
	if (!nbl_tools_available())
		return 0;
	int height = nbl_terminal_height ? nbl_terminal_height : edit_rows / 3;
	if (height < 4)
		height = 4;
	if (!nbl_terminal_height && height > 13)
		height = 13;
	if (height > edit_rows - 4)
		height = edit_rows - 4;
	return view != CLOSED && (view != TERMINAL || nbl_terminal_enabled) && height >= 4 ? height + 1 : 1;
}

void nbl_tools_layout(int start, int height)
{
	if (toolbar)
		delwin(toolbar);
	if (split)
		delwin(split);
	toolbar = height ? newwin(1, COLS, start, 0) : NULL;
	split = height > 1 ? newwin(height - 1, COLS, start + 1, 0) : NULL;
	if (!split)
		focus = FALSE;
	if (split && view == TERMINAL)
		nbl_terminal_resize(height - 2, COLS);
}

void nbl_relayout(void)
{
	window_init();
	ensure_firstcolumn_is_aligned();
	draw_all_subwindows();
}

static void show_view(enum tool_view requested, bool want_focus)
{
	bool changed = view != requested;
	view = requested;
	focus = want_focus;
	if (want_focus)
		nbl_focus_editor();
	if (changed)
		nbl_relayout();
	if (!split)
		focus = FALSE;
}

void nbl_tools_show_problems(void)
{
	run_blocked = FALSE;
	problem_buffer = openfile;
	selected = top = 0;
	show_view(PROBLEMS, FALSE);
}

void nbl_toggle_problems(void)
{
	if (view == PROBLEMS)
		show_view(CLOSED, FALSE);
	else
		nbl_tools_show_problems();
}

void nbl_toggle_terminal(void)
{
	if (!nbl_terminal_enabled) {
		statusbar(_("Terminal disabled in config.toml"));
		return;
	}
	if (view == TERMINAL)
		show_view(CLOSED, FALSE);
	else
		show_view(TERMINAL, TRUE);
}

bool nbl_tools_has_focus(void)
{
	return split && focus;
}

static void print_tool_text(WINDOW *window, int row, int col, const char *text)
{
	int room = getmaxx(window) - col - 1;
	if (room <= 0)
		return;
	char *display = display_string(text, 0, room, FALSE, FALSE);
	mvwaddstr(window, row, col, display);
	free(display);
}

static int action_width(size_t i)
{
	return strlen(actions[i].label) + 2;
}

static void draw_toolbar(void)
{
	int col = 0;
	int base = nbl_highlight_current() ? COLOR_PAIR(NBL_PANE_PAIR) : A_NORMAL;

	wbkgdset(toolbar, ' ' | base);
	werase(toolbar);
	for (size_t i = 0; i < sizeof(actions) / sizeof(*actions); i++) {
		int width = action_width(i);
		if (col + width >= COLS)
			break;
		wattrset(toolbar, interface_color_pair[KEY_COMBO]);
		mvwaddstr(toolbar, 0, col + 1, actions[i].label);
		col += width;
	}
	leaveok(toolbar, TRUE);
	wnoutrefresh(toolbar);
}

static void draw_problems(void)
{
	size_t count = nbl_problem_count();
	if (problem_buffer != openfile || !count) {
		problem_buffer = openfile;
		run_blocked = FALSE;
		top = selected = 0;
	}
	int height = getmaxy(split) - 2;
	char header[96];
	snprintf(header, sizeof(header), "%s%s  %zu", focus ? "NBL Mentor / Problems *" : "NBL Mentor / Problems",
			run_blocked ? "  Run blocked" : "", count);
	if (run_blocked)
		wattrset(split, interface_color_pair[ERROR_MESSAGE] | A_BOLD);
	print_tool_text(split, 0, 1, header);
	if (!count) {
		top = selected = 0;
		wattrset(split, A_NORMAL);
		print_tool_text(split, 1, 2, "No problems reported");
		return;
	}
	if (selected >= count)
		selected = count - 1;
	if (top > selected)
		top = selected;
	if (selected >= top + height)
		top = selected + 1 - height;

	for (int i = 0; i < height && top + i < count; i++) {
		ssize_t line, column;
		const char *message;
		bool warning;
		char label[96];
		nbl_problem(top + i, &line, &column, &message, &warning);
		wattrset(split, (warning ? nbl_problem_style(1, FALSE) :
				interface_color_pair[ERROR_MESSAGE]) | (top + i == selected ? A_BOLD : 0));
		snprintf(label, sizeof(label), "%c %s  L%zd:%zd  ",
				top + i == selected ? '>' : ' ', warning ? "Warning" : "Error", line, column);
		mvwaddstr(split, i + 1, 1, label);
		print_tool_text(split, i + 1, 1 + strlen(label), message);
	}
	const char *message;
	ssize_t line, column;
	bool warning;
	if (nbl_problem(selected, &line, &column, &message, &warning)) {
		wattrset(split, interface_color_pair[FUNCTION_TAG]);
		print_tool_text(split, getmaxy(split) - 1, 1, nbl_mentor_hint(message));
	}
}

void nbl_tools_draw(void)
{
	if (toolbar)
		draw_toolbar();
	if (!split)
		return;

	int base = nbl_highlight_current() ? COLOR_PAIR(NBL_PANE_PAIR) : A_NORMAL;
	wbkgdset(split, ' ' | base);
	werase(split);
	wattrset(split, interface_color_pair[TITLE_BAR] | A_BOLD);
	if (view == PROBLEMS)
		draw_problems();
	else {
		char header[144];
		snprintf(header, sizeof(header), "Terminal%s  %s%s", focus ? " *" : "",
				nbl_terminal_status(), nbl_terminal_active() ? "  Ctrl+C interrupt" : "");
		print_tool_text(split, 0, 1, header);
		nbl_terminal_draw(split, focus);
	}

	if (focus && view == PROBLEMS)
		curs_set(0);
	leaveok(split, !focus || view != TERMINAL);
	wnoutrefresh(split);
}

bool nbl_tools_key(int input)
{
	if (!nbl_tools_has_focus())
		return FALSE;

	if (paste && view == TERMINAL) {
		if (input == END_OF_PASTE) {
			nbl_terminal_paste(FALSE);
			paste = FALSE;
		} else
			nbl_terminal_key(input);
		return TRUE;
	}
	functionptrtype action = func_from_key(input);
	if (action == nbl_run_script || action == nbl_toggle_terminal ||
			action == nbl_toggle_problems || action == nbl_reload_config)
		return FALSE;
	if (action == nbl_toggle_focus) {
		focus = FALSE;
		return TRUE;
	}
	if (view == TERMINAL) {
		if (!nbl_terminal_active()) {
			focus = FALSE;
			return action == NULL;
		}
		if (input == START_OF_PASTE) {
			paste = TRUE;
			nbl_terminal_paste(TRUE);
		} else
			nbl_terminal_key(input);
		return TRUE;
	}
	switch (input) {
		case KEY_UP: if (selected) selected--; return TRUE;
		case KEY_DOWN: if (selected + 1 < nbl_problem_count()) selected++; return TRUE;
		case KEY_ENTER:
		case '\r':
		case '\n':
			nbl_jump_problem(selected);
			focus = FALSE;
			return TRUE;
		case 0x03: focus = FALSE; return TRUE;
		default: return action == NULL;
	}
}

bool nbl_tools_mouse(const MEVENT *event)
{
	if (currmenu != MMAIN)
		return FALSE;
	bool release = (event->bstate & (BUTTON1_RELEASED | BUTTON1_CLICKED)) != 0;
	if (event->y >= getbegy(midwin) && event->y < getbegy(midwin) + editwinrows) {
		if (event->bstate & (BUTTON1_PRESSED | BUTTON1_RELEASED | BUTTON1_CLICKED))
			focus = FALSE;
		return FALSE;
	}
	if (toolbar && wenclose(toolbar, event->y, event->x)) {
		if (release) {
			int col = 0;
			for (size_t i = 0; i < sizeof(actions) / sizeof(*actions); i++) {
				int width = action_width(i);
				if (col + width >= COLS)
					break;
				if (event->x >= col && event->x < col + width) {
					actions[i].action();
					break;
				}
				col += width;
			}
		}
		return TRUE;
	}
	if (!split || !wenclose(split, event->y, event->x))
		return FALSE;
	if (release) {
		focus = TRUE;
		nbl_focus_editor();
		int row = event->y - getbegy(split) - 1;
		if (view == PROBLEMS && row >= 0 && row < getmaxy(split) - 2 && top + row < nbl_problem_count()) {
			selected = top + row;
			nbl_jump_problem(selected);
			focus = FALSE;
		}
	} else if (view == PROBLEMS) {
		if ((event->bstate & BUTTON4_PRESSED) && selected)
			selected--;
		if ((event->bstate & BUTTON5_PRESSED) && selected + 1 < nbl_problem_count())
			selected++;
	}
	return TRUE;
}

void nbl_run_script(void)
{
	if (in_restricted_mode())
		return;
	enum nbl_language language = nbl_language();
	if (language == NBL_PLAIN) {
		statusbar(_("Open a shell, Python, C, or C++ buffer to run it"));
		return;
	}
	if (nbl_terminal_active()) {
		show_view(TERMINAL, TRUE);
		statusbar(_("Script running. Press Ctrl+C in Terminal to interrupt."));
		return;
	}
	if (!nbl_terminal_enabled) {
		statusbar(_("Terminal disabled in config.toml"));
		return;
	}
	if (!nbl_tools_available() || LINES < 12) {
		statusline(MILD, _("Enlarge the terminal to run a script"));
		return;
	}

	FILE *snapshot = tmpfile();
	if (!snapshot || !nbl_write_snapshot(snapshot)) {
		statusline(ALERT, _("Cannot snapshot this shell buffer: %s"), strerror(errno));
		if (snapshot)
			fclose(snapshot);
		return;
	}
	if (language == NBL_SH && !nbl_collect_diagnostics(snapshot, TRUE)) {
		nbl_tools_show_problems();
		run_blocked = TRUE;
		fclose(snapshot);
		return;
	}
	if (fseek(snapshot, 0, SEEK_SET) != 0) {
		statusline(ALERT, _("Cannot rewind the script snapshot"));
		fclose(snapshot);
		return;
	}

	char *path = *openfile->filename ? get_full_path(openfile->filename) : NULL;
	char *directory = path ? copy_of(path) : copy_of(".");
	char *slash = strrchr(directory, '/');
	if (slash)
		slash[slash == directory ? 1 : 0] = '\0';

	show_view(TERMINAL, TRUE);
	if (!split)
		statusline(MILD, _("Enlarge the terminal to run a script"));
	else
		nbl_start_buffer(language, snapshot, directory, path ? path :
				(language == NBL_PYTHON ? "<nbl-buffer>" : "nbl-buffer"));
	free(path);
	free(directory);
	fclose(snapshot);
}
