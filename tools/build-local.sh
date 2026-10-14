#!/usr/bin/env bash
# Build NPCReplacer.dll on Linux with clang-cl + xwin (preset "local").
#   tools/build-local.sh            build only
#   tools/build-local.sh --install  build, then copy the DLL into the MO2 mod folder
# Defaults: xwin at ~/.local/share/xwin, vcpkg at ~/.local/src/vcpkg,
# CommonLibVR (ng) at ~/.local/src/CommonLibVR. Override with XWIN_DIR, VCPKG_ROOT,
# COMMONLIB_SSE_FOLDER, MO2_MOD_DIR.
set -euo pipefail

XWIN_DIR="${XWIN_DIR:-$HOME/.local/share/xwin}"
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/.local/src/vcpkg}"
export COMMONLIB_SSE_FOLDER="${COMMONLIB_SSE_FOLDER:-$HOME/.local/src/CommonLibVR}"
export INCLUDE="$XWIN_DIR/crt/include;$XWIN_DIR/sdk/include/ucrt;$XWIN_DIR/sdk/include/um;$XWIN_DIR/sdk/include/shared;$XWIN_DIR/sdk/include/winrt"
export LIB="$XWIN_DIR/crt/lib/x86_64;$XWIN_DIR/sdk/lib/um/x86_64;$XWIN_DIR/sdk/lib/ucrt/x86_64"
MO2_MOD_DIR="${MO2_MOD_DIR:-$HOME/Games/Skyrim/MO2/mods/NPC Visual Editor}"

cd "$(dirname "$0")/.."
[[ -f build/local/build.ninja ]] || cmake --preset local
cmake --build build/local

dll="build/local/NPCReplacer.dll"
echo "built: $dll ($(sha256sum "$dll" | cut -c1-16))"

if [[ "${1:-}" == "--install" ]]; then
    if pgrep -x SkyrimSE.exe >/dev/null; then
        echo "SkyrimSE.exe is running; not replacing a loaded DLL." >&2
        exit 1
    fi
    mkdir -p "$MO2_MOD_DIR/SKSE/Plugins"
    cp "$dll" "$MO2_MOD_DIR/SKSE/Plugins/"
    echo "installed: $MO2_MOD_DIR/SKSE/Plugins/NPCReplacer.dll"
fi
