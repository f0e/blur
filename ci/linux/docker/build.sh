#!/bin/bash
# builds the linux appimage in the same environment as ci, from any os with docker. the result ends up in dist/.
# build outputs live in docker volumes so they don't clash with a native build in the same checkout
set -euo pipefail

script_dir="$(dirname "$(realpath "$0")")"
linux_dir="$(dirname "$script_dir")"
repo_dir="$(dirname "$(dirname "$linux_dir")")"
image=blur-linux-build

# the context is ci/linux so the image can install from install-build-deps.sh
docker build --platform linux/amd64 -t "$image" -f "$script_dir/Dockerfile" "$linux_dir"

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
  "$image" bash ci/linux/docker/in-container.sh
