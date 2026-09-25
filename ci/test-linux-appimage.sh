#!/bin/bash
# checks an appimage works on clean installs of a few distros: renders a video with the cli, and makes sure the only
# libraries the gui can't find are ones every desktop has
# usage: ci/test-linux-appimage.sh <appimage> [image...]
set -euo pipefail

appimage="$(realpath "$1")"
shift
images=("$@")
if [ ${#images[@]} -eq 0 ]; then
  images=(ubuntu:22.04 debian:12 fedora:latest archlinux:latest)
fi

ci_dir="$(dirname "$(realpath "$0")")"
excludelist="$ci_dir/appimage-excludelist"

failed=()
for image in "${images[@]}"; do
  echo "::group::$image"

  if docker run --rm --platform linux/amd64 \
    -v "$appimage:/blur.AppImage:ro" -v "$excludelist:/excludelist:ro" \
    "$image" bash -euo pipefail -c '
      cd /tmp
      # extracting doesnt need fuse, which containers dont have
      /blur.AppImage --appimage-extract >/dev/null
      app=/tmp/squashfs-root

      "$app/ffmpeg/ffmpeg" -v error -f lavfi -i testsrc2=size=320x240:rate=60 -t 1 -pix_fmt yuv420p -c:v libx264 in.mp4

      "$app/AppRun" cli -i in.mp4 -o out.mp4 -v

      frames="$("$app/ffmpeg/ffprobe" -v error -count_frames -select_streams v:0 -show_entries stream=nb_read_frames -of csv=p=0 out.mp4)"
      echo "rendered $frames frames"
      [ "$frames" -gt 0 ]

      missing="$(ldd "$app/blur" "$app/libmpv.so.2" | awk "/not found/ { print \$1 }" | sort -u)"
      unexpected="$(grep -vxF -f <(grep -v "^#" /excludelist | awk "NF { print \$1 }") <<<"$missing" || true)"
      if [ -n "$unexpected" ]; then
        echo "gui needs libraries that aren'\''t bundled:" >&2
        echo "$unexpected" >&2
        exit 1
      fi
    '; then
    echo "$image: ok"
  else
    echo "$image: FAILED"
    failed+=("$image")
  fi

  echo "::endgroup::"
done

if [ ${#failed[@]} -ne 0 ]; then
  echo "failed on: ${failed[*]}" >&2
  exit 1
fi
