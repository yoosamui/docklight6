#!/usr/bin/env bash

set -euo pipefail

readonly SOURCE_DIR="$(
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &&
        pwd
)"

usage()
{
    cat <<'EOF'
Usage: ./build.sh <debug|release> [OPTIONS]

Configure and build DockLight in a mode-specific out-of-source directory.

Options:
  --clean       Remove the selected build directory before building
  --clean-only  Remove the selected build directory and exit
  --check       Run the complete test suite after building
  --install     Install the complete project (uses sudo when needed)
  --run         Run the resulting binary after building/installing
  --gdb         Run the resulting binary under GDB
  --restart     Stop a running DockLight instance before --run or --gdb
  --jobs N      Use N parallel build jobs (default: number of CPUs)
  -h, --help    Show this help

Directories and flags can be overridden with:
  DOCKLIGHT_BUILD_DIR
  DEBUG_CFLAGS / DEBUG_CXXFLAGS
  RELEASE_CFLAGS / RELEASE_CXXFLAGS
  DOCKLIGHT_INSTALLED_BINARY
EOF
}

fail()
{
    echo "build.sh: $*" >&2
    exit 1
}

safe_build_directory()
{
    case "$1" in
    "$SOURCE_DIR"|/)
        fail "refusing to remove or build in unsafe directory: $1"
        ;;
    esac
}

remove_build_directory()
{
    local directory="$1"

    safe_build_directory "$directory"

    if [[ -d $directory ]]; then
        rm -rf -- "$directory"
        echo "Removed build directory: $directory"
    else
        echo "Build directory does not exist: $directory"
    fi
}

if (($# == 0)); then
    usage >&2
    exit 2
fi

case "$1" in
debug|release)
    mode="$1"
    shift
    ;;
-h|--help)
    usage
    exit 0
    ;;
*)
    fail "expected build mode 'debug' or 'release'"
    ;;
esac

clean=false
clean_only=false
run_checks=false
install_project=false
run_binary=false
run_gdb=false
restart_binary=false
jobs="$(nproc)"

while (($# > 0)); do
    case "$1" in
    --clean)
        clean=true
        ;;
    --clean-only)
        clean=true
        clean_only=true
        ;;
    --check)
        run_checks=true
        ;;
    --install)
        install_project=true
        ;;
    --run)
        run_binary=true
        ;;
    --gdb)
        run_gdb=true
        ;;
    --restart)
        restart_binary=true
        ;;
    --jobs)
        (($# >= 2)) || fail "--jobs requires a number"
        jobs="$2"
        shift
        ;;
    -h|--help)
        usage
        exit 0
        ;;
    *)
        fail "unknown option: $1"
        ;;
    esac
    shift
done

[[ $jobs =~ ^[1-9][0-9]*$ ]] ||
    fail "--jobs must be a positive integer"

if "$run_binary" && "$run_gdb"; then
    fail "--run and --gdb are mutually exclusive"
fi

if "$restart_binary" && ! "$run_binary" && ! "$run_gdb"; then
    fail "--restart requires --run or --gdb"
fi

if [[ $mode == debug ]]; then
    default_build_directory="$SOURCE_DIR/build-debug"
    build_cflags="${DEBUG_CFLAGS:-"-O0 -g3"}"
    build_cxxflags="${DEBUG_CXXFLAGS:-"-O0 -g3 -DDEBUG"}"
else
    default_build_directory="$SOURCE_DIR/build-release"
    build_cflags="${RELEASE_CFLAGS:-"-O2 -DNDEBUG"}"
    build_cxxflags="${RELEASE_CXXFLAGS:-"-O2 -DNDEBUG"}"
fi

build_directory="${DOCKLIGHT_BUILD_DIR:-$default_build_directory}"

if [[ $build_directory != /* ]]; then
    build_directory="$SOURCE_DIR/$build_directory"
fi

build_directory="$(realpath -m -- "$build_directory")"
safe_build_directory "$build_directory"

if "$clean"; then
    remove_build_directory "$build_directory"
fi

if "$clean_only"; then
    exit 0
fi

configure_required=false

if [[ ! -f $build_directory/Makefile ]]; then
    configure_required=true
elif ! grep -Fqx "CFLAGS = $build_cflags" "$build_directory/Makefile" ||
     ! grep -Fqx "CXXFLAGS = $build_cxxflags" "$build_directory/Makefile"; then
    echo "Build flags changed; cleaning existing compiler output"
    make -C "$build_directory" clean
    configure_required=true
fi

if "$configure_required"; then
    CFLAGS="$build_cflags" \
        CXXFLAGS="$build_cxxflags" \
        DOCKLIGHT_BUILD_DIR="$build_directory" \
        "$SOURCE_DIR/autogen.sh"
fi

make -C "$build_directory" -j"$jobs"

if "$run_checks"; then
    if [[ $mode == release ]]; then
        echo "Warning: NDEBUG disables assert()-based checks in C++ tests;"
        echo "run './build.sh debug --check' for complete validation."
    fi
    make -C "$build_directory" check
fi

if "$install_project"; then
    if ((EUID == 0)); then
        make -C "$build_directory" install
    else
        sudo make -C "$build_directory" install
    fi
fi

binary="$build_directory/src/docklight6"

if "$install_project"; then
    binary="${DOCKLIGHT_INSTALLED_BINARY:-/usr/local/bin/docklight6}"
fi

if "$restart_binary"; then
    pkill -x docklight6 2>/dev/null || true
fi

if "$run_gdb"; then
    command -v gdb >/dev/null 2>&1 ||
        fail "gdb is required for --gdb"
    exec gdb "$binary"
fi

if "$run_binary"; then
    exec "$binary"
fi

echo
echo "DockLight $mode build is ready: $build_directory/src/docklight6"

if "$install_project"; then
    echo "Installed DockLight binary: $binary"
fi
