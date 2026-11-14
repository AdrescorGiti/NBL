/* Buffer selection and terminal clipboard. SPDX-License-Identifier: GPL-3.0-or-later */
#include "prototypes.h"
#include "nbl.h"

#include <string.h>

#ifdef ENABLE_MOUSE
static openfilestruct *drag_buffer;
static linestruct *anchor;
static size_t anchor_x;
static bool dragging;

void nbl_cancel_drag(void)
{
	drag_buffer = NULL;
	dragging = FALSE;
}

static void move_to_mouse(int y, int x)
{
	int row = y - getbegy(midwin);
	int column = x - getbegx(midwin) - margin;
	linestruct *line = openfile->edittop;
	size_t edge = ISSET(SOFTWRAP) ? openfile->firstcolumn : 0;

	if (row < 0)
		row = 0;
	if (row >= editwinrows)
		row = editwinrows - 1;
	if (column < 0)
		column = 0;
	if (column >= editwincols)
		column = editwincols - 1;
	go_forward_chunks(row, &line, &edge);
	if (!ISSET(SOFTWRAP))
		edge = united_sidescroll ? openfile->brink :
				line == openfile->current ? get_page_start(xplustabs()) : 0;

	openfile->current = line;
	openfile->current_x = actual_x(line->data, actual_last_column(edge, column));
	openfile->placewewant = xplustabs();
	refresh_needed = TRUE;
	keep_cutbuffer = FALSE;
}

bool nbl_editor_mouse(const MEVENT *event)
{
	if (currmenu != MMAIN || inhelp)
		return FALSE;

	bool inside = wenclose(midwin, event->y, event->x);
	bool press = (event->bstate & BUTTON1_PRESSED) != 0;
	bool motion = (event->bstate & REPORT_MOUSE_POSITION) != 0;
	bool release = (event->bstate & BUTTON1_RELEASED) != 0;

	if (press && inside && (!sidebar || event->x != getbegx(midwin) + getmaxx(midwin) - 1)) {
		nbl_focus_editor();
		move_to_mouse(event->y, event->x);
		openfile->mark = NULL;
		drag_buffer = openfile;
		anchor = openfile->current;
		anchor_x = openfile->current_x;
		dragging = FALSE;
		return TRUE;
	}
	if (drag_buffer != openfile || (!motion && !release))
		return FALSE;

	move_to_mouse(event->y, event->x);
	dragging |= anchor != openfile->current || anchor_x != openfile->current_x;
	openfile->mark = dragging ? anchor : NULL;
	openfile->mark_x = anchor_x;
	openfile->softmark = TRUE;
	if (release)
		nbl_cancel_drag();
	return TRUE;
}
#endif

#define CLIPBOARD_LIMIT (64 * 1024)

static bool send_clipboard(void)
{
	static const char alphabet[] =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	char *plain = nmalloc(CLIPBOARD_LIMIT);
	size_t size = 0;

	for (linestruct *line = cutbuffer; line; line = line->next) {
		size_t length = strlen(line->data);

		if (length + (line->next != NULL) > CLIPBOARD_LIMIT - size) {
			free(plain);
			return FALSE;
		}
		memcpy(plain + size, line->data, length);
		size += length;
		if (line->next)
			plain[size++] = '\n';
	}

	char *encoded = nmalloc(4 * ((size + 2) / 3) + 1);
	size_t used = 0;
	for (size_t i = 0; i < size; i += 3) {
		unsigned value = (unsigned char)plain[i] << 16;

		if (i + 1 < size)
			value |= (unsigned char)plain[i + 1] << 8;
		if (i + 2 < size)
			value |= (unsigned char)plain[i + 2];
		encoded[used++] = alphabet[value >> 18];
		encoded[used++] = alphabet[(value >> 12) & 63];
		encoded[used++] = i + 1 < size ? alphabet[(value >> 6) & 63] : '=';
		encoded[used++] = i + 2 < size ? alphabet[value & 63] : '=';
	}
	encoded[used] = '\0';

	printf("\x1b]52;c;%s\a", encoded);
	fflush(stdout);
	free(encoded);
	free(plain);
	return TRUE;
}

void nbl_copy_selection(void)
{
	if (!openfile->mark) {
		statusbar(_("Select text with the mouse or Shift+arrows first"));
		return;
	}

	/* Copy raw buffer text, including text scrolled offscreen. */
	copy_text();
	if (!cutbuffer)
		return;
	const char *term = getenv("TERM");
	if (term && strcmp(term, "dumb") != 0 && strcmp(term, "linux") != 0 &&
			strcmp(term, "vt100") != 0 && send_clipboard())
		statusbar(_("Copied selection"));
	else
		statusbar(_("Copied to the editor buffer"));
}
