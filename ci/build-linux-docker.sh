#!/bin/bash
# builds the linux appimage in the same environment as ci, from any os with docker. the result ends up in dist/.
# build outputs live in docker volumes so they don't clash with a native build in the same checkout
set -euo pipefail

repo_dir="$(dirname "$(dirname "$(realpath "$0")")")"
image=blur-linux-build

docker build --platform linux/amd64 -t "$image" -f "$repo_dir/ci/linux.Dockerfile" "$repo_dir/ci"

mkdir -p "$repo_dir/dist"

docker run --rm --platform linux/amd64 \
  -v "$repo_dir:/src" \
  -v blur-linux-ci-out:/src/ci/out \
  -v blur-linux-ci-download:/src/ci/download \
  -v blur-linux-ci-appdir:/src/ci/appdir \
  -v blur-linux-out:/src/out \
  -v blur-linux-bin:/src/bin \
  -v blur-linux-vcpkg:/vcpkg \
  -e VCPKG_ROOT=/vcpkg \
  -w /src \
  "$image" bash ci/build-linux-in-container.sh
