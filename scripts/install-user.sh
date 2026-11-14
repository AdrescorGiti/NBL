#!/bin/sh
# Install the build and initialize any missing user configuration.
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
build=$(realpath -- "${1:-build}")
prefix="$HOME/.local"
binary="$prefix/bin/nbl"

if ! test -x "$build/src/nbl"; then
    printf '%s\n' "Build NBL before installing." >&2
    exit 1
fi
if ! grep -F -- "--prefix=$prefix" "$build/config.log" >/dev/null; then
    printf '%s\n' "Configure the build with --prefix=$prefix first." >&2
    exit 1
fi

install -d "$prefix/bin" "$prefix/share/nbl" "$prefix/share/man/man1" \
    "$prefix/share/doc/nbl" "$prefix/share/licenses/nbl"
install -m644 "$root"/syntax/*.nanorc "$prefix/share/nbl/"
install -m644 "$root/docs/nbl.1" "$prefix/share/man/man1/nbl.1"
install -m644 "$root/README.md" "$root/UPSTREAM.md" "$root/BUILDING.md" \
    "$root/CHANGELOG.md" "$root/nblrc.example" "$root/config.toml.example" \
    "$prefix/share/doc/nbl/"
install -m644 "$root/LICENSE" "$root/COPYING.DOC" "$prefix/share/licenses/nbl/"
install -m644 "$root/third_party/tomlc17/LICENSE" "$prefix/share/licenses/nbl/tomlc17.LICENSE"
install -m644 "$root/third_party/libvterm/LICENSE" "$prefix/share/licenses/nbl/libvterm.LICENSE"

# Replace the executable atomically so a running instance can finish.
temp=$(mktemp "$prefix/bin/.nbl.XXXXXXXX")
trap 'test -z "$temp" || rm -f -- "$temp"' EXIT
install -m755 "$build/src/nbl" "$temp"
mv -fT -- "$temp" "$binary"
temp=
"$binary" --init-config
"$binary" --version
