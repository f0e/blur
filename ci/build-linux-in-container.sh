#!/bin/bash
# the part of build-linux-docker.sh that runs inside the container, from the repo root
set -euo pipefail

# the source is owned by the host user
git config --global --add safe.directory "*"

(cd ci && bash build-dependencies-linux.sh)

baseline="$(jq -r '."vcpkg-configuration"."default-registry".baseline' vcpkg.json)"
if [ "$(git -C "$VCPKG_ROOT" rev-parse HEAD 2>/dev/null || true)" != "$baseline" ]; then
  git init -q "$VCPKG_ROOT"
  git -C "$VCPKG_ROOT" fetch -q --depth 1 https://github.com/microsoft/vcpkg.git "$baseline"
  git -C "$VCPKG_ROOT" checkout -q FETCH_HEAD
  "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
fi

export PKG_CONFIG_PATH="$PWD/ci/download/mpv-prefix/lib/pkgconfig"
cmake --preset linux-release
cmake --build --preset linux-release

ci/package-linux.sh bin/Release dist/blur-Linux-x64.AppImage
