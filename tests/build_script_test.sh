#!/usr/bin/env bash

set -euo pipefail

readonly SOURCE_DIRECTORY="$({
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
    pwd
})"
readonly TEST_DIRECTORY="$(mktemp -d "${TMPDIR:-/tmp}/docklight-build-script.XXXXXX")"

cleanup()
{
    rm -rf -- "${TEST_DIRECTORY}"
}

trap cleanup EXIT

mkdir -p \
    "${TEST_DIRECTORY}/bin" \
    "${TEST_DIRECTORY}/build/src"

printf '%s\n' \
    'CFLAGS = -O0 -g3' \
    'CXXFLAGS = -O0 -g3 -DDEBUG' \
    >"${TEST_DIRECTORY}/build/Makefile"

printf '%s\n' \
    '#!/usr/bin/env bash' \
    'exit 0' \
    >"${TEST_DIRECTORY}/bin/make"
chmod +x "${TEST_DIRECTORY}/bin/make"

printf '%s\n' \
    '#!/usr/bin/env bash' \
    'printf "arguments:%s\\n" "$*"' \
    >"${TEST_DIRECTORY}/build/src/docklight6"
chmod +x "${TEST_DIRECTORY}/build/src/docklight6"

run_build()
{
    env \
        PATH="${TEST_DIRECTORY}/bin:${PATH}" \
        DOCKLIGHT_BUILD_DIR="${TEST_DIRECTORY}/build" \
        "$@" \
        "${SOURCE_DIRECTORY}/build.sh" debug --run
}

x11_output="$(run_build \
    XDG_SESSION_TYPE=x11 \
    WAYLAND_DISPLAY=)"
grep -Fq \
    'Non-Wayland session detected; using native presentation' \
    <<<"${x11_output}"
grep -Fq 'arguments:--presentation=native' \
    <<<"${x11_output}"

wayland_output="$(run_build \
    XDG_SESSION_TYPE=wayland \
    WAYLAND_DISPLAY=wayland-test)"
if grep -Fq -- '--presentation=native' \
    <<<"${wayland_output}"
then
    echo "Wayland launch was incorrectly forced to native presentation" >&2
    exit 1
fi
grep -Fq 'arguments:' <<<"${wayland_output}"

build_only_output="$(env \
    PATH="${TEST_DIRECTORY}/bin:${PATH}" \
    DOCKLIGHT_BUILD_DIR="${TEST_DIRECTORY}/build" \
    XDG_SESSION_TYPE=x11 \
    WAYLAND_DISPLAY= \
    "${SOURCE_DIRECTORY}/build.sh" debug)"
if grep -Fq 'using native presentation' \
    <<<"${build_only_output}"
then
    echo "Build-only command reported an unused presentation override" >&2
    exit 1
fi
