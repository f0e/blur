#!/bin/bash
# installs everything needed to build and package blur on macos, with homebrew
set -euo pipefail

brew install \
  ffmpeg libass zimg imagemagick autoconf automake libtool gcc meson vapoursynth dylibbundler vulkan-loader \
  molten-vk mpv create-dmg
