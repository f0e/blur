#!/bin/bash
# checks an appimage and tarball work on clean installs of a few distros: renders a video with the cli, makes sure the
# only libraries the gui can't find are ones every desktop has, then installs those and checks the gui opens a window
# usage: ci/linux/test/distros.sh <appimage> <tarball> [image...]
set -euo pipefail

appimage="$(realpath "$1")"
tarball="$(realpath "$2")"
shift 2
images=("$@")
if [ ${#images[@]} -eq 0 ]; then
  images=(ubuntu:22.04 debian:12 fedora:latest archlinux:latest)
fi

linux_dir="$(dirname "$(dirname "$(realpath "$0")")")"

# extracted out here since minimal images don't all have tar
tarball_dir="$(mktemp -d)"
trap 'rm -rf "$tarball_dir"' EXIT
tar -xzf "$tarball" -C "$tarball_dir"

failed=()
for image in "${images[@]}"; do
  echo "::group::$image"

  if docker run --rm --platform linux/amd64 \
    -v "$appimage:/blur.AppImage:ro" -v "$tarball_dir/blur:/blur:ro" -v "$linux_dir:/linux:ro" \
    "$image" bash /linux/test/in-container.sh /blur.AppImage /blur; then
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
