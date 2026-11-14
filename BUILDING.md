# Building NBL

The source installer targets GNU/Linux and uses POSIX sh and GNU coreutils.
The editor needs a C compiler and development headers for wide-character
ncurses. GNU make and pkg-config are required for the installer. For the
default build, provide zlib and libmagic development headers. Use gettext,
texinfo, groff, and git for a full development environment. For a checkout,
also provide Autoconf, Automake, autopoint, tar, xz, and curl, or use a local
copy of the pinned nano archive. Python and pexpect are needed for PTY tests.

## Build and install

From the unpacked NBL release source archive:

```sh
./install.sh --user
"$HOME/.local/bin/nbl" --version
```

The same command works from a Git checkout and downloads the upstream support
archive. For an offline build, pass `--upstream /path/to/nano-9.3.tar.xz`.
The staging script verifies SHA-256 before extracting it. You can also
build directly from a checkout with these commands:

```sh
curl -fLO https://ftp.gnu.org/gnu/nano/nano-9.3.tar.xz
./scripts/stage-source.sh nano-9.3.tar.xz build-source
mkdir build
cd build
../build-source/configure --prefix="$HOME/.local" --disable-nls
make -j"$(nproc)"
cd ..
```

Pass `--system` explicitly to install under `/usr/local` with administrator
privileges. The installer accepts `--prefix` with an absolute path. Use
the corresponding options with `uninstall.sh`. The installer checks existing
destinations and records the files it installed. Existing files from the
older build helper may require using a new prefix or moving those files aside.

## Tests

```sh
sh tests/identity.sh build/src/nbl
for test in tests/*.py; do python3 "$test" build/src/nbl || exit; done
```

Pass the path to ShellCheck as a second argument to `tests/diagnostics.py`
to verify both shell checkers. For sanitizer testing, configure a fresh build
with `CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"`,
then run the PTY tests against its binary.

## Arch Linux

The release bundle contains the matching source archive and PKGBUILD.
Build as a normal user with `makepkg` in the directory containing both
files. Install the package with:

```sh
sudo pacman -U ./nbl-0.4.0-1-x86_64.pkg.tar.zst
/usr/bin/nbl --init-config
```

NBL also creates the configuration on first launch. Use `command -v nbl`
to check which binary your shell resolves when multiple installations exist.

Build on stock Arch or in a stock Arch chroot to target the baseline x86-64
ISA. The PKGBUILD sets a generic CPU target. On CachyOS, the C runtime startup
objects can still impose that distribution's higher ISA requirement. The
provided package uses baseline startup objects from Arch glibc. ShellCheck,
Python, and GCC are optional runtime dependencies.

## Release source

Run `scripts/release-source.sh nano-9.3.tar.xz NEW_DIRECTORY` from a
clean Git checkout. The script stages the fork and produces a source archive
alongside a PKGBUILD with the archive’s SHA-256.
