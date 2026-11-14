/* Native right-side project explorer. SPDX-License-Identifier: GPL-3.0-or-later */
#include "prototypes.h"
#include "nbl.h"
#include "nbl_tree.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#define MIN_EDITOR_WIDTH 40
#define MIN_EXPLORER_WIDTH 19
#define MAX_EXPLORER_WIDTH 80
#define HEADER_ROWS 3

bool nbl_explorer_visible = TRUE;
int nbl_explorer_percent = 24;

static nbl_node *root;
static WINDOW *panel;
static int panel_width, modal_depth;
static bool panel_focus, dirty = TRUE;
static size_t selected, first_visible, count, capacity;
static struct {
	nbl_node *node;
	bool continuation;
} *rows;
static char *buffer_name, *active_path;
static bool active_modified;

static int available_width(void)
{
	if (!nbl_explorer_visible || ISSET(RESTRICTED) || ISSET(ZERO) || modal_depth ||
			LINES < 12 || COLS < MIN_EDITOR_WIDTH + MIN_EXPLORER_WIDTH)
		return 0;
	int width = COLS * nbl_explorer_percent / 100;

	if (width < MIN_EXPLORER_WIDTH)
		width = MIN_EXPLORER_WIDTH;
	if (width > MAX_EXPLORER_WIDTH)
		width = MAX_EXPLORER_WIDTH;
	return width < COLS - MIN_EDITOR_WIDTH ? width : COLS - MIN_EDITOR_WIDTH;
}

bool nbl_in_modal_layout(void)
{
	return modal_depth > 0;
}

int nbl_editor_width(void)
{
	return COLS - available_width();
}

void nbl_explorer_layout(int y)
{
	if (panel)
		delwin(panel);
	panel_width = editwinrows >= HEADER_ROWS + 2 ? available_width() : 0;
	panel = panel_width ? newwin(editwinrows, panel_width, y, nbl_explorer_left ? 0 : COLS - panel_width) : NULL;
	if (!panel)
		panel_focus = FALSE;
	dirty = TRUE;
}

void nbl_modal_layout(bool entering)
{
	int before = nbl_editor_width();
	bool had_tools = nbl_tools_available();

	modal_depth += entering ? 1 : -1;
	if (before != nbl_editor_width() || had_tools != nbl_tools_available()) {
		window_init();
		if (openfile) {
			ensure_firstcolumn_is_aligned();
			refresh_needed = TRUE;
		}
	}
}

bool nbl_explorer_set_root(const char *path)
{
	char *expanded = expand_leading_tilde(path);
	char *real = realpath(expanded, NULL);
	struct stat info;

	free(expanded);
	if (!real || stat(real, &info) != 0 || !S_ISDIR(info.st_mode)) {
		free(real);
		return FALSE;
	}
#ifdef ENABLE_OPERATINGDIR
	if (operating_dir && outside_of_confinement(real, FALSE)) {
		free(real);
		return FALSE;
	}
#endif
	DIR *readable = opendir(real);
	if (!readable) {
		free(real);
		return FALSE;
	}
	closedir(readable);
	if (root) {
		nbl_tree_clear(root);
		free(root->path);
		free(root);
	}
	root = nbl_tree_root(real);
	count = selected = first_visible = 0;
	dirty = TRUE;
	return TRUE;
}

bool nbl_project_argument(const char *name)
{
	return !ISSET(RESTRICTED) && nbl_explorer_set_root(name);
}

void nbl_set_default_project(void)
{
	if (root || ISSET(RESTRICTED))
		return;
#ifdef ENABLE_OPERATINGDIR
	if (operating_dir && nbl_explorer_set_root(operating_dir))
		return;
#endif
	if (openfile->filename[0]) {
		char *path = get_full_path(openfile->filename);
		char *slash = path ? strrchr(path, '/') : NULL;

		if (slash) {
			slash[slash == path ? 1 : 0] = '\0';
			nbl_explorer_set_root(path);
		}
		free(path);
	}
	if (!root)
		nbl_explorer_set_root(".");
}

static void append_row(nbl_node *node, bool continuation)
{
	if (count == capacity) {
		capacity = capacity ? capacity * 2 : 64;
		rows = nrealloc(rows, capacity * sizeof(*rows));
	}
	rows[count].node = node;
	rows[count++].continuation = continuation;
}

static void append_children(nbl_node *node)
{
	if (!node->expanded)
		return;
	for (size_t i = 0; i < node->count; i++) {
		nbl_node *child = node->children[i];

		append_row(child, FALSE);
		append_children(child);
	}
	if (node->scan)
		append_row(node, TRUE);
}

static void rebuild_rows(nbl_node *keep)
{
	count = 0;
	if (root)
		append_children(root);
	if (keep)
		for (size_t i = 0; i < count; i++)
			if (!rows[i].continuation && rows[i].node == keep) {
				selected = i;
				break;
			}
	if (selected >= count)
		selected = count ? count - 1 : 0;
	dirty = TRUE;
}

static int view_rows(void)
{
	return editwinrows - HEADER_ROWS - 1;
}

static void reveal_selection(void)
{
	int space = view_rows();

	if (selected < first_visible)
		first_visible = selected;
	else if (selected >= first_visible + space)
		first_visible = selected - space + 1;
}

static void print_label(int row, int x, const char *label, int width)
{
	char *visible;

	if (width <= 0)
		return;
	visible = display_string(label, 0, width, FALSE, FALSE);
	mvwaddstr(panel, row, x, visible);
	free(visible);
}

static bool inside_project(const char *path)
{
	size_t size = strlen(root->path);

	return strcmp(root->path, "/") == 0 ||
			(strncmp(path, root->path, size) == 0 && path[size] == '/');
}

static void track_active_file(void)
{
	const char *name = openfile->filename;

	if (active_modified != openfile->modified) {
		active_modified = openfile->modified;
		dirty = TRUE;
	}
	if (buffer_name && strcmp(buffer_name, name) == 0)
		return;
	buffer_name = mallocstrcpy(buffer_name, name);
	free(active_path);
	active_path = *name ? get_full_path(name) : NULL;
	dirty = TRUE;
}

void nbl_explorer_draw(void)
{
	if (!panel)
		return;

	track_active_file();
	if (!dirty)
		return;
	dirty = FALSE;

	bool blue = nbl_highlight_current();
	int pane = blue ? COLOR_PAIR(NBL_PANE_PAIR) : A_NORMAL;
	int emphasis = blue ? COLOR_PAIR(NBL_FOCUS_PAIR) : A_BOLD;
	int divider = blue ? COLOR_PAIR(NBL_DIVIDER_PAIR) : A_DIM;
	int room = view_rows();
	char position[48];

	wbkgdset(panel, ' ' | pane);
	werase(panel);
	if (root && !root->loaded) {
		nbl_tree_scan(root, nbl_show_hidden);
		rebuild_rows(NULL);
		dirty = FALSE;
	}

	wattrset(panel, divider);
	for (int y = 0; y < editwinrows; y++)
		mvwaddstr(panel, y, nbl_explorer_left ? panel_width - 1 : 0, using_utf8 ? "│" : "|");

	wattrset(panel, emphasis | A_BOLD | (panel_focus && !blue ? A_REVERSE : 0));
	mvwaddstr(panel, 0, 2, panel_focus ? "Explorer *" : "Explorer");
	wattrset(panel, pane);
	if (root)
		print_label(1, 2, root->path[1] ? tail(root->path) : "/", panel_width - 3);

	if (!count)
		print_label(HEADER_ROWS, 2, !root ? "No project" :
				root->error ? strerror(root->error) : "Empty directory", panel_width - 3);

	if (first_visible >= count)
		first_visible = count ? count - 1 : 0;
	if (count <= (size_t)room)
		first_visible = 0;
	else if (first_visible + room > count)
		first_visible = count - room;

	for (int i = 0; i < room && first_visible + i < count; i++) {
		size_t index = first_visible + i;
		nbl_node *node = rows[index].node;
		bool current = active_path && strcmp(active_path, node->path) == 0;
		int row = HEADER_ROWS + i;
		int indent = node->depth ? 2 * (node->depth - 1) : 0;
		const char *mark = " ";

		if (indent > panel_width - 10)
			indent = panel_width - 10;
		if (rows[index].continuation) {
			mark = "+";
			indent += node->depth ? 2 : 0;
		} else if (node->kind == NBL_DIRECTORY)
			mark = node->error ? "!" : node->expanded ? "-" : ">";
		else if (current && active_modified)
			mark = "~";
		else if (node->kind == NBL_LINK)
			mark = "@";
		else if (current)
			mark = "*";

		if (index == selected)
			wattrset(panel, blue ? COLOR_PAIR(NBL_SELECTED_PAIR) |
					(panel_focus ? A_BOLD : 0) : A_REVERSE);
		else if (current || node->kind == NBL_DIRECTORY)
			wattrset(panel, emphasis);
		else
			wattrset(panel, pane);
		mvwhline(panel, row, 1, ' ', panel_width - (nbl_explorer_left ? 2 : 1));
		mvwaddstr(panel, row, 2 + indent, mark);
		print_label(row, 4 + indent, rows[index].continuation ? "Load more" : node->name,
				panel_width - 5 - indent);
	}

	wattrset(panel, pane);
	if (count > (size_t)room)
		snprintf(position, sizeof(position), "%zu-%zu / %zu", first_visible + 1,
				first_visible + room, count);
	else {
		const keystruct *key = first_sc_for(MMAIN, nbl_toggle_focus);
		if (key)
			snprintf(position, sizeof(position), "%s %s", key->keystr,
					panel_focus ? "Editor  ? Keys" : "Focus");
		else
			position[0] = '\0';
	}
	print_label(editwinrows - 1, 2, position, panel_width - 3);
	leaveok(panel, TRUE);
	wnoutrefresh(panel);
}

bool nbl_explorer_has_focus(void)
{
	return panel && panel_focus;
}

static void set_focus(bool explorer)
{
	panel_focus = explorer;
	dirty = TRUE;
}

void nbl_focus_editor(void)
{
	if (panel_focus)
		set_focus(FALSE);
}

void nbl_toggle_focus(void)
{
	if (panel)
		set_focus(!panel_focus);
}

void nbl_toggle_explorer(void)
{
	if (in_restricted_mode())
		return;
	nbl_explorer_visible = !nbl_explorer_visible;
	window_init();
	ensure_firstcolumn_is_aligned();
	draw_all_subwindows();
}

static void resize_explorer(int delta)
{
	int next = nbl_explorer_percent + delta;

	if (!panel || next < 15 || next > 60)
		return;
	nbl_explorer_percent = next;
	window_init();
	ensure_firstcolumn_is_aligned();
	draw_all_subwindows();
}

void nbl_narrow_explorer(void)
{
	resize_explorer(-3);
}

void nbl_widen_explorer(void)
{
	resize_explorer(3);
}

void nbl_refresh_explorer(void)
{
	if (!panel || !root)
		return;
	nbl_tree_clear(root);
	nbl_tree_scan(root, nbl_show_hidden);
	selected = first_visible = 0;
	rebuild_rows(NULL);
	if (root->error)
		statusline(AHEM, _("Cannot read project: %s"), strerror(root->error));
}

void nbl_toggle_hidden(void)
{
	if (!panel)
		return;
	nbl_show_hidden = !nbl_show_hidden;
	nbl_refresh_explorer();
}

void nbl_explorer_open_path(const char *name, bool explicit_path)
{
	struct stat info;
	char *path = realpath(name, NULL);

	if (!path || stat(path, &info) < 0) {
		statusline(AHEM, _("Cannot open %s: %s"), tail(name), strerror(errno));
		free(path);
		return;
	}
	if ((!explicit_path && !inside_project(path)) || !S_ISREG(info.st_mode)) {
		statusline(AHEM, _("Explorer opens regular files inside the project"));
		free(path);
		return;
	}
	if (access(path, R_OK) < 0) {
		statusline(AHEM, _("Cannot open %s: %s"), tail(name), strerror(errno));
		free(path);
		return;
	}

	openfilestruct *buffer = openfile;
	do {
		if (buffer->statinfo && buffer->statinfo->st_dev == info.st_dev &&
				buffer->statinfo->st_ino == info.st_ino) {
			char *existing = get_full_path(buffer->filename);
			bool same_path = existing && strcmp(path, existing) == 0;
			free(existing);
			if (same_path) {
				if (openfile != buffer) {
					openfile = buffer;
					redecorate_after_switch();
				}
				set_focus(FALSE);
				free(path);
				return;
			}
		}
		buffer = buffer->next;
	} while (buffer != openfile);

	/* Close an untouched launch buffer only after the file opened. */
	bool empty = openfile->next == openfile && !openfile->filename[0] &&
			!openfile->modified && openfile->totsize == 0;
	if (open_buffer(path, TRUE) && openfile != buffer && openfile->filename[0]) {
		if (empty) {
			openfilestruct *opened = openfile;
			openfile = buffer;
			close_buffer();
			openfile = opened;
		}
		prepare_for_display();
		currmenu = MMOST;
		set_focus(FALSE);
	}
	free(path);
}

static void activate(void)
{
	if (!count)
		return;
	nbl_node *node = rows[selected].node;

	if (rows[selected].continuation) {
		nbl_tree_scan(node, nbl_show_hidden);
		rebuild_rows(NULL);
	} else if (node->kind == NBL_DIRECTORY) {
		node->expanded = !node->expanded;
		if (node->expanded && !node->loaded)
			nbl_tree_scan(node, nbl_show_hidden);
		if (node->expanded && node->error)
			statusline(AHEM, _("Cannot read %s: %s"), node->name, strerror(node->error));
		rebuild_rows(node);
	} else
		nbl_explorer_open_path(node->path, FALSE);
}

bool nbl_explorer_key(int input)
{
	if (!nbl_explorer_has_focus())
		return FALSE;

	if (root && !meta_key && input > 0 && input < 128 && strchr("nNemdciugh?", input)) {
		nbl_node *node = count && !rows[selected].continuation ? rows[selected].node : NULL;
		char *base = copy_of(root->path);
		char *entry = node ? copy_of(node->path) : NULL;
		nbl_file_action(input, base, entry, node && node->kind == NBL_DIRECTORY);
		free(entry);
		free(base);
		return TRUE;
	}
	int page = view_rows();
	switch (input) {
		case 'r': if (meta_key) return FALSE; nbl_refresh_explorer(); return TRUE;
		case '.': if (meta_key) return FALSE; nbl_toggle_hidden(); return TRUE;
		case '-': if (meta_key) return FALSE; nbl_narrow_explorer(); return TRUE;
		case '+': if (meta_key) return FALSE; nbl_widen_explorer(); return TRUE;
		case KEY_UP: if (selected) selected--; break;
		case KEY_DOWN: if (selected + 1 < count) selected++; break;
		case KEY_HOME: selected = 0; break;
		case KEY_END: selected = count ? count - 1 : 0; break;
		case KEY_PPAGE: selected = selected > (size_t)page ? selected - page : 0; break;
		case KEY_NPAGE:
			if (count)
				selected = selected + page < count ? selected + page : count - 1;
			break;
		case KEY_LEFT:
			if (count && !rows[selected].continuation) {
				nbl_node *node = rows[selected].node;
				if (node->expanded)
					activate();
				else if (node->parent != root)
					rebuild_rows(node->parent);
			}
			break;
		case KEY_RIGHT:
			if (count && (rows[selected].continuation ||
					(rows[selected].node->kind == NBL_DIRECTORY &&
					 !rows[selected].node->expanded)))
				activate();
			else if (count && rows[selected].node->expanded &&
					rows[selected].node->count && selected + 1 < count)
				selected++;
			break;
		case KEY_ENTER:
		case '\n':
		case '\r':
			activate();
			break;
		case 0x03:
			set_focus(FALSE);
			return TRUE;
		default:
			return FALSE;
	}
	reveal_selection();
	dirty = TRUE;
	return TRUE;
}

#ifdef ENABLE_MOUSE
bool nbl_mouse_event(const MEVENT *event)
{
	bool click = (event->bstate & (BUTTON1_RELEASED | BUTTON1_CLICKED |
			BUTTON1_DOUBLE_CLICKED)) != 0;

	if (currmenu != MMAIN || !panel)
		return FALSE;
	if (wenclose(midwin, event->y, event->x) && click) {
		set_focus(FALSE);
		return FALSE;
	}
	if (!wenclose(panel, event->y, event->x))
		return FALSE;

	int row = event->y - getbegy(panel) - HEADER_ROWS;
	int room = view_rows();
#if NCURSES_MOUSE_VERSION >= 2
	if (event->bstate & (BUTTON4_PRESSED | BUTTON5_PRESSED)) {
		size_t limit = count > (size_t)room ? count - room : 0;

		if (event->bstate & BUTTON4_PRESSED)
			first_visible = first_visible > 3 ? first_visible - 3 : 0;
		else
			first_visible = first_visible + 3 < limit ? first_visible + 3 : limit;
		dirty = TRUE;
		return TRUE;
	}
#endif
	if (click) {
		set_focus(TRUE);
		if (row >= 0 && row < room && first_visible + row < count &&
				event->x > getbegx(panel)) {
			selected = first_visible + row;
			activate();
			reveal_selection();
		}
	}
	return TRUE;
}
#endif
