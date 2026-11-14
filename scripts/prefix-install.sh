# Shared Linux prefix handling for install.sh and uninstall.sh.
die() { printf '%s\n' "$*" >&2; exit 1; }

options() {
    mode=user
    prefix=
    upstream=
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --user) mode=user ;;
            --system) mode=system ;;
            --prefix)
                [ "$#" -ge 2 ] || die '--prefix requires an absolute directory.'
                prefix=$2
                shift ;;
            --upstream)
                [ "$action" = install ] && [ "$#" -ge 2 ] ||
                    die '--upstream requires an archive and is available for installation.'
                upstream=$2
                shift ;;
            --version) printf 'NBL %s\n' "$version"; exit 0 ;;
            -h|--help)
                printf 'Usage: %s [--user | --system] [--prefix DIR] [--help] [--version]\n' "$0"
                if [ "$action" = install ]; then
                    printf '%s\n' 'Build and install NBL from source. --upstream FILE supplies the verified nano archive for a Git checkout.'
                else
                    printf '%s\n' 'Remove files recorded by the NBL installer. Keep edited files and user configuration.'
                fi
                exit 0 ;;
            *) die "Unknown option: $1" ;;
        esac
        shift
    done
    [ "$(uname -s)" = Linux ] || die 'NBL requires GNU/Linux.'
    if [ "$mode" = system ]; then
        [ "$(id -u)" -eq 0 ] || die '--system requires administrator privileges.'
        prefix=${prefix:-/usr/local}
    else
        [ "$(id -u)" -ne 0 ] || die 'Run --user as your regular user, or choose --system explicitly.'
        prefix=${prefix:-"${HOME:?}/.local"}
    fi
    case "$prefix" in /*) ;; *) die '--prefix requires an absolute directory.' ;; esac
    case "$prefix" in *[![:print:]]*) die '--prefix cannot contain control characters.' ;; esac
    prefix=$(realpath -m -- "$prefix")
    case "$prefix" in
        /|/bin|/etc|/sbin|/lib|/lib64|/usr|/usr/bin|/usr/lib|/usr/share|/usr/sbin|/opt|/var)
            die 'Choose a dedicated prefix, ~/.local or /usr/local.' ;;
    esac
    manifest="$prefix/share/nbl/.nbl-install-manifest"
}

# Restrict the manifest to the release's public file layout. Also refuse to
# follow a destination directory symlink when installing or uninstalling.
valid_file() {
    case "$1" in
        bin/nbl|share/man/man1/nbl.1|share/doc/nbl/README.md|share/doc/nbl/UPSTREAM.md|\
        share/doc/nbl/BUILDING.md|share/doc/nbl/CHANGELOG.md|share/doc/nbl/nblrc.example|\
        share/doc/nbl/config.toml.example|share/licenses/nbl/LICENSE|share/licenses/nbl/COPYING.DOC|\
        share/licenses/nbl/tomlc17.LICENSE|share/licenses/nbl/libvterm.LICENSE) return 0 ;;
        share/nbl/*.nanorc) case "${1#share/nbl/}" in */*) return 1 ;; *) return 0 ;; esac ;;
        *) return 1 ;;
    esac
}

check_parent() {
    directory=$prefix
    remainder=${1%/*}
    while [ -n "$remainder" ]; do
        directory="$directory/${remainder%%/*}"
        [ ! -L "$directory" ] || die "Destination directory is a symlink: $directory"
        case "$remainder" in */*) remainder=${remainder#*/} ;; *) break ;; esac
    done
}

lock_prefix() {
    candidate="$prefix/share/nbl/.nbl-install-lock"
    mkdir -- "$candidate" || die "Could not lock the NBL installation in $prefix."
    lock=$candidate
}

digest() {
    sum=$(sha256sum < "$1")
    printf '%s\n' "${sum%% *}"
}
