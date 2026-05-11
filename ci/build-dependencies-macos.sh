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

    wget -q "$url" -O "$dir_name.zip"
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

download_library() {
  local url="$1"
  local filename="$2"
  local out_path="$3"
  local dir_name="${filename%.*}" # Remove file extension to get dir name

  mkdir -p download/$dir_name
  cd download/$dir_name

  if [ ! -f "$filename" ]; then
    echo "Downloading $filename..."
    wget -q "$url" -O "$filename"
  else
    echo "$filename already exists. Skipping download."
  fi

  # copy to output directory
  dest_path="../../$out_dir/$out_path"
  mkdir -p "$dest_path"
  echo "Copying $filename to $dest_path"
  cp "$filename" "$dest_path"

  cd ../..
}

download_model_files() {
  local base_url="$1"
  local model_name="$2"
  local file_list=("${@:3}")

  echo "Downloading model: $model_name"
  local model_dir="$out_dir/models/$model_name"

  mkdir -p "$model_dir"
  echo "Created directory: $model_dir"

  for file in "${file_list[@]}"; do
    local file_url="$base_url/$file"
    local output_path="$model_dir/$file"

    echo "Downloading $file_url to $output_path"
    wget -q "$file_url" -O "$output_path"
  done

  echo "Model $model_name download completed"
}

build() {
  local repo="$1"
  local pull_args="$2"
  local commit="$3"
  local name="$4"
  local build_cmd="$5"
  local lib_path="$6"
  local out_path="$7"

  echo "--- Building $name ---"

  mkdir -p build
  cd build

  if [ ! -d "$name" ]; then
    echo "Cloning $name..."
    # shellcheck disable=SC2086
    git clone $pull_args "$repo" "$name"
    cd "$name"

    if [ ! -z "$commit" ]; then
      echo "Checking out commit $commit..."
      git checkout "$commit"
    fi
  else
    echo "Repository $name already exists"
    cd "$name"
    git fetch origin
    if [ ! -z "$commit" ]; then
      echo "Checking out commit $commit..."
      git checkout "$commit"
    else
      echo "Pulling"
      git pull
    fi
  fi

  eval "$build_cmd"

  # copy built stuff
  dest_path="../../$out_dir/$out_path"
  mkdir -p "$dest_path"

  if [[ -n "$lib_path" ]]; then
    echo "Copying $name libraries to $dest_path"
    find "$lib_path" -name "*.dylib" -exec cp {} "$dest_path" \;
  else
    echo "Skipping copy: lib_path is empty"
  fi

  cd ../..
}

# downloads
## ffmpeg (static)
download_zip \
  "https://ffmpeg.martin-riedl.de/download/macos/arm64/1744739657_N-119265-g0040d7e608/ffmpeg.zip" \
  "ffmpeg" \
  "ffmpeg"

## ffprobe (static) todo: shared? for smaller size?
download_zip \
  "https://ffmpeg.martin-riedl.de/download/macos/arm64/1744739657_N-119265-g0040d7e608/ffprobe.zip" \
  "ffprobe" \
  "ffmpeg"

## svpflow
echo "Downloading SVPFlow libraries..."
download_library \
  "https://github.com/Spritzerland/svpflow-arm64/raw/4922e4bcfeb0ee80d80555ac54b4f0e92e4d6316/libsvpflow1_arm.dylib" \
  "libsvpflow1_arm.dylib" \
  "vapoursynth-plugins"

download_library \
  "https://github.com/Spritzerland/svpflow-arm64/raw/4922e4bcfeb0ee80d80555ac54b4f0e92e4d6316/libsvpflow2_arm.dylib" \
  "libsvpflow2_arm.dylib" \
  "vapoursynth-plugins"

download_library \
  "https://github.com/f0e/Vapoursynth-adjust/releases/latest/download/libadjust.dylib" \
  "libadjust.dylib" \
  "vapoursynth-plugins"

# ## RIFE ncnn Vulkan library
# echo "Downloading RIFE ncnn Vulkan library..."
# download_library \
#   "https://github.com/styler00dollar/VapourSynth-RIFE-ncnn-Vulkan/releases/download/r9_mod_v32/librife_macos_arm64.dylib" \
#   "librife_macos_arm64.dylib" \
#   "vapoursynth-plugins"

## python for vapoursynth
mkdir -p download/python
cd download/python

if [ ! -d "python" ]; then
  wget -q https://github.com/astral-sh/python-build-standalone/releases/download/20250317/cpython-3.12.9+20250317-aarch64-apple-darwin-install_only.tar.gz -O python.tar.gz
  mkdir -p python
  tar -xzf python.tar.gz -C python --strip-components 1
  rm python.tar.gz
fi

# copy python to output directory
python_dest_path="../../$out_dir/python"
mkdir -p "$python_dest_path"
cp -R python/* "$python_dest_path"

cd ../..

# it uses /install - change it to what it actually is, it'll be changed later anyway by dylibbundler (https://gregoryszorc.com/docs/python-build-standalone/main/quirks.html)
install_name_tool -change /install/lib/libpython3.12.dylib "$PWD/$out_dir/python/lib/libpython3.12.dylib" $out_dir/python/lib/libpython3.12.dylib
install_name_tool -id "$PWD/$out_dir/python/lib/libpython3.12.dylib" $out_dir/python/lib/libpython3.12.dylib

$out_dir/python/bin/pip install --upgrade pip
$out_dir/python/bin/pip install cython

# builds
## vapoursynth

PATH="$PWD/$out_dir/python/bin:$PATH"
PYTHON_PREFIX="$PWD/$out_dir/python"

build "https://github.com/vapoursynth/vapoursynth.git" "--single-branch" "e46204429041e95a881b61eedddd46c08f9a307c" "vapoursynth" "
./autogen.sh
PYTHON3_LIBS=\"-L$PYTHON_PREFIX/lib/python3.12 -L$PYTHON_PREFIX/lib -lpython3.12\" \
  PYTHON3_CFLAGS=\"-I$PYTHON_PREFIX/include/python3.12\" \
  ./configure --with-python_prefix=\"$PYTHON_PREFIX\" --with-cython=\"$PYTHON_PREFIX/bin/cython\"
make
sudo make install
" "" "vapoursynth"

### copy vspipe
cp build/vapoursynth/.libs/vspipe $out_dir/vapoursynth

## bestsource
build "https://github.com/vapoursynth/bestsource.git" "--single-branch --recurse-submodules --shallow-submodules --remote-submodules" "c2be08527100a363e0018bc907c73644737b3953" "bestsource" "
meson setup build
ninja -C build
" "build" "vapoursynth-plugins"

## mvtools
build "https://github.com/dubhater/vapoursynth-mvtools.git" "--single-branch" "e516e90f9618a20c2dc06be05935d2abbb5f691b" "mvtools" "
meson setup build
ninja -C build
" "build" "vapoursynth-plugins"

## rife ncnn vulkan
build "https://github.com/styler00dollar/VapourSynth-RIFE-ncnn-Vulkan.git" "--single-branch" "48da541a7b1f1a71678d2325faa6cf2bd9ef8382" "rife-ncnn-vulkan" "
git submodule update --init --recursive --depth 1
meson build
ninja -C build
" "build" "vapoursynth-plugins"

## fmtconv
build "https://gitlab.com/EleonoreMizo/fmtconv.git" "--single-branch" "259b702e4e3c1e2fc6d7b2c8e83d95b612519e89" "fmtconv" "
cd build/unix
./autogen.sh
./configure
make
cd ../..
" "build/unix/.libs" "vapoursynth-plugins"

PATH="/opt/homebrew/opt/llvm@20/bin:$PATH"
ZSTD_PREFIX=$(brew --prefix zstd)

## akarin
build "https://github.com/Jaded-Encoding-Thaumaturgy/akarin-vapoursynth-plugin.git" "--single-branch" "ed8ecd58722958891684a25fb883bbe626b8950a" "akarin" "
meson build
sed -i.bak \"s|-lzstd|-L${ZSTD_PREFIX}/lib -lzstd|g\" build/build.ninja # fuck you
ninja -C build
" "build" "vapoursynth-plugins"

# Define model downloads
echo "Starting model downloads..."

# Download RIFE models
download_model_files \
  "https://raw.githubusercontent.com/styler00dollar/VapourSynth-RIFE-ncnn-Vulkan/a2579e656dac7909a66e7da84578a2f80ccba41c/models/rife-v4.26_ensembleFalse" \
  "rife-v4.26_ensembleFalse" \
  "flownet.bin" "flownet.param"

echo "Model downloads completed"

echo "done"

echo "fixing all library dependencies with dylibbundler..."

for plugin in $out_dir/vapoursynth-plugins/*.dylib; do
  dylibbundler -cd -b -of -x "$plugin" -d "$out_dir/libs"
done

dylibbundler -cd -b -of -x "$out_dir/vapoursynth/vspipe" -d "$out_dir/libs"
dylibbundler -cd -b -of -x "$out_dir/python/lib/python3.12/site-packages/vapoursynth.so" -d "$out_dir/libs"
