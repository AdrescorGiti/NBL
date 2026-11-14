#!/bin/sh
# Make the source and Arch packaging inputs from a clean Git revision.
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
archive=$(realpath -- "$1")
cd "$root"
if [ -n "$(git status --porcelain)" ]; then
    printf '%s\n' "Commit release sources before creating the archive." >&2
    exit 1
fi
version=$(sed -n 's/^AC_INIT(\[NBL\], \[\([^]]*\)\].*/\1/p' configure.ac)
epoch=$(git log -1 --format=%ct)
mkdir -- "$2"
release=$(realpath -- "$2")
stage="$release/nbl-$version"
export SOURCE_DATE_EPOCH="$epoch"

scripts/stage-source.sh "$archive" "$stage"
cp README.md UPSTREAM.md BUILDING.md CONTRIBUTING.md CHANGELOG.md LICENSE \
    nblrc.example config.toml.example install.sh uninstall.sh "$stage/"
mkdir -p "$stage/docs/screenshots" "$stage/scripts" "$stage/tests" "$stage/packaging"
mkdir -p "$stage/assets/screenshots"
cp assets/screenshots/*.png "$stage/assets/screenshots/"
cp docs/nbl.1 "$stage/docs/"
cp docs/screenshots/*.png "$stage/docs/screenshots/"
cp scripts/*.sh "$stage/scripts/"
cp tests/*.py tests/*.sh "$stage/tests/"
cp packaging/PKGBUILD.in "$stage/packaging/"
tar -C "$release" --sort=name --mtime="@$epoch" --owner=0 --group=0 --numeric-owner \
    --exclude=autom4te.cache --exclude='*~' \
    -cJf "$release/nbl-$version.tar.xz" "nbl-$version"
checksum=$(sha256sum "$release/nbl-$version.tar.xz")
sed "s/@SOURCE_SHA256@/${checksum%% *}/g" packaging/PKGBUILD.in > "$release/PKGBUILD"
