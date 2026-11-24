#!/bin/sh
set -eu
BASE=/storage/c1-video/device-cache
mkdir -p "${C1_VIDEO_CACHE:-$BASE}"
EXPECTED=1744d3c04f6614f2c0d079f81adcdbfa0268ce44834a7e396d0a5a327280dbfc
actual=$(sha256sum /usr/bin/d261/mpenMain); actual=${actual%% *}
[ "$actual" = "$EXPECTED" ] || { echo 'Unverified firmware'; exit 2; }
ui=$(pidof mpenMain)
case "$ui" in ''|*[!0-9]*) exit 2;; esac
hotkey=$(pidof c1hotkey || true)
case "$hotkey" in '') hotkey=0;; *[!0-9]*) exit 2;; esac
exec "$BASE/c1-video-unified" "$ui" "$hotkey" > "/storage/c1-video/unified.log" 2>&1