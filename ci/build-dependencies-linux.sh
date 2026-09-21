#!/bin/bash
set -e

out_dir=out

echo "Building dependencies for Linux"

# clean outputs every run
rm -rf $out_dir
mkdir -p $out_dir

download_archive() {
  local url="$1"
  local dir_name="$2"
  local out_path="$3"
  local subfolder="$4"

  original_dir=$(pwd)

  mkdir -p download
  cd download

  if [ -d "$dir_name" ]; then
    echo "$dir_name already exists. Skipping download."
    cd "$dir_name"
  else
    mkdir -p "$dir_name" && cd "$dir_name"

    echo "Downloading $dir_name"

    if [[ "$url" == *.zip ]]; then
      wget -q "$url" -O "$dir_name.zip"
      unzip "$dir_name.zip"
      rm "$dir_name.zip"
    elif [[ "$url" == *.tar.xz ]]; then
      wget -q "$url" -O "$dir_name.tar.xz"
      tar -xf "$dir_name.tar.xz"
      rm "$dir_name.tar.xz"
    else
      echo "Unsupported archive format: $url"
      cd "$original_dir"
      return 1
    fi
  fi

  dest_path="$original_dir/$out_dir/$out_path"

  echo "Copying files from $subfolder to $dest_path"

  cp -r "${subfolder:=.}" "$dest_path"

  cd "$original_dir"
}

download_library() {
  local url="$1"
  local filename="$2"
  local out_path="$3"
  local sha256="$4"
  local dir_name="${filename%.*}" # Remove file extension to get dir name

  mkdir -p download/$dir_name
  cd download/$dir_name

  if [ ! -f "$filename" ]; then
    echo "Downloading $filename..."
    wget -q "$url" -O "$filename"

    if ! echo "$sha256  $filename" | sha256sum -c --quiet -; then
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

# blur doesn't ship vapoursynth on linux, so there's no python to install plugins into - pip just fetches them. their
# bundled libraries have hashed names that don't end in .so, so the plugin loader ignores them
install_pypi_plugins() {
  local target="download/pypi"
  local plugins_dir="$out_dir/vapoursynth-plugins"

  rm -rf "$target"
  # pip doesn't count a newer manylinux tag as covering older ones, so every tag the wheels use is listed
  python3 -m pip install --no-deps --only-binary=:all: \
    --platform manylinux_2_35_x86_64 --platform manylinux_2_28_x86_64 --platform manylinux_2_27_x86_64 \
    --target "$target" "$@"

  mkdir -p "$plugins_dir"
  find "$target/vapoursynth/plugins" -maxdepth 2 -name "*.so" | while read -r plugin; do
    cp "$plugin" "$plugins_dir"
    patchelf --set-rpath '$ORIGIN' "$plugins_dir/$(basename "$plugin")"
  done

  find "$target" -maxdepth 1 -name "vapoursynth_*.libs" -exec sh -c 'cp "$1"/* "$2"' _ {} "$plugins_dir" \;
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

## svpflow
download_archive \
  "https://web.archive.org/web/20190322064557if_/http://www.svp-team.com/files/gpl/svpflow-4.2.0.142.zip" \
  "svpflow" \
  "vapoursynth-plugins" \
  "svpflow-4.2.0.142/lib-linux"

# plugins on pypi
install_pypi_plugins \
  vapoursynth-akarin==1.5.0 \
  vapoursynth-bestsource==21.0 \
  vapoursynth-lsmas==1310.0.0.0 \
  vapoursynth-mvtools==29

# frameblender
download_library \
  "https://github.com/f0e/vs-frameblender/releases/download/v2.1/frameblender-linux-x64.so" \
  "libframeblender.so" \
  "vapoursynth-plugins" \
  "624269bbca909d3be6e9437c5b5a2594b59d3135e39609fcc46606f8d4c979ee"

# fmtconv
download_library \
  "https://github.com/f0e/blur-plugin-builds/releases/download/build-20260512-f81d24b/libfmtconv.so" \
  "libfmtconv.so" \
  "vapoursynth-plugins" \
  "b0ec80d5eacd0028365ca76fe3e296beb2ac2db43bd46acc064ef1b28d33ef26"

# rife-ncnn-vulkan
download_library \
  "https://github.com/styler00dollar/VapourSynth-RIFE-ncnn-Vulkan/releases/download/r9_mod_v33/librife_linux_x86-64.so" \
  "librife_linux_x86-64.so" \
  "vapoursynth-plugins" \
  "18a00a5e3ac90a5dfcfdf85fdb81d977381a5a2560a4c45dbe9a469d007b1886"

# rife model
download_model_files \
  "https://raw.githubusercontent.com/styler00dollar/VapourSynth-RIFE-ncnn-Vulkan/c3ec6aabc07c8fa37a4f58d7fed9e2ad1fc1b13f/models/rife-v4.26_ensembleFalse" \
  "rife-v4.26_ensembleFalse" \
  "flownet.bin" "flownet.param"

echo "done"
