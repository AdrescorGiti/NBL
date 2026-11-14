#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
version=$(sed -n 's/^AC_INIT(\[NBL\], \[\([^]]*\)\].*/\1/p' "$root/configure.ac")
action=install
. "$root/scripts/prefix-install.sh"
options "$@"

for command in cc make install sha256sum realpath mktemp pkg-config; do
    command -v "$command" >/dev/null || die "Missing build dependency: $command"
done
pkg-config --exists ncursesw || die 'Install the ncurses wide-character development headers before building NBL.'
printf 'Building NBL %s for Linux (%s), prefix %s\n' "$version" "$(uname -m)" "$prefix"
check_parent share/nbl/.nbl-install-manifest
[ ! -L "$manifest" ] || die "Manifest is a symlink: $manifest"
if [ -f "$manifest" ] && [ -e "$prefix/bin/nbl" ]; then
    check_parent bin/nbl
    [ -f "$prefix/bin/nbl" ] && [ ! -L "$prefix/bin/nbl" ] ||
        die "Unexpected binary at $prefix/bin/nbl"
    grep -Fqx -- "$(digest "$prefix/bin/nbl") bin/nbl" "$manifest" ||
        die "The installed binary differs from the manifest: $prefix/bin/nbl"
    old=$("$prefix/bin/nbl" --version | sed -n '1p')
    case "$old" in NBL\ *) printf 'Found %s\n' "$old" ;; *) die "Unexpected binary at $prefix/bin/nbl" ;; esac
fi

scratch=$(mktemp -d "${TMPDIR:-/tmp}/nbl-build.XXXXXXXX")
pending=
lock=
trap 'if [ -n "$pending" ]; then rm -f -- "$pending"; fi; if [ -n "$lock" ]; then rmdir -- "$lock"; fi; rm -rf -- "$scratch"' 0
trap 'exit 1' HUP INT TERM
source=$root
if [ ! -x "$source/configure" ]; then
    for command in autoconf automake autopoint tar xz; do
        command -v "$command" >/dev/null || die "Missing checkout build dependency: $command"
    done
    if [ -z "$upstream" ]; then
        command -v curl >/dev/null || die 'Install curl or supply --upstream FILE.'
        upstream="$scratch/nano-9.3.tar.xz"
        curl -fL --show-error --output "$upstream" https://ftp.gnu.org/gnu/nano/nano-9.3.tar.xz
    fi
    "$root/scripts/stage-source.sh" "$upstream" "$scratch/source"
    source="$scratch/source"
fi

mkdir "$scratch/build" "$scratch/files"
(
    cd "$scratch/build"
    "$source/configure" --prefix="$prefix" --sysconfdir="$prefix/etc" --disable-nls
    make -C lib
    make -C src
)
binary="$scratch/build/src/nbl"
LC_ALL=C "$binary" --version | grep -F "NBL $version (GNU nano " >/dev/null ||
    die 'The built editor could not run on this system.'

stage="$scratch/files"
install -Dm755 "$binary" "$stage/bin/nbl"
install -Dm644 "$root/docs/nbl.1" "$stage/share/man/man1/nbl.1"
install -d "$stage/share/nbl" "$stage/share/doc/nbl" "$stage/share/licenses/nbl"
install -m644 "$root"/syntax/*.nanorc "$stage/share/nbl/"
for doc in README.md UPSTREAM.md BUILDING.md CHANGELOG.md nblrc.example config.toml.example; do
    install -m644 "$root/$doc" "$stage/share/doc/nbl/$doc"
done
install -m644 "$root/LICENSE" "$root/COPYING.DOC" "$stage/share/licenses/nbl/"
for library in tomlc17 libvterm; do
    install -m644 "$root/third_party/$library/LICENSE" "$stage/share/licenses/nbl/$library.LICENSE"
done

(cd "$stage" && find bin share -type f | LC_ALL=C sort) > "$scratch/list"
check_parent share/nbl/.nbl-install-manifest
mkdir -p -- "$prefix/share/nbl"
lock_prefix
[ ! -L "$manifest" ] || die "Manifest is a symlink: $manifest"
# Check every destination first. Keep existing non-NBL or locally edited files.
while IFS= read -r file; do
    valid_file "$file" || die "Unexpected release path: $file"
    check_parent "$file"
    if [ -e "$prefix/$file" ] || [ -L "$prefix/$file" ]; then
        [ -f "$prefix/$file" ] && [ ! -L "$prefix/$file" ] || die "Unexpected entry: $prefix/$file"
        actual=$(digest "$prefix/$file")
        if [ "$actual" != "$(digest "$stage/$file")" ] &&
            ! { [ -f "$manifest" ] && grep -Fqx -- "$actual $file" "$manifest"; }; then
            die "File differs from the previous installation: $prefix/$file"
        fi
    fi
    printf '%s %s\n' "$(digest "$stage/$file")" "$file" >> "$scratch/manifest"
done < "$scratch/list"

publish_manifest() {
    pending=$(mktemp "$prefix/share/nbl/.nbl.XXXXXXXX")
    install -m644 "$1" "$pending"
    mv -fT -- "$pending" "$manifest"
    pending=
}

# Accept either intact version on a retry if an update was interrupted.
if [ -f "$manifest" ]; then
    LC_ALL=C sort -u "$manifest" "$scratch/manifest" > "$scratch/recovery"
    publish_manifest "$scratch/recovery"
else
    publish_manifest "$scratch/manifest"
fi

while IFS= read -r file; do
    mkdir -p -- "$prefix/${file%/*}"
    pending=$(mktemp "$prefix/${file%/*}/.nbl.XXXXXXXX")
    permissions=644
    [ "$file" != bin/nbl ] || permissions=755
    install -m "$permissions" "$stage/$file" "$pending"
    mv -fT -- "$pending" "$prefix/$file"
    pending=
done < "$scratch/list"

publish_manifest "$scratch/manifest"
"$prefix/bin/nbl" --version
if [ "$mode" = user ]; then
    "$prefix/bin/nbl" --init-config
fi
case ":${PATH:-}:" in
    *":$prefix/bin:"*) ;;
    *) printf 'Add %s/bin to PATH to run nbl by name.\n' "$prefix" ;;
esac
