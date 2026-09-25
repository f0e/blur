#!/bin/bash
# installs everything needed to build blur and its bundled libmpv on ubuntu 22.04. releases are built on the oldest
# distro still supported so they run on as many as possible, with a newer gcc on top for c++20
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get install -y --no-install-recommends software-properties-common gpg-agent
add-apt-repository -y ppa:ubuntu-toolchain-r/test

apt-get update
apt-get install -y --no-install-recommends \
  gcc-13 g++-13 \
  git curl wget ca-certificates zip unzip tar xz-utils zstd file jq \
  make pkg-config ninja-build \
  autoconf autoconf-archive automake libtool libltdl-dev gperf bison flex \
  python3 python3-pip python3-venv \
  libx11-dev libxcursor-dev libxinerama-dev libxi-dev libxrandr-dev libxext-dev libxfixes-dev libxss-dev \
  libxkbcommon-dev libwayland-dev wayland-protocols \
  libgl1-mesa-dev libegl-dev mesa-common-dev \
  libfontconfig1-dev libfreetype-dev libass-dev libfribidi-dev libharfbuzz-dev \
  libpulse-dev libasound2-dev libdbus-1-dev

update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 --slave /usr/bin/g++ g++ /usr/bin/g++-13

# the distro versions are too old for vcpkg, mpv and clearing executable stacks
python3 -m pip install --no-cache-dir cmake==3.31.6 meson==1.8.2 patchelf==0.19.1.0 jinja2
