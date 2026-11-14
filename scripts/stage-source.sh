#!/bin/sh
# Prepare a self-contained Autotools tree using the official generated files.
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
archive=$(realpath -- "$1")
mkdir -- "$2"
stage=$(realpath -- "$2")
printf '%s  %s\n' a9affc52c9596ee0682bcdd4ceb3c0b235eb221d83d733dfd9b0dff45897cb0d "$archive" |
    sha256sum --check --status
tar -xf "$archive" --strip-components=1 -C "$stage"
cp "$root/configure.ac" "$root/Makefile.am" "$root/config.toml.example" "$stage/"
cp "$root"/src/*.[ch] "$root/src/Makefile.am" "$stage/src/"
cp "$root"/syntax/*.nanorc "$stage/syntax/"
cp -r "$root/third_party" "$stage/"
cd "$stage"
autoreconf -fi
