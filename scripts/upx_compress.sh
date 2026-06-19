#!/usr/bin/env bash
# Compress the shippable binaries in $1 with UPX to shrink the release package.
#
# OpenCV drags in libopenblas.dll (~42 MB) which dominates the package; UPX cuts
# the folder ~78% and the zip ~45% with no functional change (DLLs self-extract
# at load). hook.dll is SKIPPED on purpose: it has writable shared sections
# (cross-process IPC) that UPX cannot pack.
#
# If upx is not installed this is a no-op (build still succeeds).
set -u

DIR="${1:?usage: upx_compress.sh <dir>}"

if ! command -v upx >/dev/null 2>&1; then
    echo "upx_compress: upx not found, skipping (package will be uncompressed)"
    exit 0
fi

targets=()
for f in "$DIR"/*.exe "$DIR"/*.dll; do
    [ -e "$f" ] || continue
    case "$(basename "$f")" in
        hook.dll) continue ;;   # writable shared sections, cannot pack
    esac
    targets+=("$f")
done

if [ "${#targets[@]}" -eq 0 ]; then
    echo "upx_compress: nothing to compress in $DIR"
    exit 0
fi

# --best --lzma gives the smallest output; failures on individual files are
# tolerated so one odd binary never breaks the release.
upx --best --lzma "${targets[@]}" || true
echo "upx_compress: done ($DIR)"
