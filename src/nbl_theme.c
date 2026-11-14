/* NBL terminal palette. SPDX-License-Identifier: GPL-3.0-or-later */
#include "prototypes.h"
#include "nbl.h"

#include <string.h>

bool nbl_blue = TRUE;
bool nbl_use_line_numbers = TRUE, nbl_use_mouse = TRUE;
bool nbl_use_autoindent = TRUE, nbl_use_spaces = TRUE;

static bool colored;
static bool direct;
static int role_ids[NBL_ROLES];
static int background_id;
static const struct {
	int rgb;
	short ansi;
} palette[NBL_ROLES] = {
	[NBL_TEXT] =    { 0xE2E8F0, COLOR_WHITE },
	[NBL_PANE] =    { 0x111C30, COLOR_BLACK },
	[NBL_CURRENT] = { 0x1E3A5F, COLOR_BLUE },
	[NBL_ACCENT] =  { 0x3B82F6, COLOR_BLUE },
	[NBL_SOFT] =    { 0x60A5FA, COLOR_CYAN },
	[NBL_LIGHT] =   { 0x7DD3FC, COLOR_CYAN },
	[NBL_MUTED] =   { 0x94A3B8, COLOR_WHITE },
	[NBL_COMMENT] = { 0x64748B, COLOR_CYAN },
	[NBL_STRING] =  { 0xA5D6FF, COLOR_CYAN },
	[NBL_KEYWORD] = { 0x818CF8, COLOR_MAGENTA },
	[NBL_SUCCESS] = { 0x34D399, COLOR_GREEN },
	[NBL_ERROR] =   { 0xFB7185, COLOR_RED },
	[NBL_NUMBER] =  { 0xE5BD80, COLOR_YELLOW },
	[NBL_SELECTION] = { 0x28466D, COLOR_BLUE },
	[NBL_BACKGROUND] = { 0x0B1220, COLOR_BLACK },
};

#define NBL_BG  (-32)
#define NBL_ROLE_COLOR(role) (-40 - (role))
#define CURRENT_PAIR_OFFSET 128

void nbl_init_terminal(void)
{
#ifdef NCURSES_EXT_COLORS
	const char *term = getenv("TERM");
	const char *hint = getenv("COLORTERM");

	/* Use the equivalent direct-color terminfo entry for xterm-compatible
	 * sessions. Keep TERM intact for child programs. */
	const char *rgb_term = NULL;

	if (term && strcmp(term, "xterm-256color") == 0)
		rgb_term = "xterm-direct";
	else if (term && strcmp(term, "konsole-256color") == 0)
		rgb_term = "konsole-direct";
	if (rgb_term && hint && (strcmp(hint, "truecolor") == 0 || strcmp(hint, "24bit") == 0) &&
			!getenv("NO_COLOR") && newterm(rgb_term, stdout, stdin))
		return;
#endif
	if (!initscr())
		exit(1);
}

static int rgb_from_index(int index)
{
	static const int cube[] = {0, 95, 135, 175, 215, 255};

	if (index < 16)
		return index;
	if (index >= 232)
		return (8 + 10 * (index - 232)) * 0x010101;
	index -= 16;
	return cube[index / 36] * 65536 + cube[index / 6 % 6] * 256 + cube[index % 6];
}

static int nearest_index(int rgb)
{
	int best = 16, distance = 3 * 256 * 256;

	for (int index = 16; index < 256; index++) {
		int candidate = rgb_from_index(index);
		int dr = (rgb >> 16) - (candidate >> 16);
		int dg = (rgb >> 8 & 255) - (candidate >> 8 & 255);
		int db = (rgb & 255) - (candidate & 255);
		int score = dr * dr + dg * dg + db * db;

		if (score < distance) {
			best = index;
			distance = score;
		}
	}
	return best;
}

static int terminal_color(int rgb, short ansi)
{
	/* Direct-color terminfo reserves values 0–7 for ANSI colors. */
	if (direct)
		return rgb < 8 ? 8 : rgb;
	return COLORS >= 256 ? nearest_index(rgb) : ansi;
}

short nbl_named_color(const char *name)
{
	static const char *const names[] = {
		"nbltext", "nblpane", "nblcurrent", "nblaccent", "nblsoft",
		"nbllight", "nblmuted", "nblcomment", "nblstring", "nblkeyword",
		"nblsuccess", "nblerror", "nblnumber", "nblselection", "nblbackground"
	};

	for (int role = 0; role < NBL_ROLES; role++)
		if (strcmp(name, names[role]) == 0)
			return NBL_ROLE_COLOR(role);
	return -1;
}

int nbl_color_id(short color)
{
	if (color <= NBL_ROLE_COLOR(0) && color >= NBL_ROLE_COLOR(NBL_ROLES - 1)) {
		int role = -40 - color;
		return role_ids[role];
	}
	if (color == NBL_BG)
		return background_id;
	if (direct && color >= 8 && color < 256) {
		if (color < 16) {
			static const int bright[] = {
				0x808080, 0xFF0000, 0x00FF00, 0xFFFF00,
				0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF
			};
			return bright[color - 8];
		}
		return rgb_from_index(color);
	}
	return color;
}

static void pair_colors(short pair, short fg, short bg)
{
	int fore = nbl_color_id(fg), back = nbl_color_id(bg);

	if (pair >= COLOR_PAIRS)
		return;
#ifdef NCURSES_EXT_COLORS
	if (direct) {
		init_extended_pair(pair, fore, back);
		return;
	}
#endif
	init_pair(pair, fore, back);
}

bool nbl_highlight_current(void)
{
	return colored && !inhelp;
}

int nbl_background(void)
{
	return colored ? NBL_BG : THE_DEFAULT;
}

void nbl_init_pair(short pair, short fg, short bg, bool current)
{
	short base = (colored && bg == THE_DEFAULT) ? NBL_BG : bg;

	pair_colors(pair, fg, base);
	if (current && colored && bg == THE_DEFAULT &&
			pair < CURRENT_PAIR_OFFSET && pair + CURRENT_PAIR_OFFSET < COLOR_PAIRS)
		pair_colors(pair + CURRENT_PAIR_OFFSET, fg, NBL_ROLE_COLOR(NBL_CURRENT));
}

int nbl_current_ink(int attributes, short pair)
{
	if (pair < CURRENT_PAIR_OFFSET && pair + CURRENT_PAIR_OFFSET < COLOR_PAIRS)
		return (attributes & ~A_COLOR) | COLOR_PAIR(pair + CURRENT_PAIR_OFFSET);
	return attributes;
}

void nbl_init_theme(void)
{
	direct = COLORS >= 0x1000000;
	for (int role = 0; role < NBL_ROLES; role++) {
		int rgb = nbl_theme_rgb[role] < 0 ? palette[role].rgb : nbl_theme_rgb[role];
		if (nbl_theme_rgb[NBL_ACCENT] >= 0 &&
				(role == NBL_SOFT || role == NBL_LIGHT || role == NBL_SUCCESS))
			rgb = nbl_theme_rgb[NBL_ACCENT];
		if (role == NBL_PANE && nbl_theme_rgb[NBL_BACKGROUND] >= 0)
			rgb = nbl_theme_rgb[NBL_BACKGROUND];
		if (role == NBL_MUTED && nbl_theme_rgb[NBL_TEXT] >= 0)
			rgb = nbl_theme_rgb[NBL_TEXT];
		role_ids[role] = terminal_color(rgb, palette[role].ansi);
	}
	background_id = role_ids[NBL_BACKGROUND];
	colored = nbl_blue && !rescind_colors && has_colors() && COLORS >= 8 &&
				COLOR_PAIRS > NBL_STATUS_PAIR;
	if (!colored)
		return;

	pair_colors(NBL_EDITOR_PAIR, NBL_ROLE_COLOR(NBL_TEXT), NBL_BG);
	pair_colors(NBL_CURRENT_PAIR, NBL_ROLE_COLOR(NBL_TEXT), NBL_ROLE_COLOR(NBL_CURRENT));
	pair_colors(NBL_PANE_PAIR, NBL_ROLE_COLOR(NBL_MUTED), NBL_ROLE_COLOR(NBL_PANE));
	pair_colors(NBL_FOCUS_PAIR, NBL_ROLE_COLOR(NBL_LIGHT), NBL_ROLE_COLOR(NBL_PANE));
	pair_colors(NBL_DIVIDER_PAIR, NBL_ROLE_COLOR(NBL_SOFT), NBL_ROLE_COLOR(NBL_PANE));
	pair_colors(NBL_SCROLL_PAIR, NBL_ROLE_COLOR(NBL_MUTED), NBL_ROLE_COLOR(NBL_CURRENT));
	pair_colors(NBL_SELECTED_PAIR, NBL_ROLE_COLOR(NBL_TEXT), NBL_ROLE_COLOR(NBL_SELECTION));
	pair_colors(NBL_CURRENT_NUMBER_PAIR, NBL_ROLE_COLOR(NBL_LIGHT), NBL_ROLE_COLOR(NBL_CURRENT));
	pair_colors(NBL_WARNING_PAIR, NBL_ROLE_COLOR(NBL_NUMBER), NBL_ROLE_COLOR(NBL_PANE));
	pair_colors(NBL_ERROR_ROW_PAIR, NBL_ROLE_COLOR(NBL_ERROR), NBL_BG);
	pair_colors(NBL_WARNING_ROW_PAIR, NBL_ROLE_COLOR(NBL_NUMBER), NBL_BG);
	pair_colors(NBL_STATUS_PAIR, NBL_ROLE_COLOR(NBL_SUCCESS), NBL_ROLE_COLOR(NBL_PANE));
}

void nbl_theme_windows(void)
{
	if (!colored)
		return;
	wbkgdset(midwin, ' ' | COLOR_PAIR(NBL_EDITOR_PAIR));
	wbkgdset(footwin, ' ' | COLOR_PAIR(NBL_PANE_PAIR));
	if (topwin)
		wbkgdset(topwin, ' ' | COLOR_PAIR(NBL_PANE_PAIR));
	werase(midwin);
	werase(footwin);
	if (topwin)
		werase(topwin);
}

void nbl_status(void)
{
	char position[64];

	if (ISSET(ZERO) || ISSET(MINIBAR) || LINES < 3 || lastmessage != VACUUM ||
			ISSET(CONSTANT_SHOW) || waiting_keycodes())
		return;

	snprintf(position, sizeof(position), "Ln %zd  Col %zu ", openfile->current->lineno,
			 xplustabs() + 1);
	int width = strlen(position);
	if (width >= COLS)
		return;

	blank_statusbar();
	wattrset(footwin, colored ? COLOR_PAIR(NBL_PANE_PAIR) : A_NORMAL);
	mvwaddstr(footwin, 0, COLS - width, position);
	const char *language = nbl_language_label();
	const char *state = openfile->modified ? _("Modified") :
			*openfile->filename ? _("Saved") : _("New Buffer");
	int label_width = COLS - width - breadth(language) - breadth(state) - 8;
	if (label_width > 3) {
		const char *name = *openfile->filename ? tail(openfile->filename) : _("Untitled");
		char *label = display_string(name, 0, label_width, FALSE, FALSE);
		mvwprintw(footwin, 0, 1, "%s  %s  ", label, language);
		free(label);
		if (colored && !openfile->modified)
			wattrset(footwin, COLOR_PAIR(NBL_STATUS_PAIR));
		waddstr(footwin, state);
	}
	wattrset(footwin, A_NORMAL);
	wnoutrefresh(footwin);
}

/* Keep the normal title concise. Upstream renders its special menus. */
bool nbl_title(void)
{
	if (inhelp || currmenu == MLINTER || ISSET(STATEFLAGS) || COLS < 26)
		return FALSE;

	const char *name = *openfile->filename ? tail(openfile->filename) : _("New Buffer");
	const char *state = ISSET(VIEW_MODE) ? _("View") :
			openfile->modified ? _("Modified") : ISSET(RESTRICTED) ? _("Restricted") : "";
	char left[32] = "NBL";
	if (openfile->next != openfile)
		snprintf(left, sizeof(left), "NBL  %i/%i", buffer_number(openfile), buffer_number(startfile->prev));
	int name_start = strlen(left) + 5;
	int state_width = breadth(state);
	int room = COLS - name_start - state_width - 3;
	if (room < 1)
		return FALSE;

	wattrset(topwin, interface_color_pair[TITLE_BAR]);
	blank_titlebar();
	mvwaddstr(topwin, 0, 2, left);
	char *label = display_string(name, 0, room, FALSE, FALSE);
	mvwaddstr(topwin, 0, name_start, label);
	free(label);
	if (state_width)
		mvwaddstr(topwin, 0, COLS - state_width - 2, state);
	wattrset(topwin, A_NORMAL);
	wnoutrefresh(topwin);
	return TRUE;
}

int nbl_problem_style(int severity, bool row)
{
	if (!colored)
		return row ? A_UNDERLINE : A_BOLD;
	return severity == 2 ? (row ? COLOR_PAIR(NBL_ERROR_ROW_PAIR) : interface_color_pair[ERROR_MESSAGE]) :
			COLOR_PAIR(row ? NBL_WARNING_ROW_PAIR : NBL_WARNING_PAIR);
}
