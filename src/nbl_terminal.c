/* Integrated POSIX shell PTY. SPDX-License-Identifier: GPL-3.0-or-later */
#include "prototypes.h"
#include "nbl.h"

#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <stdint.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wchar.h>

#include "vterm.h"

#define INPUT_LIMIT 65536

static VTerm *terminal;
static VTermScreen *screen;
static pid_t child = -1;
static int master = -1, result;
static bool exited, cursor_visible;
static int rows = 8, columns = 80;
static unsigned char pending[INPUT_LIMIT];
static size_t queued;
static char status[80] = "F5 runs the current shell buffer";

static int set_terminal_property(VTermProp prop, VTermValue *value, void *unused)
{
	if (prop == VTERM_PROP_CURSORVISIBLE)
		cursor_visible = value->boolean;
	return 1;
}

static const VTermScreenCallbacks callbacks = {
	.settermprop = set_terminal_property,
};

static void flush_input(void)
{
	while (master >= 0 && queued) {
		ssize_t wrote = write(master, pending, queued);
		if (wrote > 0) {
			queued -= wrote;
			memmove(pending, pending + wrote, queued);
		} else if (wrote < 0 && errno == EINTR)
			continue;
		else
			break;
	}
}

static void queue_input(const char *bytes, size_t size, void *unused)
{
	flush_input();
	if (master < 0)
		return;
	if (size > INPUT_LIMIT - queued) {
		beep();
		return;
	}
	memcpy(pending + queued, bytes, size);
	queued += size;
	flush_input();
}

bool nbl_terminal_active(void)
{
	return child > 0 || master >= 0;
}

const char *nbl_terminal_status(void)
{
	return status;
}

void nbl_terminal_resize(int height, int width)
{
	if (height < 1 || width < 1 || (height == rows && width == columns))
		return;
	rows = height;
	columns = width;
	if (terminal)
		vterm_set_size(terminal, rows, columns);
	if (master >= 0) {
		struct winsize size = { .ws_row = rows, .ws_col = columns };
		ioctl(master, TIOCSWINSZ, &size);
	}
}

bool nbl_terminal_start(enum nbl_language language, FILE *snapshot, const char *directory,
		const char *name)
{
	if (nbl_terminal_active()) {
		statusline(MILD, _("A script is still running. Focus Terminal and press Ctrl+C to interrupt."));
		return FALSE;
	}

	struct winsize size = { .ws_row = rows, .ws_col = columns };
	int script = fileno(snapshot);

	fflush(stdout);
	child = forkpty(&master, NULL, NULL, &size);
	if (child < 0) {
		master = -1;
		statusline(ALERT, _("Cannot open a script terminal: %s"), strerror(errno));
		return FALSE;
	}
	if (child == 0) {
		signal(SIGINT, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
		signal(SIGTERM, SIG_DFL);
		signal(SIGHUP, SIG_DFL);
		signal(SIGTSTP, SIG_DFL);
		signal(SIGCONT, SIG_DFL);
		signal(SIGWINCH, SIG_DFL);
		if (dup2(script, 3) < 0 || fcntl(3, F_SETFD, 0) < 0 || chdir(directory) < 0) {
			dprintf(STDERR_FILENO, "NBL: %s\n", strerror(errno));
			_exit(126);
		}
		if (script != 3)
			close(script);
		setenv("TERM", "xterm-256color", 1);
		unsetenv("ENV");
		unsetenv("BASH_ENV");
		unsetenv("SHELLOPTS");
		unsetenv("BASHOPTS");
		nbl_exec_buffer(language, name);
		_exit(127);
	}

	fcntl(master, F_SETFD, FD_CLOEXEC);
	int flags = fcntl(master, F_GETFL);
	if (flags < 0 || fcntl(master, F_SETFL, flags | O_NONBLOCK) < 0) {
		nbl_terminal_stop();
		statusline(ALERT, _("Cannot set up nonblocking terminal I/O"));
		return FALSE;
	}
	if (terminal)
		vterm_free(terminal);
	terminal = vterm_new(rows, columns);
	vterm_set_utf8(terminal, using_utf8);
	vterm_output_set_callback(terminal, queue_input, NULL);
	screen = vterm_obtain_screen(terminal);
	vterm_screen_set_callbacks(screen, &callbacks, NULL);
	vterm_screen_enable_altscreen(screen, TRUE);
	vterm_screen_enable_reflow(screen, TRUE);
	vterm_screen_reset(screen, TRUE);
	queued = 0;
	exited = FALSE;
	cursor_visible = TRUE;
	snprintf(status, sizeof(status), "Running");
	return TRUE;
}

void nbl_terminal_poll(void)
{
	if (master >= 0) {
		char bytes[8192];
		flush_input();
		/* Bound work per frame so a noisy script cannot starve the editor. */
		for (int i = 0; i < 8; i++) {
			ssize_t got = read(master, bytes, sizeof(bytes));

			if (got > 0)
				vterm_input_write(terminal, bytes, got);
			else if (!got || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
				close(master);
				master = -1;
				break;
			} else
				break;
		}
	}
	if (child > 0 && waitpid(child, &result, WNOHANG) == child) {
		child = -1;
		exited = TRUE;
	}
	if (exited && master < 0) {
		exited = FALSE;
		if (WIFEXITED(result))
			snprintf(status, sizeof(status), "Exit %d", WEXITSTATUS(result));
		else if (WIFSIGNALED(result))
			snprintf(status, sizeof(status), "Signal %d", WTERMSIG(result));
		nbl_run_finished(!WIFEXITED(result) || WEXITSTATUS(result) != 0);
	}
}

void nbl_terminal_stop(void)
{
	if (master >= 0) {
		close(master);
		master = -1;
	}
	if (child > 0) {
		kill(-child, SIGHUP);
		kill(child, SIGKILL);
		while (waitpid(child, NULL, 0) < 0 && errno == EINTR)
			;
		child = -1;
	}
	if (terminal) {
		vterm_free(terminal);
		terminal = NULL;
		screen = NULL;
	}
	nbl_run_cleanup();
}

static int cell_color(const VTermScreenCell *cell)
{
	if (VTERM_COLOR_IS_INDEXED(&cell->fg)) {
		switch (cell->fg.indexed.idx % 8) {
			case 1: return interface_color_pair[ERROR_MESSAGE];
			case 2: return interface_color_pair[STATUS_BAR];
			case 4:
			case 6: return interface_color_pair[TITLE_BAR];
		}
	}
	return A_NORMAL;
}

void nbl_terminal_draw(WINDOW *window, bool focus)
{
	if (!screen)
		return;

	for (int y = 0; y < rows; y++)
		for (int x = 0; x < columns; x++) {
			VTermScreenCell cell = {0};
			if (!vterm_screen_get_cell(screen, (VTermPos){ y, x }, &cell) ||
					cell.chars[0] == 0 || cell.chars[0] == UINT32_MAX)
				continue;
			int style = cell_color(&cell);
			if (cell.attrs.bold)
				style |= A_BOLD;
			if (cell.attrs.reverse)
				style |= A_REVERSE;
			if (cell.attrs.underline)
				style |= A_UNDERLINE;
			wattrset(window, style);
			wmove(window, y + 1, x);
			if (using_utf8) {
				wchar_t text[VTERM_MAX_CHARS_PER_CELL + 1] = {0};

				for (size_t i = 0; i < VTERM_MAX_CHARS_PER_CELL && cell.chars[i]; i++)
					text[i] = cell.chars[i];
				waddwstr(window, text);
			} else
				waddch(window, cell.chars[0] < 128 ? cell.chars[0] : '?');
		}

	if (focus && cursor_visible && master >= 0) {
		VTermPos position;
		vterm_state_get_cursorpos(vterm_obtain_state(terminal), &position);
		wmove(window, position.row + 1, position.col);
		curs_set(1);
	} else if (focus)
		curs_set(0);
}

void nbl_terminal_key(int key)
{
	if (master < 0)
		return;

	VTermKey special = VTERM_KEY_NONE;
	VTermModifier mods = (shift_held ? VTERM_MOD_SHIFT : 0) | (meta_key ? VTERM_MOD_ALT : 0);
	switch (key) {
		case KEY_UP: special = VTERM_KEY_UP; break;
		case KEY_DOWN: special = VTERM_KEY_DOWN; break;
		case KEY_LEFT: special = VTERM_KEY_LEFT; break;
		case KEY_RIGHT: special = VTERM_KEY_RIGHT; break;
		case KEY_HOME: special = VTERM_KEY_HOME; break;
		case KEY_END: special = VTERM_KEY_END; break;
		case KEY_PPAGE: special = VTERM_KEY_PAGEUP; break;
		case KEY_NPAGE: special = VTERM_KEY_PAGEDOWN; break;
		case KEY_IC: special = VTERM_KEY_INS; break;
		case KEY_DC: special = VTERM_KEY_DEL; break;
		case KEY_BACKSPACE: special = VTERM_KEY_BACKSPACE; break;
		case KEY_ENTER: special = VTERM_KEY_ENTER; break;
		case CONTROL_LEFT: special = VTERM_KEY_LEFT; mods |= VTERM_MOD_CTRL; break;
		case CONTROL_RIGHT: special = VTERM_KEY_RIGHT; mods |= VTERM_MOD_CTRL; break;
		case CONTROL_UP: special = VTERM_KEY_UP; mods |= VTERM_MOD_CTRL; break;
		case CONTROL_DOWN: special = VTERM_KEY_DOWN; mods |= VTERM_MOD_CTRL; break;
		case CONTROL_DELETE: special = VTERM_KEY_DEL; mods |= VTERM_MOD_CTRL; break;
	}
	if (special != VTERM_KEY_NONE)
		vterm_keyboard_key(terminal, special, mods);
	else if (key >= 0 && key <= 255) {
		char bytes[] = { '\x1b', key };
		queue_input(bytes + !meta_key, meta_key ? 2 : 1, NULL);
	}
}

void nbl_terminal_paste(bool starting)
{
	if (master < 0)
		return;
	if (starting)
		vterm_keyboard_start_paste(terminal);
	else
		vterm_keyboard_end_paste(terminal);
}
