#!/bin/bash
# the part of test-linux-appimage.sh that runs inside each distro's container
# usage: ci/test-linux-appimage-in-container.sh <appimage>
set -euo pipefail

appimage="$(realpath "$1")"
ci_dir="$(dirname "$(realpath "$0")")"

cd /tmp
# extracting doesn't need fuse, which containers don't have
"$appimage" --appimage-extract >/dev/null
app=/tmp/squashfs-root

"$app/ffmpeg/ffmpeg" -v error -f lavfi -i testsrc2=size=320x240:rate=60 -t 1 -pix_fmt yuv420p -c:v libx264 in.mp4

"$app/AppRun" cli -i in.mp4 -o out.mp4 -v

frames="$("$app/ffmpeg/ffprobe" -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames \
  -of csv=p=0 out.mp4)"
echo "rendered $frames frames"
[ "$frames" -gt 0 ]

missing="$(ldd "$app/blur" "$app/libmpv.so.2" | awk '/not found/ { print $1 }' | sort -u)"
unexpected="$(grep -vxF -f <(grep -v '^#' "$ci_dir/appimage-excludelist" | awk 'NF { print $1 }') <<<"$missing" || true)"
if [ -n "$unexpected" ]; then
  echo "gui needs libraries that aren't bundled:" >&2
  echo "$unexpected" >&2
  exit 1
fi
