#!/bin/bash
# checks an appimage works on clean installs of a few distros: renders a video with the cli, makes sure the only
# libraries the gui can't find are ones every desktop has, then installs those and checks the gui opens a window
# usage: ci/linux/test/distros.sh <appimage> [image...]
set -euo pipefail

appimage="$(realpath "$1")"
shift
images=("$@")
if [ ${#images[@]} -eq 0 ]; then
  images=(ubuntu:22.04 debian:12 fedora:latest archlinux:latest)
fi

linux_dir="$(dirname "$(dirname "$(realpath "$0")")")"

failed=()
for image in "${images[@]}"; do
  echo "::group::$image"

  if docker run --rm --platform linux/amd64 \
    -v "$appimage:/blur.AppImage:ro" -v "$linux_dir:/linux:ro" \
    "$image" bash /linux/test/in-container.sh /blur.AppImage; then
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
