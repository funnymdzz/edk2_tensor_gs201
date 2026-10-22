#!/bin/bash
# Repack the EDK2 UEFI payload into a Pixel 7 (panther) boot image using
# magiskboot, the most reliable way to produce an image ABL accepts.
#
# magiskboot handles the whole container for us:
#   unpack  -> detects header version, kernel compression format, page size
#   repack  -> reuses the original header and recompresses with the *same*
#              format it detected, so lz4_legacy vs lz4 frame is never wrong
#
# Usage:
#   ./tools/pack-panther-magiskboot.sh <stock-boot.img> <uefi-payload> [out.img]
#
#   <stock-boot.img>  stock boot_a.img of the target device (template)
#   <uefi-payload>    lz4_legacy-compressed UEFI payload
#                     (build output: workspace/uefi-<device>.img.gz)
#   [out.img]         default: boot-panther-magiskboot.img
#
# Requires magiskboot (x86_64). Set MAGISKBOOT=/path/to/magiskboot or drop the
# binary next to this script.
set -euo pipefail

STOCK="${1:?usage: $0 <stock-boot.img> <uefi-payload> [out.img]}"
PAYLOAD="${2:?usage: $0 <stock-boot.img> <uefi-payload> [out.img]}"
OUT="${3:-boot-panther-magiskboot.img}"

HERE="$(cd "$(dirname "$0")" && pwd)"
MAGISKBOOT="${MAGISKBOOT:-$HERE/magiskboot/magiskboot}"

[ -x "$MAGISKBOOT" ] || { echo "error: magiskboot not found at $MAGISKBOOT" >&2; exit 1; }
[ -f "$STOCK" ]      || { echo "error: stock image not found: $STOCK" >&2; exit 1; }
[ -f "$PAYLOAD" ]    || { echo "error: payload not found: $PAYLOAD" >&2; exit 1; }

STOCK="$(realpath "$STOCK")"
PAYLOAD="$(realpath "$PAYLOAD")"
OUT="$(realpath -m "$OUT")"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

echo "[magiskboot] unpacking stock image as template"
cp "$STOCK" stock.img
"$MAGISKBOOT" unpack -h stock.img

# Sanity: the template must be a Pixel-style v4 boot image with a bare kernel.
[ -f header ] || { echo "error: magiskboot produced no header" >&2; exit 1; }

echo "[magiskboot] replacing kernel with UEFI payload ($(stat -c%s "$PAYLOAD") bytes)"
cp "$PAYLOAD" kernel

echo "[magiskboot] repacking"
"$MAGISKBOOT" repack stock.img "$OUT"

echo "[magiskboot] done: $OUT"
echo "[magiskboot] verifying"
VERIFY_DIR="$(mktemp -d)"
cp "$OUT" "$VERIFY_DIR/verify.img"
# unpack decompresses the kernel, so compare against the decompressed payload
( cd "$VERIFY_DIR" && \
  "$MAGISKBOOT" unpack verify.img >/dev/null 2>&1 && \
  "$MAGISKBOOT" decompress "$PAYLOAD" payload.raw >/dev/null 2>&1 && \
  cmp -s kernel payload.raw && \
  echo "[magiskboot] payload verified: decompressed kernel matches decompressed payload" )
rm -rf "$VERIFY_DIR"
