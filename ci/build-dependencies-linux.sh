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

## svpflow
download_archive \
  "https://web.archive.org/web/20190322064557/http://www.svp-team.com/files/gpl/svpflow-4.2.0.142.zip" \
  "svpflow" \
  "vapoursynth-plugins" \
  "svpflow-4.2.0.142/lib-linux"

# bestsource
download_library \
  "https://github.com/f0e/blur-plugin-builds/releases/latest/download/bestsource.so" \
  "bestsource.so" \
  "vapoursynth-plugins"

download_library \
  "https://github.com/f0e/blur-plugin-builds/releases/latest/download/libbestsource.so" \
  "libbestsource.so" \
  "vapoursynth-plugins"

# akarin
download_library \
  "https://github.com/f0e/blur-plugin-builds/releases/latest/download/libakarin.so" \
  "libakarin.so" \
  "vapoursynth-plugins"

# mvtools
download_library \
  "https://github.com/f0e/blur-plugin-builds/releases/latest/download/libmvtools.so" \
  "libmvtools.so" \
  "vapoursynth-plugins"

# adjust
download_library \
  "https://github.com/f0e/Vapoursynth-adjust/releases/latest/download/libadjust.so" \
  "libadjust.so" \
  "vapoursynth-plugins"

# rife-ncnn-vulkan
download_library \
  "https://github.com/styler00dollar/VapourSynth-RIFE-ncnn-Vulkan/releases/download/r9_mod_v33/librife_linux_x86-64.so" \
  "librife_linux_x86-64.so" \
  "vapoursynth-plugins"

# rife model
download_model_files \
  "https://raw.githubusercontent.com/styler00dollar/VapourSynth-RIFE-ncnn-Vulkan/c3ec6aabc07c8fa37a4f58d7fed9e2ad1fc1b13f/models/rife-v4.26_ensembleFalse" \
  "rife-v4.26_ensembleFalse" \
  "flownet.bin" "flownet.param"

echo "done"
