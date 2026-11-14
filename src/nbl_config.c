/* User TOML settings. SPDX-License-Identifier: GPL-3.0-or-later */
#include "prototypes.h"
#include "nbl.h"
#include "tomlc17.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static const unsigned char default_config[] = {
#include "nbl-default-config.inc"
};

int nbl_theme_rgb[NBL_ROLES];
bool nbl_highlight_line = TRUE, nbl_auto_pairs = TRUE;
bool nbl_explorer_left, nbl_show_hidden, nbl_terminal_top;
bool nbl_terminal_enabled = TRUE;
int nbl_terminal_height;

struct settings {
	int colors[NBL_ROLES];
	int lines, highlight, indent, pairs, tab, mouse;
	int explorer, left, width, hidden;
	int terminal, top, height;
	char cc[128], cxx[128], python[128];
};

static struct settings baseline;
static struct settings active;

enum value_kind { BOOLEAN, INTEGER, COLOR, POSITION, COMMAND };

#define FIELD(key, kind, member, low, high) {key, kind, offsetof(struct settings, member), low, high}
#define RGB(key, role) FIELD(key, COLOR, colors[role], 0, 0)

static const struct {
	const char *key;
	enum value_kind kind;
	size_t offset;
	int low, high;
} options[] = {
	RGB("theme.background", NBL_BACKGROUND), RGB("theme.foreground", NBL_TEXT),
	RGB("theme.accent", NBL_ACCENT), RGB("theme.selection", NBL_SELECTION),
	RGB("theme.current_line", NBL_CURRENT), RGB("theme.error", NBL_ERROR),
	RGB("theme.comment", NBL_COMMENT), RGB("theme.keyword", NBL_KEYWORD),
	RGB("theme.string", NBL_STRING), RGB("theme.number", NBL_NUMBER),
	FIELD("editor.line_numbers", BOOLEAN, lines, 0, 0),
	FIELD("editor.highlight_current_line", BOOLEAN, highlight, 0, 0),
	FIELD("editor.auto_indent", BOOLEAN, indent, 0, 0),
	FIELD("editor.auto_close_brackets", BOOLEAN, pairs, 0, 0),
	FIELD("editor.tab_size", INTEGER, tab, 1, 32),
	FIELD("editor.mouse", BOOLEAN, mouse, 0, 0),
	FIELD("explorer.enabled", BOOLEAN, explorer, 0, 0),
	FIELD("explorer.position", POSITION, left, 0, 0),
	FIELD("explorer.width", INTEGER, width, 15, 60),
	FIELD("explorer.show_hidden", BOOLEAN, hidden, 0, 0),
	FIELD("terminal.enabled", BOOLEAN, terminal, 0, 0),
	FIELD("terminal.position", POSITION, top, 1, 0),
	FIELD("terminal.height", INTEGER, height, 4, 40),
	FIELD("tools.cc", COMMAND, cc, 0, 0),
	FIELD("tools.cxx", COMMAND, cxx, 0, 0),
	FIELD("tools.python", COMMAND, python, 0, 0),
};

static bool assign(struct settings *target, const char *key, toml_datum_t value, char *error)
{
	for (size_t i = 0; i < sizeof(options) / sizeof(*options); i++) {
		if (strcmp(key, options[i].key) != 0)
			continue;
		int *out = (int *)((char *)target + options[i].offset);
		const char *expectation = "";
		switch (options[i].kind) {
			case BOOLEAN:
				if (value.type == TOML_BOOLEAN) {
					*out = value.u.boolean;
					return TRUE;
				}
				expectation = "expected true or false";
				break;
			case INTEGER:
				if (value.type == TOML_INT64 && value.u.int64 >= options[i].low &&
						value.u.int64 <= options[i].high) {
					*out = value.u.int64;
					return TRUE;
				}
				snprintf(error, 256, "line %d: %s: expected integer %d–%d",
						value.lineno, key, options[i].low, options[i].high);
				return FALSE;
			case COLOR:
				if (value.type == TOML_STRING && value.u.str.len == 7 && value.u.s[0] == '#' &&
						strspn(value.u.s + 1, "0123456789abcdefABCDEF") == 6) {
					*out = strtol(value.u.s + 1, NULL, 16);
					return TRUE;
				}
				expectation = "expected a color string #RRGGBB";
				break;
			case POSITION: {
				const char *first = options[i].low ? "bottom" : "right";
				const char *second = options[i].low ? "top" : "left";
				if (value.type == TOML_STRING && strlen(value.u.s) == (size_t)value.u.str.len) {
					if (strcmp(value.u.s, first) == 0 || strcmp(value.u.s, second) == 0) {
						*out = strcmp(value.u.s, second) == 0;
						return TRUE;
					}
				}
				expectation = options[i].low ? "expected bottom or top" : "expected right or left";
				break;
			}
			case COMMAND:
				if (value.type == TOML_STRING && value.u.str.len > 0 && value.u.str.len < 128 &&
						strcspn(value.u.s, "\r\n") == (size_t)value.u.str.len) {
					memcpy((char *)target + options[i].offset, value.u.s, value.u.str.len + 1);
					return TRUE;
				}
				expectation = "expected an executable name or path";
				break;
		}
		snprintf(error, 256, "line %d: %s: %s", value.lineno, key, expectation);
		return FALSE;
	}
	snprintf(error, 256, "line %d: unknown option %s", value.lineno, key);
	return FALSE;
}

static bool validate(struct settings *target, toml_datum_t root, char *error)
{
	const char *sections[] = {"theme", "editor", "explorer", "terminal", "tools"};
	for (int i = 0; i < root.u.tab.size; i++) {
		const char *name = root.u.tab.key[i];
		toml_datum_t table = root.u.tab.value[i];
		bool known = FALSE;
		for (size_t j = 0; j < sizeof(sections) / sizeof(*sections); j++)
			known |= strcmp(name, sections[j]) == 0;
		if (!known || table.type != TOML_TABLE) {
			snprintf(error, 256, "line %d: expected a known section, got %.100s", table.lineno, name);
			return FALSE;
		}
		for (int j = 0; j < table.u.tab.size; j++) {
			char key[128];
			snprintf(key, sizeof(key), "%s.%s", name, table.u.tab.key[j]);
			if (!assign(target, key, table.u.tab.value[j], error))
				return FALSE;
		}
	}
	return TRUE;
}

static char *config_path(void)
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	get_homedir();
	return xdg && *xdg ? concatenate(xdg, "/nbl/config.toml") :
			homedir ? concatenate(homedir, "/.config/nbl/config.toml") : NULL;
}

/* Make missing parents with private permissions. Existing directories keep
 * their permissions, and open/link report any non-directory components. */
static bool make_config_parents(char *path)
{
	for (char *p = strchr(path + (*path == '/'), '/'); p; p = strchr(p + 1, '/')) {
		*p = '\0';
		int result = mkdir(path, 0700);
		*p = '/';
		if (result < 0 && errno != EEXIST)
			return FALSE;
	}
	return TRUE;
}

/* Publish the complete file atomically, without replacing an existing file
 * or symlink, even when multiple sessions initialize at the same time. */
static int seed_config(char *path)
{
	struct stat info;
	if (lstat(path, &info) == 0)
		return 0;
	if (errno != ENOENT || !make_config_parents(path))
		return -1;

	char *temporary = concatenate(path, ".XXXXXX");
	int fd = mkstemp(temporary);
	int result = -1;
	if (fd >= 0) {
		size_t written = 0;
		while (written < sizeof(default_config)) {
			ssize_t count = write(fd, default_config + written, sizeof(default_config) - written);
			if (count < 0 && errno == EINTR)
				continue;
			if (count <= 0) {
				if (!count)
					errno = EIO;
				break;
			}
			written += count;
		}
		int saved = errno;
		if (close(fd) == 0 && written == sizeof(default_config)) {
			if (link(temporary, path) == 0)
				result = 1;
			else if (errno == EEXIST)
				result = 0;
			saved = errno;
		} else if (written == sizeof(default_config))
			saved = errno;
		unlink(temporary);
		errno = saved;
	}
	free(temporary);
	return result;
}

int nbl_init_config(void)
{
	char *path = config_path();
	if (!path) {
		fputs("Cannot locate the user configuration directory\n", stderr);
		return 1;
	}
	int result = seed_config(path);
	if (result < 0)
		fprintf(stderr, "%s: %s\n", path, strerror(errno));
	else
		printf("%s %s\n", result ? "Created" : "Already exists:", path);
	free(path);
	return result < 0 ? 1 : 0;
}

static int load(struct settings *target, char *error, bool initialize)
{
	char *path = config_path();
	if (!path)
		return 0;
	if (initialize && seed_config(path) < 0) {
		snprintf(error, 256, "could not create configuration: %s", strerror(errno));
		free(path);
		return -1;
	}
	int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
	int problem = errno;
	free(path);
	if (fd < 0) {
		if (problem == ENOENT)
			return 0;
		snprintf(error, 256, "%s", strerror(problem));
		return -1;
	}

	struct stat info;
	if (fstat(fd, &info) < 0 || !S_ISREG(info.st_mode) || info.st_size > 65536) {
		close(fd);
		snprintf(error, 256, "expected a regular file up to 64 KiB");
		return -1;
	}
	FILE *file = fdopen(fd, "rb");
	if (!file) {
		snprintf(error, 256, "%s", strerror(errno));
		close(fd);
		return -1;
	}
	char *data = nmalloc(65537);
	size_t length = fread(data, 1, 65537, file);
	bool failed = ferror(file) || length > 65536;
	fclose(file);
	if (failed) {
		free(data);
		snprintf(error, 256, "could not read configuration (limit 64 KiB)");
		return -1;
	}
	toml_result_t result = toml_parse(data, length);
	bool valid = result.ok && validate(target, result.toptab, error);
	if (!result.ok)
		snprintf(error, 256, "%s", result.errmsg);
	toml_free(result);
	free(data);
	return valid ? 1 : -1;
}

int nbl_check_config(void)
{
	struct settings trial = {0};
	char error[256];
	int result = load(&trial, error, FALSE);
	if (result < 0)
		fprintf(stderr, "config.toml: %s\n", error);
	else
		puts(result ? "config.toml: OK" : "config.toml: not found; using defaults");
	return result < 0 ? 1 : 0;
}

static void apply(const struct settings *config)
{
	memcpy(nbl_theme_rgb, config->colors, sizeof(nbl_theme_rgb));
	if (config->lines) SET(LINE_NUMBERS); else UNSET(LINE_NUMBERS);
	if (config->indent) SET(AUTOINDENT); else UNSET(AUTOINDENT);
	if (config->mouse && !ISSET(RAW_SEQUENCES)) SET(USE_MOUSE); else UNSET(USE_MOUSE);
	tabsize = config->tab;
	nbl_highlight_line = config->highlight;
	nbl_auto_pairs = config->pairs;
	nbl_explorer_visible = config->explorer;
	nbl_explorer_left = config->left;
	nbl_explorer_percent = config->width;
	nbl_show_hidden = config->hidden;
	nbl_terminal_enabled = config->terminal;
	nbl_terminal_top = config->top;
	nbl_terminal_height = config->height;
	active = *config;
}

const char *nbl_tool_command(enum nbl_language language)
{
	switch (language) {
		case NBL_PYTHON: return active.python;
		case NBL_C: return active.cc;
		case NBL_CPP: return active.cxx;
		default: return "";
	}
}

static bool ignore_config;

void nbl_config_start(bool ignore)
{
	ignore_config = ignore;
	baseline = (struct settings) {
		.lines = ISSET(LINE_NUMBERS), .indent = ISSET(AUTOINDENT), .mouse = ISSET(USE_MOUSE),
		.highlight = TRUE, .pairs = TRUE, .tab = tabsize,
		.explorer = nbl_explorer_visible, .width = nbl_explorer_percent,
		.terminal = TRUE,
	};
	for (int i = 0; i < NBL_ROLES; i++)
		baseline.colors[i] = -1;

	struct settings trial = baseline;
	char error[256];
	int result = ignore ? 0 : load(&trial, error, !ISSET(RESTRICTED));
	if (result < 0) {
		char message[300];
		snprintf(message, sizeof(message), "config.toml: %s", error);
		startup_problem = mallocstrcpy(startup_problem, message);
	}
	apply(result < 0 ? &baseline : &trial);
}

void nbl_reload_config(void)
{
	if (ignore_config) {
		statusbar(_("Configuration disabled by -I"));
		return;
	}
	struct settings trial = baseline;
	char error[256];
	int result = load(&trial, error, FALSE);
	if (result < 0) {
		statusline(ALERT, "config.toml: %s", error);
		return;
	}
	bool hidden_changed = nbl_show_hidden != trial.hidden;
	apply(&trial);
	if (has_colors())
		set_interface_colorpairs();
	have_palette = FALSE;
	mouse_init();
	nbl_relayout();
	if (hidden_changed)
		nbl_refresh_explorer();
	statusbar(_("Configuration reloaded"));
}
