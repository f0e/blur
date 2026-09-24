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
  local sha256="$4"

  mkdir -p download
  cd download

  if [ -d "$dir_name" ]; then
    echo "$dir_name already exists. Skipping download."
    cd "$dir_name"
  else
    mkdir -p "$dir_name" && cd "$dir_name"

    echo "Downloading $dir_name"

    wget -q "$url" -O "$dir_name.zip"

    if ! echo "$sha256  $dir_name.zip" | shasum -a 256 -c --quiet -; then
      echo "$url doesn't match its hash"
      rm "$dir_name.zip"
      return 1
    fi

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
  local sha256="$4"               # only needed when the url could change
  local dir_name="${filename%.*}" # Remove file extension to get dir name

  mkdir -p download/"$dir_name"
  cd download/"$dir_name"

  if [ ! -f "$filename" ]; then
    echo "Downloading $filename..."
    wget -q "$url" -O "$filename"

    if [ -n "$sha256" ] && ! echo "$sha256  $filename" | shasum -a 256 -c --quiet -; then
      echo "$url doesn't match its hash"
      rm "$filename"
      return 1
    fi
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

download_wheel() {
  local url="$1"
  local dir_name="$2"

  mkdir -p download/"$dir_name"
  cd download/"$dir_name"

  if [ ! -d "wheel" ]; then
    echo "Downloading $dir_name wheel..."
    wget -q "$url" -O wheel.zip # a wheel is just a zip
    unzip -q wheel.zip -d wheel
    rm wheel.zip
  else
    echo "$dir_name wheel already extracted. Skipping download."
  fi

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

  # cloning only recurses submodules at the default branch, which may not match the pinned commit
  if [ -f .gitmodules ]; then
    git submodule update --init --recursive --depth 1
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
  "ffmpeg" \
  "07bc646fa3a0ba2b6a9575ffe5e1e43abfe9accb060fd38bf76ea06a1e2de9ca"

## ffprobe (static) todo: shared? for smaller size?
download_zip \
  "https://ffmpeg.martin-riedl.de/download/macos/arm64/1744739657_N-119265-g0040d7e608/ffprobe.zip" \
  "ffprobe" \
  "ffmpeg" \
  "ec9b34a5abd0decc87b150809f82ef8352500b32db9496c917b5ffa8b5c5b8dd"

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

# ## RIFE ncnn Vulkan library
# echo "Downloading RIFE ncnn Vulkan library..."
# download_library \
#   "https://github.com/styler00dollar/VapourSynth-RIFE-ncnn-Vulkan/releases/download/r9_mod_v33/librife_macos_arm64.dylib" \
#   "librife_macos_arm64.dylib" \
#   "vapoursynth-plugins"

## python for vapoursynth
mkdir -p download/python
cd download/python

if [ ! -d "python" ]; then
  wget -q https://github.com/astral-sh/python-build-standalone/releases/download/20250317/cpython-3.12.9+20250317-aarch64-apple-darwin-install_only.tar.gz -O python.tar.gz
  echo "7c7fd9809da0382a601a79287b5d62d61ce0b15f5a5ee836233727a516e85381  python.tar.gz" | shasum -a 256 -c --quiet -
  mkdir -p python
  tar -xzf python.tar.gz -C python --strip-components 1
  rm python.tar.gz
fi

# copy python to output directory
python_dest_path="../../$out_dir/python"
mkdir -p "$python_dest_path"
cp -R python/* "$python_dest_path"

cd ../..

$out_dir/python/bin/pip install --upgrade pip

# the blur scripts need numpy. plugins on pypi install into vapoursynth's own plugins folder, which it autoloads - the
# ones whose wheels need macos 15 are built or extracted below instead
$out_dir/python/bin/pip install \
  numpy==2.5.3 \
  vapoursynth==79 \
  vapoursynth-akarin==1.5.0

# build the plugins against the vapoursynth we just bundled, not brew's. bestsource and mvtools find it through
# python, so meson's python is pointed at ours
meson_native="$PWD/download/meson-native.ini"
mkdir -p download
printf "[binaries]\npython = '%s'\n" "$PWD/$out_dir/python/bin/python3.12" >"$meson_native"

# rife reads libdir from the .pc, which ours lacks, and ours finds its headers relative to itself. so a copy with both
# filled in is used
vapoursynth_package="$PWD/$out_dir/python/lib/python3.12/site-packages/vapoursynth"
vapoursynth_pkgconfig="$PWD/download/vapoursynth-pkgconfig"
mkdir -p "$vapoursynth_pkgconfig"
{
  echo "libdir=$vapoursynth_package"
  sed "s|^prefix=.*|prefix=$vapoursynth_package|" "$vapoursynth_package/pkgconfig/vapoursynth.pc"
} >"$vapoursynth_pkgconfig/vapoursynth.pc"
export PKG_CONFIG_PATH="$vapoursynth_pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"

## bestsource
build "https://github.com/vapoursynth/bestsource.git" "--single-branch" "14c91f9fa74705facb251519096dc1b74a8632fe" "bestsource" "
meson setup build --native-file \"$meson_native\" -Denable_avisynth=false
ninja -C build
" "build" "vapoursynth-plugins"

## mvtools
build "https://github.com/dubhatervapoursynth/vapoursynth-mvtools.git" "--single-branch" "17250aa979616ac48dfb0e18abfdcf2bd4e3afc0" "mvtools" "
meson setup build --native-file \"$meson_native\"
ninja -C build
" "build" "vapoursynth-plugins"

## rife ncnn vulkan
build "https://github.com/styler00dollar/VapourSynth-RIFE-ncnn-Vulkan.git" "--single-branch" "c3ec6aabc07c8fa37a4f58d7fed9e2ad1fc1b13f" "rife-ncnn-vulkan" "
meson build
ninja -C build
" "build" "vapoursynth-plugins"

## fmtconv
build "https://gitlab.com/EleonoreMizo/fmtconv.git" "--single-branch" "f841f5ff94fd701484041068bb7124f5f393f9e0" "fmtconv" "
cd build/unix
./autogen.sh
./configure
make
cd ../..
" "build/unix/.libs" "vapoursynth-plugins"

## lsmash
download_wheel \
  "https://files.pythonhosted.org/packages/0a/3e/9ffe270c6c48d4c108a613931519edfc8ffecf39f50db9c0d57357654e64/vapoursynth_lsmas-1310.0.0.0-py3-none-macosx_15_0_arm64.whl" \
  "lsmas"

mkdir -p "$out_dir/vapoursynth-plugins"
cp download/lsmas/wheel/vapoursynth/plugins/liblsmashsource.dylib "$out_dir/vapoursynth-plugins"

## frameblender
download_library \
  "https://github.com/f0e/vs-frameblender/releases/download/v2.1/frameblender-macos-arm64.dylib" \
  "libframeblender.dylib" \
  "vapoursynth-plugins" \
  "dabbf2a507d0a63c30937836afdfb5613c885d70d85c424c19d88e84c3c0368f"

# Define model downloads
echo "Starting model downloads..."

# Download RIFE models
download_model_files \
  "https://raw.githubusercontent.com/styler00dollar/VapourSynth-RIFE-ncnn-Vulkan/c3ec6aabc07c8fa37a4f58d7fed9e2ad1fc1b13f/models/rife-v4.26_ensembleFalse" \
  "rife-v4.26_ensembleFalse" \
  "flownet.bin" "flownet.param"

echo "Model downloads completed"

echo "bundling MoltenVK..."

MOLTENVK_PREFIX="$(brew --prefix molten-vk)"

MOLTENVK_DEST="$out_dir/libs"
ICD_DEST="$out_dir/vulkan/icd.d"
MOLTENVK_JSON_SRC="$MOLTENVK_PREFIX/etc/vulkan/icd.d/MoltenVK_icd.json"
MOLTENVK_SRC="$MOLTENVK_PREFIX/lib/libMoltenVK.dylib"

mkdir -p "$MOLTENVK_DEST"
mkdir -p "$ICD_DEST"

if [ ! -f "$MOLTENVK_SRC" ]; then
  echo "ERROR: MoltenVK not found at $MOLTENVK_SRC"
  exit 1
fi

if [ ! -f "$MOLTENVK_JSON_SRC" ]; then
  echo "ERROR: MoltenVK ICD JSON not found at $MOLTENVK_JSON_SRC"
  exit 1
fi

echo "Copying libMoltenVK.dylib..."
cp "$MOLTENVK_SRC" "$MOLTENVK_DEST/"

# fix install name to be relative
install_name_tool -id "@rpath/libMoltenVK.dylib" \
  "$MOLTENVK_DEST/libMoltenVK.dylib"

echo "copying and patching MoltenVK_icd.json..."

cp "$MOLTENVK_JSON_SRC" "$ICD_DEST/MoltenVK_icd.json"

# Patch only library_path using jq
jq '.ICD.library_path = "../../libs/libMoltenVK.dylib"' \
  "$ICD_DEST/MoltenVK_icd.json" > \
  "$ICD_DEST/MoltenVK_icd.json.tmp"

mv "$ICD_DEST/MoltenVK_icd.json.tmp" \
  "$ICD_DEST/MoltenVK_icd.json"

echo "MoltenVK bundled successfully"

echo "fixing dylib permissions..."
chmod -R u+rwX,go+rX "$out_dir/libs"

echo "fixing all library dependencies with dylibbundler..."

dylibbundler -cd -b -of \
  -x "$MOLTENVK_DEST/libMoltenVK.dylib" \
  -d "$out_dir/libs"

for plugin in "$out_dir"/vapoursynth-plugins/*.dylib; do
  dylibbundler -cd -b -of -x "$plugin" -d "$out_dir/libs"
done

echo "done"
