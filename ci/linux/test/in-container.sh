#!/bin/bash
# the part of distros.sh that runs inside each distro's container
# usage: ci/linux/test/in-container.sh <appimage> <extracted tarball's blur folder>
set -euo pipefail

appimage="$(realpath "$1")"
tar_app="$(realpath "$2")"
linux_dir="$(dirname "$(dirname "$(realpath "$0")")")"

cd /tmp
# extracting doesn't need fuse, which containers don't have
"$appimage" --appimage-extract >/dev/null
app=/tmp/squashfs-root

"$app/ffmpeg/ffmpeg" -v error -f lavfi -i testsrc2=size=320x240:rate=60 -t 1 -pix_fmt yuv420p -c:v libx264 in.mp4

render() {
  local name="$1"
  shift

  "$@" -i in.mp4 -o "$name.mp4" -v

  local frames
  frames="$("$app/ffmpeg/ffprobe" -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames \
    -of csv=p=0 "$name.mp4")"
  echo "$name: rendered $frames frames"
  [ "$frames" -gt 0 ]
}

render appimage "$app/AppRun" cli
render tarball "$tar_app/blur-cli"

missing="$(ldd "$app/blur" "$app/libmpv.so.2" "$tar_app/blur" "$tar_app/libmpv.so.2" |
  awk '/not found/ { print $1 }' | sort -u)"
unexpected="$(grep -vxF -f <(grep -v '^#' "$linux_dir/appimage/excludelist" | awk 'NF { print $1 }') <<<"$missing" || true)"
if [ -n "$unexpected" ]; then
  echo "gui needs libraries that aren't bundled:" >&2
  echo "$unexpected" >&2
  exit 1
fi

# install what a desktop has (an x server, a graphics driver and the libraries above) and check the gui opens. it
# exits straight away if it can't, so still running when timeout stops it means it worked
if command -v apt-get >/dev/null; then
  apt-get update -qq
  DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
    xvfb xauth libegl1 libegl-mesa0 libgles2 libgl1-mesa-dri libasound2 libfontconfig1 libharfbuzz0b libfribidi0 >/dev/null
elif command -v dnf >/dev/null; then
  dnf install -y -q \
    xorg-x11-server-Xvfb mesa-libEGL mesa-dri-drivers libglvnd-gles alsa-lib fontconfig harfbuzz fribidi >/dev/null
elif command -v pacman >/dev/null; then
  pacman -Syu --noconfirm --needed \
    xorg-server-xvfb xorg-xauth mesa libglvnd alsa-lib fontconfig harfbuzz fribidi >/dev/null
fi

check_gui() {
  local name="$1"
  shift

  local status=0
  xvfb-run -a timeout 15 "$@" >"$name-gui.log" 2>&1 || status=$?
  if [ "$status" -ne 124 ]; then
    echo "$name: gui exited with $status instead of staying open:" >&2
    cat "$name-gui.log" >&2
    exit 1
  fi
  echo "$name: gui stayed open"
}

check_gui appimage "$app/AppRun"
check_gui tarball "$tar_app/blur"
