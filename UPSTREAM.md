# GNU nano upstream

NBL is an independent fork of GNU nano 9.3. The starting point is
[v9.3](https://www.nano-editor.org/git.php) at commit
`a30a7ec25ed7230f438f08af3affe233de7a33bc` from
`https://https.git.savannah.gnu.org/git/nano.git`. The release tarball
provides the generated Autotools and bundled portability files. The
baseline C sources and templates matched the Git checkout byte-for-byte
where the release carries those files.

The editor is licensed under GPL-3.0-or-later. Source headers and
`COPYING` carry the original notices. `COPYING.DOC` covers the
upstream manual, whose own notices govern its distribution. Keep the
corresponding source, build scripts and license texts available alongside
any redistributed binaries.

## Architecture

- `src/nano.c` creates curses windows, handles signals and dispatches keys.
- `src/winio.c` translates keyboard and mouse events and renders the
  title, prompts, editing viewport, status, and contextual shortcuts.
- `src/files.c` and `src/text.c` own file I/O, linked-list buffers,
  modification checks, and undo. `src/move.c` and `src/search.c`
  implement navigation and search over these buffers.
- `src/color.c` and `src/rcfile.c` load and apply syntax definitions.
- NBL changes are concentrated in `src/nbl_*.c`, with integration
  points in the files above.

To update, fetch tags from the official remote, build and test the
target upstream release in a separate checkout, and merge its pinned
commit into the fork. Update the release tarball checksum and baseline
in the same change. Review the window, rcfile, and shortcut integration
points and rerun the PTY tests before packaging.

The terminal split embeds libvterm 0.3.3. Its source, attribution and MIT
license are under `third_party/libvterm/`.

The TOML parser is tomlc17, pinned at aa4ca1bc5fe6382ac631a8d1428077a9a084a64d.
Its source and MIT license are under `third_party/tomlc17/`.
