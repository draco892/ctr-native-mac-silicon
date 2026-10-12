#!/bin/bash
# Clean complete-game build for macOS Apple Silicon, including CTest.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: ./build-macos.sh [--sanitized] [--jobs N]

Default: clean ARM64 game build and tests in build-macos-arm64-game/.
--sanitized  Use ASan/UBSan and build-macos-arm64-game-sanitized/.
--jobs N     Set build parallelism (otherwise CMAKE_BUILD_PARALLEL_LEVEL
             or the host CPU count).

Requires macOS Apple Silicon, Xcode Command Line Tools, CMake >= 3.24,
Ninja, and Python 3. Assets, JSON settings, and saves are preserved.
EOF
}

fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

preset=macos-arm64-game
jobs=${CMAKE_BUILD_PARALLEL_LEVEL:-}
while [[ $# -gt 0 ]]; do
    case "$1" in
        --sanitized) preset=macos-arm64-game-sanitized; shift ;;
        --jobs)
            [[ $# -ge 2 ]] || fail '--jobs requires a positive integer.'
            jobs=$2
            shift 2
            ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; fail "Unknown option: $1" ;;
    esac
done

[[ $(uname -s) == Darwin ]] || fail 'This script requires macOS.'
if [[ $(uname -m) != arm64 && $(sysctl -n hw.optional.arm64 2>/dev/null || true) != 1 ]]; then
    fail 'The complete macOS game preset requires an Apple Silicon Mac.'
fi

for dependency in cmake ctest ninja python3 xcrun; do
    command -v "$dependency" >/dev/null 2>&1 || fail "$dependency not found in PATH. See README.md for build requirements."
done
xcrun --find clang >/dev/null 2>&1 || fail 'Apple Clang not found. Install Xcode Command Line Tools with: xcode-select --install'
xcrun --show-sdk-path >/dev/null 2>&1 || fail 'The macOS SDK is unavailable. Check the selected Xcode/Command Line Tools installation.'

cmake_version=$(cmake --version)
if [[ ! $cmake_version =~ cmake\ version\ ([0-9]+)\.([0-9]+) ]]; then
    fail 'Cannot determine the installed CMake version.'
fi
if (( BASH_REMATCH[1] < 3 || (BASH_REMATCH[1] == 3 && BASH_REMATCH[2] < 24) )); then
    fail 'CMake 3.24 or newer is required for --fresh configuration.'
fi

if [[ -z $jobs ]]; then jobs=$(sysctl -n hw.ncpu); fi
[[ $jobs =~ ^[1-9][0-9]*$ ]] || fail 'Build parallelism must be a positive integer.'

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
cd -- "$repo_dir"

printf 'Clean build: %s (%s parallel jobs)\n' "$preset" "$jobs"
# Reset CMake-generated configuration without deleting unrelated files in the
# build directory. Clean all generated targets before rebuilding, including SDL.
cmake --fresh --preset "$preset"
cmake --build --preset "$preset" --clean-first --parallel "$jobs"
ctest --preset "$preset" --output-on-failure

executable="$repo_dir/build-$preset/ctr_native"
"$executable" --version
"$executable" --config "$repo_dir/config.json" --check-config
printf '\nBuild and tests succeeded: %s\n' "$executable"
printf 'Run: ./build-%s/ctr_native\n' "$preset"
