/* Explicit local filesystem operations for Explorer. GPL-3.0-or-later. */
#include "prototypes.h"
#include "nbl.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static bool within(const char *path, const char *directory)
{
	size_t size = strlen(directory);
	return strcmp(directory, "/") == 0 ||
			(!strncmp(path, directory, size) && (!path[size] || path[size] == '/'));
}

static bool permitted(const char *path)
{
#ifdef ENABLE_OPERATINGDIR
	if (operating_dir) {
		char *base = copy_of(operating_dir);
		size_t size = strlen(base);
		if (size > 1 && base[size - 1] == '/')
			base[size - 1] = '\0';
		bool inside = within(path, base);
		free(base);
		if (!inside)
			errno = EPERM;
		return inside;
	}
#endif
	return TRUE;
}

static char *join(const char *parent, const char *name)
{
	if (strcmp(parent, "/") == 0)
		return concatenate(parent, name);
	char *base = concatenate(parent, "/");
	char *path = concatenate(base, name);
	free(base);
	return path;
}

static char *prompt_path(const char *question, const char *base)
{
	if (do_prompt(MEXPLORERPATH, "", NULL, edit_refresh, "%s", question) != 0)
		return NULL;
	char *expanded = expand_leading_tilde(answer);
	if (*expanded == '/')
		return expanded;
	char *result = join(base, expanded);
	free(expanded);
	return result;
}

/* Resolve existing parents component by component, never creating through
 * an unresolved symlink, and check confinement before every mkdir. */
static char *resolve_dir(const char *path, bool create)
{
	char *parts = copy_of(path), *cursor = NULL;
	char *result = copy_of("/");
	for (char *part = strtok_r(parts, "/", &cursor); part; part = strtok_r(NULL, "/", &cursor)) {
		char *next = join(result, part);
		char *real = realpath(next, NULL);
		if (!real && errno == ENOENT && create) {
			struct stat info;
			if (lstat(next, &info) < 0 && errno == ENOENT && permitted(next) &&
					(mkdir(next, 0777) == 0 || errno == EEXIST))
				real = realpath(next, NULL);
		}
		int error = errno;
		free(next);
		if (!real) {
			free(result);
			free(parts);
			errno = error;
			return NULL;
		}
		free(result);
		result = real;
	}
	free(parts);
	struct stat info;
	if (stat(result, &info) < 0) {
		int error = errno;
		free(result);
		errno = error;
		return NULL;
	}
	if (!S_ISDIR(info.st_mode) || !permitted(result)) {
		int error = S_ISDIR(info.st_mode) ? errno : ENOTDIR;
		free(result);
		errno = error;
		return NULL;
	}
	return result;
}

static char *destination(const char *path, bool new_parents)
{
	const char *slash = strrchr(path, '/');
	if (!slash || !slash[1] || !strcmp(slash + 1, ".") || !strcmp(slash + 1, "..")) {
		errno = EINVAL;
		return NULL;
	}
	char *parent = slash == path ? copy_of("/") : measured_copy(path, slash - path);
	char *directory = resolve_dir(parent, FALSE);
	if (!directory && errno == ENOENT && new_parents) {
		if (ask_user(YESORNO, _("Create missing parent directories?")) == YES)
			directory = resolve_dir(parent, TRUE);
		else
			errno = ECANCELED;
	}
	int error = errno;
	free(parent);
	if (!directory) {
		errno = error;
		return NULL;
	}
	char *result = join(directory, slash + 1);
	free(directory);
	return result;
}

static bool open_buffer_uses(const char *path, const struct stat *source)
{
	openfilestruct *buffer = openfile;
	do {
		if (*buffer->filename) {
			char *canonical = get_full_path(buffer->filename);
			if (canonical) {
				size_t length = strlen(canonical);
				if (length > 1 && canonical[length - 1] == '/')
					canonical[length - 1] = 0;
			}
			bool overlaps = canonical && (S_ISDIR(source->st_mode) ?
					within(canonical, path) : !strcmp(canonical, path));
			free(canonical);

			/* A directly selected symlink must stay available to any buffer
			 * opened through it, even if the buffer resolves elsewhere. */
			struct stat lexical;
			bool same_entry = lstat(buffer->filename, &lexical) == 0 &&
					lexical.st_dev == source->st_dev && lexical.st_ino == source->st_ino;
			bool same_file = buffer->statinfo && buffer->statinfo->st_dev == source->st_dev &&
					buffer->statinfo->st_ino == source->st_ino;
			if (overlaps || same_entry || same_file)
				return TRUE;
		}
		buffer = buffer->next;
	} while (buffer != openfile);
	return FALSE;
}

static bool same_entry(const char *path, const struct stat *original)
{
	struct stat current;
	if (lstat(path, &current) < 0)
		return FALSE;
	if (current.st_dev == original->st_dev && current.st_ino == original->st_ino)
		return TRUE;
	errno = ESTALE;
	return FALSE;
}

static bool copy_regular(const char *source, const char *target, const struct stat *original)
{
	int from = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (from < 0)
		return FALSE;
	struct stat info;
	if (fstat(from, &info) < 0 || !S_ISREG(info.st_mode) ||
			info.st_dev != original->st_dev || info.st_ino != original->st_ino) {
		close(from);
		errno = ESTALE;
		return FALSE;
	}
	/* Write to a private sibling, then publish with a no-clobber hard link. */
	char *parent = measured_copy(target, strrchr(target, '/') - target + 1);
	char *temp = concatenate(parent, ".nbl-copy.XXXXXX");
	free(parent);
	int to = mkstemp(temp);
	bool okay = to >= 0;
	char buffer[65536];
	while (okay) {
		ssize_t size = read(from, buffer, sizeof(buffer));
		if (size < 0 && errno == EINTR)
			continue;
		if (size <= 0) {
			okay = size == 0;
			break;
		}
		ssize_t done = 0;
		while (done < size) {
			ssize_t written = write(to, buffer + done, size - done);
			if (written < 0 && errno == EINTR)
				continue;
			if (written <= 0) {
				if (!written)
					errno = EIO;
				okay = FALSE;
				break;
			}
			done += written;
		}
	}
	int error = errno;
	if (okay && fchmod(to, info.st_mode & 0777) < 0) {
		okay = FALSE;
		error = errno;
	}
	if (to >= 0 && close(to) < 0 && okay) {
		okay = FALSE;
		error = errno;
	}
	close(from);
	if (okay && link(temp, target) < 0) {
		okay = FALSE;
		error = errno;
	}
	if (to >= 0)
		unlink(temp);
	free(temp);
	errno = error;
	return okay;
}

static void change_root(const char *path)
{
	char *resolved = resolve_dir(path, FALSE);
	if (!resolved || !nbl_explorer_set_root(resolved))
		statusline(AHEM, _("Cannot open folder: %s"), strerror(errno));
	else
		statusbar(_("Project folder changed"));
	free(resolved);
}

void nbl_file_action(int key, const char *root, const char *selected, bool is_directory)
{
	if (in_restricted_mode())
		return;
	char *base = !selected ? copy_of(root) : is_directory ? copy_of(selected) :
			measured_copy(selected, strrchr(selected, '/') - selected);
	if (!*base) {
		free(base);
		base = copy_of("/");
	}

	char *input = NULL;
	char *target = NULL;
	char *source = NULL;
	struct stat info;
	bool changed = FALSE;
	bool creation = key == 'n' || key == 'N';
	const char *operation = _("File operation");
	if (key == 'h' || key == 'u' || key == 'i' || key == 'g') {
		if (key == 'h') {
			get_homedir();
			if (homedir)
				change_root(homedir);
		} else if (key == 'u') {
			char *parent = join(root, "..");
			change_root(parent);
			free(parent);
		} else if (key == 'i') {
			if (is_directory)
				change_root(selected);
			else
				statusbar(_("Select a folder to enter"));
		} else {
			input = prompt_path(_("Open folder"), root);
			if (input)
				change_root(input);
		}
		goto done;
	}
	if (key == '?') {
		statusbar(_("n File  N Folder  e Rename  m Move  c Copy  d Delete  g Path  i Enter  u Up  h Home"));
		goto done;
	}

	if (!creation) {
		if (!selected) {
			statusbar(_("Select an entry first"));
			goto done;
		}
		source = destination(selected, FALSE);
		if (!source || lstat(source, &info) < 0)
			goto failure;
		if (!S_ISREG(info.st_mode) && !S_ISDIR(info.st_mode) && !S_ISLNK(info.st_mode)) {
			errno = EINVAL;
			goto failure;
		}
		if (key != 'c' && open_buffer_uses(source, &info)) {
			statusline(AHEM, _("Close open buffers for this path before changing it"));
			goto done;
		}
	}
	if (key == 'd') {
		char *label = display_string(tail(selected), 0, COLS / 2, FALSE, FALSE);
		char *question = concatenate(_("Delete this entry permanently? "), label);
		int response = ask_user(YESORNO, question);
		free(question);
		free(label);
		if (response != YES)
			goto done;
		if (same_entry(source, &info))
			changed = (S_ISDIR(info.st_mode) ? rmdir(source) : unlink(source)) == 0;
	} else {
		/* A relative rename stays beside the source. Move and copy paths
		 * use the current project root, while creation uses the selection. */
		if (key == 'e') {
			free(base);
			base = measured_copy(selected, strrchr(selected, '/') - selected);
			if (!*base) {
				free(base);
				base = copy_of("/");
			}
		}
		input = prompt_path(key == 'n' ? _("New file path") : key == 'N' ? _("New folder path") :
				key == 'e' ? _("Rename to") : key == 'm' ? _("Move to") : _("Copy to"),
				creation || key == 'e' ? base : root);
		if (!input)
			goto done;
		target = destination(input, TRUE);
		if (!target)
			goto failure;
		if (key == 'n') {
			int fd = open(target, O_WRONLY | O_EXCL | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0666);
			if (fd >= 0)
				changed = close(fd) == 0;
		} else if (key == 'N')
			changed = mkdir(target, 0777) == 0;
		else if (key == 'c') {
			if (!S_ISREG(info.st_mode))
				errno = EINVAL;
			else
				changed = copy_regular(source, target, &info);
		} else if (same_entry(source, &info))
			changed = renameat2(AT_FDCWD, source, AT_FDCWD, target, RENAME_NOREPLACE) == 0;
	}
	if (!changed)
		goto failure;
	nbl_refresh_explorer();
	if (key == 'n')
		nbl_explorer_open_path(target, TRUE);
	else
		statusbar(key == 'N' ? _("Folder created") : key == 'c' ? _("File copied") :
				key == 'd' ? _("Entry deleted") : _("Entry moved"));
	goto done;

failure:
	if (errno != ECANCELED)
		statusline(AHEM, "%s: %s", operation, strerror(errno));
done:
	free(input);
	free(target);
	free(source);
	free(base);
}
