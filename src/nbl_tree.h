/* Lazy filesystem tree for NBL. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NBL_TREE_H
#define NBL_TREE_H

#include <dirent.h>

enum nbl_file_kind { NBL_DIRECTORY, NBL_REGULAR, NBL_LINK, NBL_OTHER };

typedef struct nbl_node {
	struct nbl_node *parent;
	struct nbl_node **children;
	char *name, *path;
	size_t count, capacity;
	unsigned depth;
	enum nbl_file_kind kind;
	DIR *scan;
	int error;
	bool expanded, loaded;
} nbl_node;

nbl_node *nbl_tree_root(char *path);
void nbl_tree_scan(nbl_node *node, bool hidden);
void nbl_tree_clear(nbl_node *node);

#endif
