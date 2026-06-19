#!/usr/bin/env bash
# bundle_dlls.sh - copy the MinGW/OpenCV runtime DLLs that
# <dir>/FocusKeeper.exe depends on into <dir>, so the build is self-contained.
#
# Uses objdump (reads the PE import table) instead of ldd: our exe's manifest
# requires admin elevation, which makes ldd fail with "Permission denied".
# Resolves dependencies transitively; only DLLs that live in /mingw64/bin are
# copied (Windows system DLLs are intentionally left out).
#
# Usage: bundle_dlls.sh <dir>
set -u

dir="${1:-}"
exe="$dir/FocusKeeper.exe"
if [ -z "$dir" ] || [ ! -f "$exe" ]; then
    echo "bundle_dlls: no $dir/FocusKeeper.exe, skipping" >&2
    exit 0
fi

MINGW_BIN="/mingw64/bin"
declare -A seen

imports() {
    objdump -p "$1" 2>/dev/null \
        | grep -i 'DLL Name:' \
        | sed -E 's/.*DLL Name:[[:space:]]*//I'
}

queue=()
while IFS= read -r n; do [ -n "$n" ] && queue+=("$n"); done < <(imports "$exe")

while [ ${#queue[@]} -gt 0 ]; do
    name="${queue[0]}"
    queue=("${queue[@]:1}")
    key="${name,,}"
    [ -n "${seen[$key]:-}" ] && continue
    seen[$key]=1

    src="$MINGW_BIN/$name"
    if [ -f "$src" ]; then
        cp -f "$src" "$dir/" 2>/dev/null || true
        while IFS= read -r d; do [ -n "$d" ] && queue+=("$d"); done < <(imports "$src")
    fi
done

echo "bundle_dlls: $(ls "$dir"/*.dll 2>/dev/null | wc -l) DLLs in $dir/"
