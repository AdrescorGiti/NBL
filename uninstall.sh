#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
version=$(sed -n 's/^AC_INIT(\[NBL\], \[\([^]]*\)\].*/\1/p' "$root/configure.ac")
action=uninstall
. "$root/scripts/prefix-install.sh"
options "$@"

check_parent share/nbl/.nbl-install-manifest
[ ! -L "$manifest" ] || die "Manifest is a symlink: $manifest"
if [ ! -f "$manifest" ]; then
    printf 'No installer manifest found in %s.\n' "$prefix"
    exit 0
fi
lock=
trap 'if [ -n "$lock" ]; then rmdir -- "$lock"; fi' 0
trap 'exit 1' HUP INT TERM
lock_prefix
[ -f "$manifest" ] && [ ! -L "$manifest" ] || die 'The manifest changed during startup.'
# Validate the complete manifest before deleting files.
while IFS=' ' read -r expected file; do
    valid_file "$file" || die "Invalid manifest entry: $file"
    check_parent "$file"
done < "$manifest"
while IFS=' ' read -r expected file; do
    if [ ! -e "$prefix/$file" ] && [ ! -L "$prefix/$file" ]; then
        continue
    fi
    if [ -L "$prefix/$file" ] || [ ! -f "$prefix/$file" ] || [ "$(digest "$prefix/$file")" != "$expected" ]; then
        printf 'Kept changed file %s\n' "$prefix/$file"
        continue
    fi
    rm -- "$prefix/$file"
done < "$manifest"
rm -- "$manifest"
rmdir -- "$lock"
lock=
for directory in share/nbl share/licenses/nbl share/doc/nbl; do
    rmdir -- "$prefix/$directory" 2>/dev/null || :
done
printf 'Removed the tracked NBL files in %s.\n' "$prefix"
