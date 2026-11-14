/* Code-aware typing and automatic closer provenance. GPL-3.0-or-later. */
#include "prototypes.h"
#include "nbl.h"

#include <ctype.h>
#include <string.h>

struct closer {
	ssize_t line;
	size_t column;
	char character;
};

struct nbl_closers {
	size_t count;
	struct closer items[];
};

struct nbl_closers *nbl_copy_closers(void)
{
	struct nbl_closers *current = openfile->closers;

	if (!current || !current->count)
		return NULL;
	size_t bytes = sizeof(*current) + current->count * sizeof(*current->items);
	struct nbl_closers *copy = nmalloc(bytes);
	memcpy(copy, current, bytes);
	return copy;
}

void nbl_restore_closers(const struct nbl_closers *saved)
{
	free(openfile->closers);
	openfile->closers = NULL;
	if (saved) {
		size_t bytes = sizeof(*saved) + saved->count * sizeof(*saved->items);
		openfile->closers = nmalloc(bytes);
		memcpy(openfile->closers, saved, bytes);
	}
}

static void record_closer(ssize_t line, size_t col, char character)
{
	size_t count = openfile->closers ? openfile->closers->count : 0;

	if (count >= 128)
		return;
	struct nbl_closers *result = nrealloc(openfile->closers,
			sizeof(*result) + (count + 1) * sizeof(*result->items));
	result->items[count] = (struct closer){line, col, character};
	result->count = count + 1;
	openfile->closers = result;
}

static bool tracked(char character)
{
	const struct nbl_closers *pairs = openfile->closers;
	if (pairs)
		for (size_t i = 0; i < pairs->count; i++)
			if (pairs->items[i].line == openfile->current->lineno &&
					pairs->items[i].column == openfile->current_x &&
					pairs->items[i].character == character)
				return TRUE;
	return FALSE;
}

/* Move surviving closers for a known splice. Other mutations drop provenance. */
void nbl_closers_splice(size_t from, size_t removed, size_t inserted, bool split)
{
	struct nbl_closers *pairs = openfile->closers;
	openfile->closers_spliced = TRUE;
	if (!pairs)
		return;
	size_t kept = 0;
	ssize_t line = openfile->current->lineno;
	for (size_t i = 0; i < pairs->count; i++) {
		struct closer item = pairs->items[i];
		if (item.line == line && item.column >= from) {
			if (item.column < from + removed)
				continue;
			if (split) {
				item.line++;
				item.column = item.column - from + inserted;
			} else
				item.column = item.column - removed + inserted;
		} else if (split && item.line > line)
			item.line++;
		pairs->items[kept++] = item;
	}
	pairs->count = kept;
}

void nbl_closers_modified(void)
{
	if (!openfile->closers_spliced) {
		free(openfile->closers);
		openfile->closers = NULL;
	}
	openfile->closers_spliced = FALSE;
#ifndef NANO_TINY
	if (openfile->current_undo && openfile->last_action != OTHER)
		nbl_closers_finish();
#endif
}

void nbl_closers_finish(void)
{
	undostruct *undo = openfile->current_undo;
	if (undo) {
		free(undo->closers_after);
		undo->closers_after = nbl_copy_closers();
	}
}

static bool suffix(const char *name, const char *ending)
{
	size_t length = strlen(name), size = strlen(ending);
	return length >= size && strcmp(name + length - size, ending) == 0;
}

enum nbl_language nbl_language(void)
{
	const char *name = openfile->filename;
	if (suffix(name, ".py"))
		return NBL_PYTHON;
	if (suffix(name, ".c") || suffix(name, ".h"))
		return NBL_C;
	if (suffix(name, ".cpp") || suffix(name, ".hpp") || suffix(name, ".cc") || suffix(name, ".cxx"))
		return NBL_CPP;
	if (openfile->syntax && strcmp(openfile->syntax->name, "python") == 0)
		return NBL_PYTHON;
	return nbl_is_shell() ? NBL_SH : NBL_PLAIN;
}

struct lexical {
	char quote;
	bool triple, block, comment, escape;
	char last_code;
	size_t nesting;
};

static void scan(struct lexical *state, const char *text, size_t end, enum nbl_language language)
{
	bool c_style = language == NBL_C || language == NBL_CPP;
	state->last_code = 0;
	state->comment = FALSE;
	for (size_t i = 0; i < end; i++) {
		char ch = text[i], next = i + 1 < end ? text[i + 1] : 0;
		if (state->block) {
			if (ch == '*' && next == '/') {
				state->block = FALSE;
				i++;
			}
		} else if (state->escape) {
			state->escape = FALSE;
		} else if (ch == '\\' && !(language == NBL_SH && state->quote == '\'')) {
			state->escape = TRUE;
		} else if (state->quote) {
			if (ch == state->quote && (!state->triple ||
					(next == ch && i + 2 < end && text[i + 2] == ch))) {
				i += state->triple ? 2 : 0;
				state->quote = 0;
				state->triple = FALSE;
			}
		} else if (c_style && ch == '/' && next == '*') {
			state->block = TRUE;
			i++;
		} else if ((c_style && ch == '/' && next == '/') ||
				(language == NBL_PYTHON && ch == '#') ||
				(language == NBL_SH && ch == '#' &&
				 (i == 0 || strchr(" \t;|&()", text[i - 1])))) {
			state->comment = TRUE;
			break;
		} else if (ch == '"' || ch == '\'') {
			state->quote = ch;
			state->triple = language == NBL_PYTHON && next == ch &&
					i + 2 < end && text[i + 2] == ch;
			i += state->triple ? 2 : 0;
			state->last_code = ch;
		} else if (!isblank((unsigned char)ch)) {
			state->last_code = ch;
			if (language == NBL_PYTHON) {
				if (strchr("([{", ch))
					state->nesting++;
				else if (state->nesting && strchr(")]}", ch))
					state->nesting--;
			}
		}
	}
}

static struct lexical context(enum nbl_language language)
{
	struct lexical state = {0};
	if (language == NBL_PLAIN) {
		scan(&state, openfile->current->data, openfile->current_x, language);
		return state;
	}
	for (linestruct *line = openfile->filetop; line != openfile->current; line = line->next) {
		scan(&state, line->data, strlen(line->data), language);
		if (!state.triple && !state.escape && language != NBL_SH)
			state.quote = 0;
		state.escape = FALSE;
	}
	scan(&state, openfile->current->data, openfile->current_x, language);
	return state;
}

static char closing(char key)
{
	switch (key) {
		case '(': return ')';
		case '[': return ']';
		case '{': return '}';
		case '"':
		case '\'': return key;
		default: return 0;
	}
}

static bool special(char key)
{
	return key && strchr("()[]{}\"'", key);
}

static void type_special(char key)
{
	char *text = openfile->current->data;
	size_t col = openfile->current_x;
	enum nbl_language language = nbl_language();
	struct lexical state = context(language);

	if (text[col] == key && tracked(key) && !state.escape &&
			!state.comment && !state.block && (!state.quote || state.quote == key)) {
		openfile->current_x++;
		openfile->placewewant = xplustabs();
		openfile->last_action = OTHER;
		refresh_needed = TRUE;
		return;
	}
	char close = closing(key);
	bool at_word = is_word_char(text + col, FALSE) || text[col] == '_';
	bool apostrophe = key == '\'' && col && is_word_char(text + step_left(text, col), FALSE);
	if (!close || state.quote || state.comment || state.block || state.escape || at_word || apostrophe) {
		char single[] = {key, 0};
		inject(single, 1);
		return;
	}

	char pair[] = {key, close, 0};
	ssize_t line = openfile->current->lineno;
	openfile->last_action = OTHER;
	inject(pair, 2);
	if (openfile->current->lineno == line && openfile->current_x == col + 2 &&
			openfile->last_action == ADD) {
		openfile->current_x--;
		record_closer(line, col + 1, close);
		openfile->current_undo->auto_pair = TRUE;
		nbl_closers_finish();
		openfile->placewewant = xplustabs();
	}
	openfile->last_action = OTHER;
	refresh_needed = TRUE;
}

void nbl_type(char *text, size_t length)
{
	if (!nbl_auto_pairs || openfile->mark) {
		inject(text, length);
		return;
	}

	size_t from = 0;
	for (size_t i = 0; i <= length; i++) {
		if (i < length && !special(text[i]))
			continue;
		char key = text[i];
		if (i > from) {
			text[i] = 0;
			inject(text + from, i - from);
			text[i] = key;
		}
		if (i < length)
			type_special(key);
		from = i + 1;
	}
}

bool nbl_backspace_pair(void)
{
	size_t col = openfile->current_x;
	const char *text = openfile->current->data;
	if (!nbl_auto_pairs || openfile->mark || !col || !text[col] ||
			closing(text[col - 1]) != text[col] || !tracked(text[col]))
		return FALSE;

	openfile->last_action = OTHER;
	openfile->current_x++;
	do_backspace();
	do_backspace();
	openfile->current_undo->tail_x = col;
	openfile->last_action = OTHER;
	return TRUE;
}

size_t nbl_extra_indent(void)
{
	enum nbl_language language = nbl_language();
	char trigger = language == NBL_PYTHON ? ':' :
			language == NBL_C || language == NBL_CPP ? '{' : 0;
	if (!trigger)
		return 0;
	struct lexical state = context(language);
	return !state.quote && !state.block && !state.escape && !state.nesting &&
			state.last_code == trigger ?
			tabsize : 0;
}
