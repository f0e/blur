#!/bin/bash
set -e

ARCH=$(uname -m)
out_dir=out

echo "Building dependencies for macOS ($ARCH)..."

# clean outputs every run
rm -rf $out_dir
mkdir -p $out_dir

download_zip() {
  local url="$1"
  local dir_name="$2"
  local out_path="$3"

  mkdir -p download
  cd download

  if [ -d "$dir_name" ]; then
    echo "$dir_name already exists. Skipping download."
    cd "$dir_name"
  else
    mkdir -p "$dir_name" && cd "$dir_name"

    echo "Downloading $dir_name"

    wget "$url" -O "$dir_name.zip"
    unzip "$dir_name.zip"
    rm "$dir_name.zip"
  fi

  # copy built stuff
  dest_path="../../$out_dir/$out_path"
  mkdir -p "$dest_path"

  echo "Copying $dir_name binaries to $dest_path"

  find . -type f -exec cp {} "$dest_path" \;

  cd ../..
}

build() {
  local repo="$1"
  local pull_args="$2"
  local name="$3"
  local build_cmd="$4"
  local lib_path="$5"
  local out_path="$6"

  echo "--- Building $name ---"

  mkdir -p build
  cd build

  if [ ! -d "$name" ]; then
    echo "Cloning $name..."
    # shellcheck disable=SC2086
    git clone $pull_args "$repo" "$name"
    cd "$name"
  else
    echo "Updating $name..."
    cd "$name"
    git pull
  fi

  eval "$build_cmd"

  # copy built stuff
  dest_path="../../$out_dir/$out_path"
  mkdir -p "$dest_path"

  echo "Copying $name libraries to $dest_path"

  find "$lib_path" -name "*.dylib" -exec cp {} "$dest_path" \;

  cd ../..
}

# downloads
## ffmpeg (static)
download_zip \
  "https://ffmpeg.martin-riedl.de/download/macos/arm64/1743700936_N-119137-g46762c8b82/ffmpeg.zip" \
  "ffmpeg" \
  "ffmpeg"

## svpflow
echo "Downloading SVPFlow libraries..."
mkdir -p download/svpflow
cd download/svpflow

if [ ! -f "libsvpflow1_arm.dylib" ] || [ ! -f "libsvpflow2_arm.dylib" ]; then
  echo "Downloading SVPFlow libraries from GitHub..."
  wget -q https://github.com/Spritzerland/svpflow-arm64/raw/main/libsvpflow1_arm.dylib
  wget -q https://github.com/Spritzerland/svpflow-arm64/raw/main/libsvpflow2_arm.dylib
fi

# copy libraries to output directory
dest_path="../../$out_dir/vapoursynth-plugins"
mkdir -p "$dest_path"
cp libsvpflow1_arm.dylib "$dest_path"
cp libsvpflow2_arm.dylib "$dest_path"

cd ../..

# builds
## vapoursynth
PATH="/opt/homebrew/opt/cython/bin:$PATH"

build "https://github.com/vapoursynth/vapoursynth.git" "" "vapoursynth" "
./autogen.sh
./configure
make
" ".libs" "vapoursynth"

### copy other needed stuff
mkdir -p $out_dir/vapoursynth/python3.12/site-packages
cp build/vapoursynth/.libs/vapoursynth.so $out_dir/vapoursynth/python3.12/site-packages
cp build/vapoursynth/.libs/vapoursynth.lai $out_dir/vapoursynth/python3.12/site-packages
cp build/vapoursynth/.libs/vspipe $out_dir/vapoursynth

### fix paths
install_name_tool -id libvapoursynth-script.0.dylib $out_dir/vapoursynth/libvapoursynth-script.0.dylib
install_name_tool -change /usr/local/lib/libvapoursynth-script.0.dylib @executable_path/libvapoursynth-script.0.dylib $out_dir/vapoursynth/vspipe
install_name_tool -change /usr/local/lib/libvapoursynth.dylib @executable_path/libvapoursynth.dylib $out_dir/vapoursynth/python3.12/site-packages/vapoursynth.so

## bestsource
build "https://github.com/vapoursynth/bestsource.git" "--depth 1 --recurse-submodules --shallow-submodules --remote-submodules" "bestsource" "
meson setup build
ninja -C build
" "build" "vapoursynth-plugins"

cp /opt/homebrew/opt/xxhash/lib/libxxhash.0.dylib $out_dir/vapoursynth
install_name_tool -change /opt/homebrew/opt/xxhash/lib/libxxhash.0.dylib @executable_path/libxxhash.0.dylib $out_dir/vapoursynth-plugins/libbestsource.dylib

## mvtools
build "https://github.com/dubhater/vapoursynth-mvtools.git" "" "mvtools" "
meson setup build
ninja -C build
" "build" "vapoursynth-plugins"

# ### additional deps
# cp /opt/homebrew/opt/fftw/lib/libfftw3f.3.dylib $out_dir/vapoursynth
# install_name_tool -change /opt/homebrew/opt/fftw/lib/libfftw3f.3.dylib @executable_path/libfftw3f.3.dylib $out_dir/vapoursynth-plugins/libmvtools.dylib

PATH="/opt/homebrew/opt/llvm@12/bin:$PATH"

## akarin
build "https://github.com/AkarinVS/vapoursynth-plugin" "" "akarin" "
meson build
ninja -C build
" "build" "vapoursynth-plugins"

echo "done"

# fix paths
for path in \
  $(pwd)/out/vapoursynth-plugins/libbestsource.dylib \
  $(pwd)/out/vapoursynth-plugins/libmvtools.dylib \
  $(pwd)/out/vapoursynth-plugins/libakarin.dylib; do
  echo "Fixing $path"
  fish -c "source collect-bin-with-deps.fish; collect-bin-with-deps $path"
done
