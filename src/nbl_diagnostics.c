/* NBL Mentor local diagnostics. SPDX-License-Identifier: GPL-3.0-or-later */
#include "prototypes.h"
#include "nbl.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_DIAGNOSTICS 128
#define SNAPSHOT_LIMIT (4 * 1024 * 1024)
#define OUTPUT_LIMIT (64 * 1024)

struct diagnostic {
	ssize_t line, column;
	char *text;
	bool warning;
};

struct nbl_diagnostics {
	struct diagnostic items[MAX_DIAGNOSTICS];
	size_t count, selected;
};

bool nbl_check_on_save = TRUE;
bool nbl_check_on_open = TRUE;

bool nbl_is_bash(void)
{
	const char *name = openfile->filename, *dot = strrchr(name, '.');
	const char *line = openfile->filetop->data;
	if ((dot && !strcmp(dot, ".bash")) || !strcmp(tail(name), ".bashrc") ||
			!strcmp(tail(name), ".bash_profile"))
		return TRUE;
	if (strncmp(line, "#!", 2))
		return FALSE;
	while ((line = strstr(line, "bash")) != NULL) {
		if (strchr("/ \t", line[-1]) && strchr(" \t", line[4]))
			return TRUE;
		line += 4;
	}
	return FALSE;
}

bool nbl_is_shell(void)
{
	const char *name = openfile->filename;
	size_t length = strlen(name);
	const char *first = openfile->filetop->data;

	if (openfile->syntax && strcmp(openfile->syntax->name, "sh") == 0)
		return TRUE;
	if (nbl_is_bash() || (length >= 3 && strcmp(name + length - 3, ".sh") == 0))
		return TRUE;
	if (strncmp(first, "#!", 2) != 0)
		return FALSE;

	/* Match a shell interpreter token in the shebang, including env sh. */
	first += 2;
	while (*first == ' ' || *first == '\t')
		first++;
	size_t size = strcspn(first, " \t");
	const char *base = first;
	for (size_t i = 0; i < size; i++)
		if (first[i] == '/')
			base = first + i + 1;
	size -= base - first;
	if (size == 3 && memcmp(base, "env", 3) == 0) {
		base += strcspn(base, " \t");
		base += strspn(base, " \t");
		size = strcspn(base, " \t");
	}
	return (size == 2 && memcmp(base, "sh", 2) == 0) ||
			(size == 4 && memcmp(base, "dash", 4) == 0) ||
			(size == 4 && memcmp(base, "bash", 4) == 0);
}

void nbl_clear_diagnostics(void)
{
	struct nbl_diagnostics *result = openfile->diagnostics;

	if (!result)
		return;
	for (size_t i = 0; i < result->count; i++)
		free(result->items[i].text);
	free(result);
	openfile->diagnostics = NULL;
	refresh_needed = TRUE;
}

bool nbl_write_snapshot(FILE *stream)
{
	size_t total = 0;

	for (linestruct *line = openfile->filetop; line; line = line->next) {
		size_t length = strlen(line->data);

		if (length + 1 > SNAPSHOT_LIMIT - total) {
			errno = EFBIG;
			return FALSE;
		}
		if (strchr(line->data, '\n')) {
			errno = EILSEQ;
			return FALSE;
		}
		if (fwrite(line->data, 1, length, stream) != length ||
				(line->next && putc('\n', stream) == EOF))
			return FALSE;
		total += length + 1;
	}
	return fflush(stream) == 0;
}

enum check_tool { SHELL_SYNTAX, SHELLCHECK, PYTHON_SYNTAX, COMPILER_SYNTAX };

static void exec_checker(enum check_tool tool, enum nbl_language language, bool bash)
{
	const char *command = nbl_tool_command(language);

	switch (tool) {
	case SHELLCHECK:
		execlp("shellcheck", "shellcheck", "-s", bash ? "bash" : "sh",
				"-f", "gcc", "--norc", "-", (char *)NULL);
		break;
	case SHELL_SYNTAX:
		if (bash)
			execlp("bash", "bash", "--noprofile", "--norc", "-n", (char *)NULL);
		else
			execl("/bin/sh", "sh", "-n", (char *)NULL);
		break;
	case PYTHON_SYNTAX: {
		const char *check =
			"import sys\n"
			"try:\n"
			"    compile(sys.stdin.buffer.read(), '<buffer>', 'exec')\n"
			"except SyntaxError as error:\n"
			"    print(f'-:{error.lineno or 1}:{error.offset or 1}: error: {error.msg}')\n"
			"    sys.exit(1)\n";
		if (*command)
			execlp(command, command, "-I", "-S", "-c", check, (char *)NULL);
		else {
			execlp("python3", "python3", "-I", "-S", "-c", check, (char *)NULL);
			if (errno == ENOENT)
				execlp("python", "python", "-I", "-S", "-c", check, (char *)NULL);
		}
		break;
	}
	case COMPILER_SYNTAX:
		if (!*command)
			command = language == NBL_CPP ? "g++" : "gcc";
		execlp(command, command, "-x", language == NBL_CPP ? "c++" : "c",
				"-fsyntax-only", "-Wall", "-fdiagnostics-color=never", "-iquote", ".", "-", (char *)NULL);
		break;
	}
	_exit(errno == ENOENT ? 127 : 126);
}

/* All checkers operate on a private snapshot and receive fixed arguments. */
static int run_tool(FILE *input, FILE *output, enum check_tool tool)
{
	if (fseek(input, 0, SEEK_SET) < 0)
		return -1;

	enum nbl_language language = nbl_language();
	bool bash = nbl_is_bash();
	char *directory = NULL;
	if (tool == COMPILER_SYNTAX && openfile->filename[0]) {
		directory = get_full_path(openfile->filename);
		char *slash = directory ? strrchr(directory, '/') : NULL;
		if (slash)
			slash[slash == directory] = '\0';
	}
	pid_t child = fork();
	if (child != 0)
		free(directory);
	if (child < 0)
		return -1;
	if (child == 0) {
		struct rlimit output_size = { OUTPUT_LIMIT, OUTPUT_LIMIT };

		if (setrlimit(RLIMIT_FSIZE, &output_size) != 0 ||
				dup2(fileno(input), STDIN_FILENO) < 0 ||
				dup2(fileno(output), STDOUT_FILENO) < 0 ||
				dup2(fileno(output), STDERR_FILENO) < 0)
			_exit(126);
		fclose(input);
		fclose(output);
		setenv("LC_ALL", "C", 1);
		unsetenv("ENV");
		unsetenv("BASH_ENV");
		unsetenv("SHELLOPTS");
		unsetenv("BASHOPTS");
		unsetenv("SHELLCHECK_OPTS");
		setpgid(0, 0);
		if (directory && chdir(directory) < 0)
			_exit(126);
		exec_checker(tool, language, bash);
	}

	setpgid(child, child);
	int code;
	struct timespec start, now;
	const struct timespec delay = { .tv_nsec = 10000000 };

	clock_gettime(CLOCK_MONOTONIC, &start);
	for (;;) {
		pid_t waited = waitpid(child, &code, WNOHANG);
		if (waited == child)
			return WIFEXITED(code) ? WEXITSTATUS(code) : -1;
		if (waited < 0 && errno != EINTR)
			return -1;
		clock_gettime(CLOCK_MONOTONIC, &now);
		if (now.tv_sec - start.tv_sec > 3 ||
				(now.tv_sec - start.tv_sec == 3 && now.tv_nsec >= start.tv_nsec))
			break;
		nanosleep(&delay, NULL);
	}
	kill(-child, SIGKILL);
	kill(child, SIGKILL);
	while (waitpid(child, &code, 0) < 0 && errno == EINTR)
		;
	errno = ETIMEDOUT;
	return -1;
}

static void remember(const char *tool, const char *message, long line, long column, bool warning)
{
	struct nbl_diagnostics *result = openfile->diagnostics;
	struct diagnostic *item;

	if (result->count == MAX_DIAGNOSTICS)
		return;
	item = &result->items[result->count++];
	item->line = line > 0 ? line : 1;
	item->column = column > 0 ? column : 1;
	item->warning = warning;
	size_t size = strlen(tool) + strlen(message) + 3;
	item->text = nmalloc(size);
	snprintf(item->text, size, "%s: %s", tool, message);
}

static void read_diagnostics(FILE *output, const char *tool, bool structured)
{
	char *buffer = NULL;
	size_t size = 0;
	ssize_t length;

	rewind(output);
	while ((length = getline(&buffer, &size, output)) >= 0) {
		char *message = buffer;
		long line = 1, column = 1;
		bool warning = FALSE;

		while (length > 0 && (buffer[length - 1] == '\n' || buffer[length - 1] == '\r'))
			buffer[--length] = '\0';
		if (!length)
			continue;
		if (structured) {
			int offset = 0;
			const char *location = !strncmp(buffer, "<stdin>", 7) ? buffer + 7 :
					buffer[0] == '-' ? buffer + 1 : "";
			if (sscanf(location, ":%ld:%ld: %n", &line, &column, &offset) != 2 ||
					offset <= 0)
				continue;
			message = (char *)location + offset;
			if (strncmp(message, "note:", 5) == 0)
				continue;
			warning = strncmp(message, "error:", 6) != 0 &&
					strncmp(message, "fatal error:", 12) != 0;
		} else {
			char *marker = strstr(buffer, ": line ");
			if (marker) {
				char *end;
				line = strtol(marker + 7, &end, 10);
				if (*end == ':')
					message = end + 1;
				/* Bash points to EOF and names the unmatched opener. */
				char *origin = strstr(message, " command on line ");
				if (origin) {
					long opener = strtol(origin + strlen(" command on line "), NULL, 10);
					if (opener > 0)
						line = opener;
				}
			} else if ((marker = strchr(buffer, ':')) != NULL) {
				char *end;
				long number = strtol(marker + 1, &end, 10);
				if (number > 0 && *end == ':') {
					line = number;
					message = end + 1;
				}
			}
			message += strspn(message, " ");
		}
		remember(tool, message, line, column, warning);
	}
	free(buffer);
}

static void show_diagnostic(bool jump)
{
	struct nbl_diagnostics *result = openfile->diagnostics;
	struct diagnostic *item = &result->items[result->selected];

	if (jump) {
		goto_line_and_column(item->line, item->column, TRUE);
		refresh_needed = TRUE;
	}
	statusline(item->warning ? NOTICE : MILD, "[%zu/%zu] L%zd:%zd %s",
			result->selected + 1, result->count, item->line, item->column, item->text);
}

bool nbl_collect_diagnostics(FILE *input, bool announce)
{
	enum nbl_language language = nbl_language();
	enum check_tool checker = language == NBL_SH ? SHELL_SYNTAX :
			language == NBL_PYTHON ? PYTHON_SYNTAX : COMPILER_SYNTAX;
	const char *tool = language == NBL_SH ? (nbl_is_bash() ? "bash -n" : "sh -n") :
			language == NBL_PYTHON ? "Python" : "Compiler";
	nbl_clear_diagnostics();
	openfile->nbl_checked = TRUE;
	FILE *output = tmpfile();

	if (!output) {
		statusline(ALERT, _("Cannot check this buffer: %s"), strerror(errno));
		return FALSE;
	}

	openfile->diagnostics = nmalloc(sizeof(*openfile->diagnostics));
	openfile->diagnostics->count = openfile->diagnostics->selected = 0;

	int syntax = run_tool(input, output, checker);
	if (syntax >= 0 && syntax < 126) {
		read_diagnostics(output, tool, checker != SHELL_SYNTAX);
		if (syntax && !nbl_problem_count())
			remember(tool, _("Syntax check failed without a source location"), 1, 1, FALSE);
	} else
		remember(tool, syntax == 127 ? _("Checker not found") :
				_("Syntax checker did not finish"), 1, 1, FALSE);

	int static_check = 0;
	if (checker == SHELL_SYNTAX && syntax >= 0 && syntax < 126 &&
			fseek(output, 0, SEEK_SET) == 0 && ftruncate(fileno(output), 0) == 0) {
		static_check = run_tool(input, output, SHELLCHECK);
		if (static_check == 1)
			read_diagnostics(output, "ShellCheck", TRUE);
	}

	refresh_needed = TRUE;
	bool okay = syntax == 0;
	for (size_t i = 0; i < openfile->diagnostics->count; i++)
		okay &= openfile->diagnostics->items[i].warning;

	if (announce) {
		if (nbl_problem_count())
			show_diagnostic(FALSE);
		else if (static_check == 127)
			statusline(NOTICE, _("%s passed; ShellCheck unavailable"), tool);
		else if (static_check != 0)
			statusline(MILD, _("%s passed; ShellCheck did not finish successfully"), tool);
		else
			statusline(NOTICE, _("Mentor: no problems reported"));
	}
	fclose(output);
	return okay;
}

void nbl_check_buffer(bool announce)
{
	if (ISSET(RESTRICTED) || nbl_language() == NBL_PLAIN)
		return;
	FILE *input = tmpfile();

	if (!input || !nbl_write_snapshot(input)) {
		openfile->nbl_checked = TRUE;
		statusline(ALERT, _("Cannot snapshot this buffer: %s"), strerror(errno));
	} else
		nbl_collect_diagnostics(input, announce);
	if (input)
		fclose(input);
}

void nbl_mentor_check(void)
{
	if (in_restricted_mode())
		return;
	if (nbl_language() == NBL_PLAIN) {
		statusbar(_("Open a shell, Python, C, or C++ buffer to check it"));
		return;
	}
	nbl_check_buffer(TRUE);
	if (nbl_problem_count())
		nbl_tools_show_problems();
}

size_t nbl_problem_count(void)
{
	return openfile->diagnostics ? openfile->diagnostics->count : 0;
}

bool nbl_problem(size_t index, ssize_t *line, ssize_t *col, const char **text, bool *warning)
{
	if (index >= nbl_problem_count())
		return FALSE;
	const struct diagnostic *item = &openfile->diagnostics->items[index];

	*line = item->line;
	*col = item->column;
	*text = item->text;
	*warning = item->warning;
	return TRUE;
}

void nbl_jump_problem(size_t index)
{
	if (index >= nbl_problem_count())
		return;
	openfile->diagnostics->selected = index;
	show_diagnostic(TRUE);
}

int nbl_line_problem(ssize_t line)
{
	int severity = 0;
	struct nbl_diagnostics *result = openfile->diagnostics;

	if (result)
		for (size_t i = 0; i < result->count; i++) {
			ssize_t shown = result->items[i].line;
			if (shown > openfile->filebot->lineno)
				shown = openfile->filebot->lineno;
			if (shown == line) {
				if (!result->items[i].warning)
					return 2;
				severity = 1;
			}
		}
	return severity;
}

void nbl_next_diagnostic(void)
{
	struct nbl_diagnostics *result = openfile->diagnostics;

	if (!result || !result->count) {
		statusbar(_("No diagnostics. Check the buffer first."));
		return;
	}
	show_diagnostic(TRUE);
	result->selected = (result->selected + 1) % result->count;
}

void nbl_mentor_after_save(void)
{
	if (nbl_check_on_save && !ISSET(RESTRICTED) && nbl_language() != NBL_PLAIN) {
		nbl_check_buffer(TRUE);
		if (nbl_problem_count())
			nbl_tools_show_problems();
	}
}

void nbl_compiler_problems(FILE *log, bool failed)
{
	nbl_clear_diagnostics();
	openfile->diagnostics = nmalloc(sizeof(*openfile->diagnostics));
	openfile->diagnostics->count = openfile->diagnostics->selected = 0;

	char *line = NULL;
	size_t size = 0, total = 0;
	char fallback[256] = "";
	rewind(log);
	while (total < 65536 && getline(&line, &size, log) >= 0) {
		total += strlen(line);
		line[strcspn(line, "\r\n")] = 0;
		if ((!*fallback && *line) || strstr(line, ": error:") || strstr(line, "undefined reference"))
			snprintf(fallback, sizeof(fallback), "%s", line);
		long row, column;
		int offset = 0;
		if (sscanf(line, "/proc/self/fd/3:%ld:%ld: %n", &row, &column, &offset) != 2 ||
				offset <= 0)
			continue;
		const char *message = line + offset;
		if (strncmp(message, "error:", 6) == 0 || strncmp(message, "fatal error:", 12) == 0 ||
				strncmp(message, "warning:", 8) == 0)
			remember("Compiler", message, row, column, strncmp(message, "warning:", 8) == 0);
	}
	free(line);
	if (failed && !nbl_problem_count() && *fallback)
		remember("Compiler", fallback, 1, 1, FALSE);
	refresh_needed = TRUE;
}
