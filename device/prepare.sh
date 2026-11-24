#!/bin/sh
set -eu
BASE=${C1_VIDEO_CACHE:-/storage/c1-video/device-cache}
SOURCE=${C1_VIDEO_SOURCE:-/storage/mtp/video/compose_video_296x152_gray_30fps.mp4}
mkdir -p "$BASE"
cd "$BASE"
exec 9>prepare.lock
flock -n 9 || exit 3
rm -f raw.pipe ready source.meta progress.txt osmo-device.s16le.part
mkfifo raw.pipe
pack=0
ff=0
cleanup() { [ "$ff" -le 1 ] || kill "$ff" 2>/dev/null || true; [ "$pack" -le 1 ] || kill "$pack" 2>/dev/null || true; rm -f raw.pipe; }
trap cleanup EXIT
trap 'exit 130' INT TERM
/storage/c1-video/device-cache/pack-gray osmo-device.c1v.part <raw.pipe 2>pack.log &
pack=$!
/usr/bin/ffmpeg -nostdin -hide_banner -i "$SOURCE" 2>probe.log || true
if grep -q 'Audio:' probe.log; then
/usr/bin/ffmpeg -nostdin -hide_banner -nostats -benchmark -progress progress.txt -stats_period 1 -threads 1 -filter_threads 1 -i "$SOURCE" -map 0:v:0 -an -vf "transpose=dir=clock:passthrough=landscape,scale=296:152:force_original_aspect_ratio=decrease:flags=area,pad=296:152:(ow-iw)/2:(oh-ih)/2:color=white,fps=30,format=gray" -pix_fmt gray -f rawvideo -y raw.pipe -map 0:a:0 -vn -ac 1 -ar 48000 -f s16le osmo-device.s16le.part 2>decode.log &
else
/usr/bin/ffmpeg -nostdin -hide_banner -nostats -benchmark -progress progress.txt -stats_period 1 -threads 1 -filter_threads 1 -i "$SOURCE" -map 0:v:0 -an -vf "transpose=dir=clock:passthrough=landscape,scale=296:152:force_original_aspect_ratio=decrease:flags=area,pad=296:152:(ow-iw)/2:(oh-ih)/2:color=white,fps=30,format=gray" -pix_fmt gray -f rawvideo -y raw.pipe 2>decode.log &
fi
ff=$!
rc=0
wait "$ff" || rc=$?
ff=0
wait "$pack" || rc=$?
pack=0
[ "$rc" = 0 ] || { echo "FAILED rc=$rc"; exit "$rc"; }
/storage/c1-video/device-cache/pack-gray --align-audio osmo-device.c1v.part osmo-device.s16le.part
mv osmo-device.c1v.part osmo-device.c1v
mv osmo-device.s16le.part osmo-device.s16le
sync
printf 'READY\n' > ready
echo DECODE_READY