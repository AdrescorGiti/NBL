/* Lazy filesystem tree for NBL. SPDX-License-Identifier: GPL-3.0-or-later */
#include "prototypes.h"
#include "nbl_tree.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define DIRECTORY_BATCH 2048
#define TREE_DEPTH_LIMIT 128

nbl_node *nbl_tree_root(char *path)
{
	nbl_node *root = nmalloc(sizeof(*root));

	*root = (nbl_node){ .path = path, .kind = NBL_DIRECTORY, .expanded = TRUE };
	return root;
}

static int compare_nodes(const void *a, const void *b)
{
	const nbl_node *left = *(nbl_node *const *)a, *right = *(nbl_node *const *)b;

	if ((left->kind == NBL_DIRECTORY) != (right->kind == NBL_DIRECTORY))
		return left->kind == NBL_DIRECTORY ? -1 : 1;
	return strcmp(left->name, right->name);
}

static enum nbl_file_kind kind_of(int fd, const struct dirent *entry)
{
	struct stat info;

	switch (entry->d_type) {
		case DT_DIR: return NBL_DIRECTORY;
		case DT_REG: return NBL_REGULAR;
		case DT_LNK: return NBL_LINK;
		case DT_UNKNOWN:
			if (fstatat(fd, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) == 0) {
				if (S_ISDIR(info.st_mode))
					return NBL_DIRECTORY;
				if (S_ISREG(info.st_mode))
					return NBL_REGULAR;
				if (S_ISLNK(info.st_mode))
					return NBL_LINK;
			}
			break;
	}
	return NBL_OTHER;
}

static void add_entry(nbl_node *parent, const struct dirent *entry)
{
	nbl_node *child = nmalloc(sizeof(*child));
	char *path = parent->path;
	size_t length = strlen(path);

	*child = (nbl_node) {
		.parent = parent,
		.name = copy_of(entry->d_name),
		.depth = parent->depth + 1,
		.kind = kind_of(dirfd(parent->scan), entry),
	};
	child->path = nmalloc(length + strlen(child->name) + 2);
	sprintf(child->path, "%s%s%s", path, path[length - 1] == '/' ? "" : "/", child->name);

	if (parent->count == parent->capacity) {
		parent->capacity = parent->capacity ? parent->capacity * 2 : 64;
		parent->children = nrealloc(parent->children, parent->capacity * sizeof(*parent->children));
	}
	parent->children[parent->count++] = child;
}

void nbl_tree_scan(nbl_node *node, bool hidden)
{
	if (!node->loaded) {
		int fd = -1;

		node->loaded = TRUE;
		if (node->depth < TREE_DEPTH_LIMIT)
			fd = open(node->path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if (fd < 0) {
			node->error = node->depth < TREE_DEPTH_LIMIT ? errno : ELOOP;
			return;
		}
		node->scan = fdopendir(fd);
		if (!node->scan) {
			node->error = errno;
			close(fd);
			return;
		}
	}
	if (!node->scan)
		return;

	/* Read a bounded amount per action. An explicit continuation lets the
	 * user reach every entry without scanning huge folders on startup. */
	for (size_t seen = 0; seen < DIRECTORY_BATCH; seen++) {
		struct dirent *entry;

		errno = 0;
		entry = readdir(node->scan);
		if (!entry) {
			node->error = errno;
			closedir(node->scan);
			node->scan = NULL;
			break;
		}
		if (entry->d_name[0] == '.' && (!hidden ||
				strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0))
			continue;
		add_entry(node, entry);
	}

	if (node->count > 1)
		qsort(node->children, node->count, sizeof(*node->children), compare_nodes);
}

void nbl_tree_clear(nbl_node *node)
{
	for (size_t i = 0; i < node->count; i++) {
		nbl_node *child = node->children[i];

		nbl_tree_clear(child);
		free(child->name);
		free(child->path);
		free(child);
	}

	if (node->scan)
		closedir(node->scan);
	node->scan = NULL;
	free(node->children);
	node->children = NULL;
	node->count = node->capacity = 0;
	node->loaded = FALSE;
	node->error = 0;
}
