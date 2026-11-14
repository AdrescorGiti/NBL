/* Explicit POSIX shell completion. SPDX-License-Identifier: GPL-3.0-or-later */
#include "prototypes.h"
#include "nbl.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define COMPLETION_SCAN_LIMIT 10000

struct matches {
	const char *prefix;
	size_t prefix_length, common;
	char *first;
	bool ambiguous;
};

static void offer(struct matches *matches, const char *candidate)
{
	if (strncmp(candidate, matches->prefix, matches->prefix_length) != 0)
		return;
	if (!matches->first) {
		matches->first = copy_of(candidate);
		matches->common = strlen(candidate);
		return;
	}
	if (strcmp(matches->first, candidate) == 0)
		return;
	matches->ambiguous = TRUE;
	size_t length = matches->prefix_length;

	while (length < matches->common && matches->first[length] == candidate[length])
		length++;
	matches->common = length;
}

static bool variable_start(unsigned char c)
{
	return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool variable_char(unsigned char c)
{
	return variable_start(c) || (c >= '0' && c <= '9');
}

static void offer_variables(struct matches *matches)
{
	linestruct *last = openfile->current;

	for (linestruct *line = last; line && last->lineno - line->lineno < COMPLETION_SCAN_LIMIT;
			line = line->prev) {
		const char *start = line->data;
		size_t bytes = line == last ? openfile->current_x : strlen(start);
		size_t pos = 0;

		while (pos < bytes && isblank((unsigned char)start[pos]))
			pos++;
		if ((bytes - pos >= 7 && strncmp(start + pos, "export ", 7) == 0) ||
				(bytes - pos >= 9 && strncmp(start + pos, "readonly ", 9) == 0))
			pos += strcspn(start + pos, " \t");
		while (pos < bytes && isblank((unsigned char)start[pos]))
			pos++;
		if (pos >= bytes || !variable_start(start[pos]))
			continue;
		size_t from = pos++;
		while (pos < bytes && variable_char(start[pos]))
			pos++;
		if (pos < bytes && start[pos] == '=') {
			char *name = measured_copy(start + from, pos - from);
			offer(matches, name);
			free(name);
		}
	}
}

static void offer_directory(struct matches *matches, const char *path,
		bool commands, size_t *budget)
{
	DIR *directory = opendir(path);
	struct dirent *entry;

	if (!directory)
		return;
	while (*budget && (entry = readdir(directory)) != NULL) {
		const char *name = entry->d_name;
		struct stat info;

		(*budget)--;
		if ((name[0] == '.' && (matches->prefix[0] != '.' || !strcmp(name, ".") ||
				!strcmp(name, ".."))) || strchr(name, '\n') || strchr(name, '\r') ||
				strncmp(name, matches->prefix, matches->prefix_length) != 0 ||
				fstatat(dirfd(directory), name, &info, 0) != 0)
			continue;
		if (commands) {
			if (S_ISREG(info.st_mode) && faccessat(dirfd(directory), name, X_OK, 0) == 0)
				offer(matches, name);
		} else if (S_ISDIR(info.st_mode)) {
			char *directory_name = concatenate(name, "/");
			offer(matches, directory_name);
			free(directory_name);
		} else if (S_ISREG(info.st_mode))
			offer(matches, name);
	}
	closedir(directory);
}

static void offer_commands(struct matches *matches)
{
	const char *keywords[] = {
		"alias", "bg", "break", "case", "cd", "command", "continue", "do", "done",
		"echo", "elif", "else", "esac", "eval", "exec", "exit", "export", "false",
		"fc", "fg", "fi", "for", "getopts", "hash", "if", "in", "jobs", "kill",
		"printf", "pwd", "read", "readonly", "return", "set", "shift", "test",
		"then", "times", "trap", "true", "type", "ulimit", "umask", "unalias",
		"unset", "until", "wait", "while", NULL
	};
	const char *path = getenv("PATH");
	size_t budget = COMPLETION_SCAN_LIMIT;

	for (size_t i = 0; keywords[i]; i++)
		offer(matches, keywords[i]);

	while (path && budget) {
		const char *colon = strchr(path, ':');
		size_t length = colon ? (size_t)(colon - path) : strlen(path);
		char *entry = length ? measured_copy(path, length) : copy_of(".");

		offer_directory(matches, entry, TRUE, &budget);
		free(entry);
		path = colon ? colon + 1 : NULL;
	}
}

static char *buffer_directory(void)
{
	char *path = *openfile->filename ? get_full_path(openfile->filename) : NULL;
	char *slash = path ? strrchr(path, '/') : NULL;

	if (!slash) {
		free(path);
		return copy_of(".");
	}
	slash[slash == path ? 1 : 0] = '\0';
	return path;
}

static char *escape_suffix(const char *text, size_t length)
{
	char *escaped = nmalloc(2 * length + 1);
	size_t used = 0;

	for (size_t i = 0; i < length; i++) {
		if (strchr(" \t\\\"'`$&;|<>*?[](){}!#~", text[i]))
			escaped[used++] = '\\';
		escaped[used++] = text[i];
	}
	escaped[used] = '\0';
	return escaped;
}

void nbl_complete_shell(void)
{
	if (nbl_explorer_has_focus())
		return;
	if (!nbl_is_shell()) {
		complete_a_word();
		return;
	}

	const char *data = openfile->current->data;
	size_t end = openfile->current_x, start = end;
	size_t budget = COMPLETION_SCAN_LIMIT;

	while (start && !strchr(" \t\"'`;&|<>(){}=", data[start - 1]))
		start--;
	if (start == end) {
		statusbar(_("Type a shell word to complete"));
		return;
	}
	if (start && data[start - 1] == '{' && start >= 2 && data[start - 2] == '$')
		start -= 2;

	char *token = measured_copy(data + start, end - start);
	char *prefix = token;
	char *slash = strrchr(token, '/');
	bool variable = token[0] == '$';
	bool command = start == 0 || strspn(data, " \t") == start;
	bool path = !variable && (slash || !command);

	if (variable)
		prefix += (token[1] == '{') ? 2 : 1;
	else if (slash)
		prefix = slash + 1;

	struct matches matches = { .prefix = prefix, .prefix_length = strlen(prefix) };
	if (variable)
		offer_variables(&matches);
	else if (path) {
		char *folder;
		char *base = buffer_directory();

		if (slash) {
			char *partial = measured_copy(token, slash + 1 - token);
			char *expanded = expand_leading_tilde(partial);
			if (expanded[0] == '/')
				folder = copy_of(expanded);
			else {
				char *leading = concatenate(base, "/");
				folder = concatenate(leading, expanded);
				free(leading);
			}
			free(expanded);
			free(partial);
		} else
			folder = copy_of(base);
#ifdef ENABLE_OPERATINGDIR
		if (!operating_dir || !outside_of_confinement(folder, FALSE))
#endif
			offer_directory(&matches, folder, FALSE, &budget);
		free(folder);
		free(base);
	} else
		offer_commands(&matches);

	if (matches.first && using_utf8)
		while (matches.common > matches.prefix_length &&
				((unsigned char)matches.first[matches.common] & 0xC0) == 0x80)
			matches.common--;

	if (!matches.first)
		statusbar(_("No shell completion"));
	else if (matches.common == matches.prefix_length)
		statusbar(matches.ambiguous ? _("Multiple completions") : _("Already complete"));
	else {
		size_t size = matches.common - matches.prefix_length;
		const char *rest = matches.first + matches.prefix_length;
		char *suffix = path ? escape_suffix(rest, size) : measured_copy(rest, size);

		openfile->last_action = OTHER;
		inject(suffix, strlen(suffix));
		free(suffix);
		statusbar(matches.ambiguous ? _("Completed common prefix") : _("Completed"));
	}
	free(matches.first);
	free(token);
}
