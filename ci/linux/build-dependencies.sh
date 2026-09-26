#!/bin/bash
set -e

out_dir=out

echo "Building dependencies for Linux"

# clean outputs every run
mkdir -p $out_dir
find $out_dir -mindepth 1 -delete

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

  mkdir -p download/"$dir_name"
  cd download/"$dir_name"

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

# extracts a pinned .tar.gz/.tar.xz into download/<dir_name>, dropping its top-level folder
download_tarball() {
  local url="$1"
  local dir_name="$2"
  local sha256="$3"

  if [ -d "download/$dir_name" ]; then
    echo "$dir_name already exists. Skipping download."
    return
  fi

  echo "Downloading $dir_name"
  mkdir -p download
  local archive="download/$dir_name.archive"
  wget -q "$url" -O "$archive"

  if ! echo "$sha256  $archive" | sha256sum -c --quiet -; then
    echo "$url doesn't match its hash"
    rm "$archive"
    return 1
  fi

  mkdir -p "download/$dir_name"
  tar -xf "$archive" -C "download/$dir_name" --strip-components 1
  rm "$archive"
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

## python for vapoursynth
download_tarball \
  "https://github.com/astral-sh/python-build-standalone/releases/download/20250317/cpython-3.12.9+20250317-x86_64-unknown-linux-gnu-install_only.tar.gz" \
  "python" \
  "ef382fb88cbb41a3b0801690bd716b8a1aec07a6c6471010bcc6bd14cd575226"

cp -R download/python "$out_dir/python"

# the blur scripts need numpy. plugins on pypi install into vapoursynth's own plugins folder, which it autoloads
$out_dir/python/bin/python3 -m pip install --no-cache-dir --no-warn-script-location \
  numpy==2.5.3 \
  vapoursynth==79 \
  vapoursynth-akarin==1.5.0 \
  vapoursynth-bestsource==21.0 \
  vapoursynth-lsmas==1310.0.0.0 \
  vapoursynth-mvtools==29

# only needed to build and install packages, which never happens once it's bundled
rm -rf "$out_dir/python/lib/python3.12/site-packages/pip" "$out_dir"/python/bin/pip* \
  "$out_dir/python/lib/python3.12/ensurepip" "$out_dir/python/lib/python3.12/idlelib" \
  "$out_dir/python/lib/python3.12/tkinter" "$out_dir"/python/lib/libtcl* "$out_dir"/python/lib/libtk* \
  "$out_dir"/python/lib/tcl* "$out_dir"/python/lib/tk* "$out_dir"/python/lib/Tix* "$out_dir"/python/lib/itcl* \
  "$out_dir"/python/lib/thread* "$out_dir/python/lib/python3.12/test"
strip --strip-unneeded "$out_dir/python/lib/libpython3.12.so.1.0"
find "$out_dir/python" -name "__pycache__" -type d -prune -exec rm -rf {} +
# the bundle's read-only once it's in an appimage, so the bytecode has to be there already
$out_dir/python/bin/python3 -m compileall -q -j 0 "$out_dir/python/lib/python3.12" || true

## ffmpeg
# btbn's month-end builds are kept, the daily ones aren't. the shared build so ffmpeg, ffprobe and libmpv share
# the libraries instead of each having a copy
download_tarball \
  "https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-08-31-13-27/ffmpeg-n8.1.2-50-g1a748fe2cd-linux64-gpl-shared-8.1.tar.xz" \
  "ffmpeg" \
  "35dc428bf78d3f8a4447a68707338ecf9560ebcc7459967974f9ff5b1f84be24"

mkdir -p "$out_dir/ffmpeg/lib"
cp download/ffmpeg/bin/ffmpeg download/ffmpeg/bin/ffprobe "$out_dir/ffmpeg"
cp -a download/ffmpeg/lib/*.so* "$out_dir/ffmpeg/lib"
# shellcheck disable=SC2016 # $ORIGIN is for the loader, not the shell
patchelf --set-rpath '$ORIGIN/lib' "$out_dir/ffmpeg/ffmpeg" "$out_dir/ffmpeg/ffprobe"

## libmpv
# built against the ffmpeg above with only what the preview player needs, so the only libraries it adds are ones
# every desktop has. installed outside out since cmake links against it, it's bundled when packaging
mpv_prefix="$PWD/download/mpv-prefix"

if [ ! -f "$mpv_prefix/lib/libmpv.so" ]; then
  ffmpeg_pkgconfig="$PWD/download/ffmpeg-pkgconfig"
  mkdir -p "$ffmpeg_pkgconfig"
  for pc in download/ffmpeg/lib/pkgconfig/*.pc; do
    sed "s|^prefix=.*|prefix=$PWD/download/ffmpeg|" "$pc" >"$ffmpeg_pkgconfig/$(basename "$pc")"
  done
  export PKG_CONFIG_PATH="$mpv_prefix/lib/pkgconfig:$ffmpeg_pkgconfig"

  rm -rf download/libplacebo download/mpv
  git clone -q --depth 1 --branch v7.351.0 --recurse-submodules --shallow-submodules \
    https://code.videolan.org/videolan/libplacebo.git download/libplacebo
  meson setup download/libplacebo/build download/libplacebo \
    --prefix "$mpv_prefix" --libdir lib --buildtype release --default-library static \
    -Dvulkan=disabled -Dopengl=enabled -Dglslang=disabled -Dshaderc=disabled -Dlcms=disabled \
    -Dd3d11=disabled -Ddovi=disabled -Dlibdovi=disabled -Dxxhash=disabled -Dunwind=disabled \
    -Ddemos=false -Dtests=false
  meson install -C download/libplacebo/build
  # libplacebo has some c++, but mpv links as c so libstdc++ wouldn't be linked in, leaving libmpv relying on the
  # executable to provide it. link it in statically like the rest of the c++ runtime, without exporting it
  sed -i '/^Libs:/ s|$| -l:libstdc++.a -Wl,--exclude-libs,libstdc++.a|' "$mpv_prefix/lib/pkgconfig/libplacebo.pc"

  git clone -q --depth 1 --branch v0.41.0 https://github.com/mpv-player/mpv.git download/mpv
  meson setup download/mpv/build download/mpv \
    --prefix "$mpv_prefix" --libdir lib --buildtype release --auto-features disabled \
    -Dlibmpv=true -Dcplayer=false -Dtests=false \
    -Dgl=enabled -Degl=enabled -Dplain-gl=enabled -Dpulse=enabled -Dalsa=enabled -Diconv=enabled -Dzlib=enabled
  meson install -C download/mpv/build
fi

# blur links it dynamically so its private dependencies don't matter, but pkg-config fails if their .pc files can't be
# found, and ffmpeg's aren't kept
sed -i '/^Requires.private:/d; /^Libs.private:/d' "$mpv_prefix/lib/pkgconfig/mpv.pc"
# so the linker finds the ffmpeg libraries it needs when blur links against it
# shellcheck disable=SC2016 # $ORIGIN is for the loader, not the shell
patchelf --set-rpath '$ORIGIN/../../../out/ffmpeg/lib' "$mpv_prefix/lib/libmpv.so.2"

# frameblender
download_library \
  "https://github.com/f0e/vs-frameblender/releases/download/v2.1/frameblender-linux-x64.so" \
  "libframeblender.so" \
  "vapoursynth-plugins" \
  "624269bbca909d3be6e9437c5b5a2594b59d3135e39609fcc46606f8d4c979ee"

# fmtconv
# built here rather than downloaded so it runs on the same distros as the rest of the app
fmtconv_dir=download/fmtconv
if [ ! -f "$fmtconv_dir/build/unix/.libs/libfmtconv.so" ]; then
  rm -rf "$fmtconv_dir"
  git clone -q https://gitlab.com/EleonoreMizo/fmtconv.git "$fmtconv_dir"
  git -C "$fmtconv_dir" checkout -q f841f5ff94fd701484041068bb7124f5f393f9e0
  (
    cd "$fmtconv_dir/build/unix"
    ./autogen.sh
    ./configure LDFLAGS="-static-libstdc++ -static-libgcc -Wl,--exclude-libs,libstdc++.a:libgcc.a"
    make -j"$(nproc)"
  )
fi
cp "$fmtconv_dir/build/unix/.libs/libfmtconv.so" "$out_dir/vapoursynth-plugins"

# rife-ncnn-vulkan
download_library \
  "https://github.com/styler00dollar/VapourSynth-RIFE-ncnn-Vulkan/releases/download/r9_mod_v33/librife_linux_x86-64.so" \
  "librife_linux_x86-64.so" \
  "vapoursynth-plugins" \
  "18a00a5e3ac90a5dfcfdf85fdb81d977381a5a2560a4c45dbe9a469d007b1886"

# some older plugins ask for an executable stack without needing one, which newer glibc refuses to load
patchelf --clear-execstack "$out_dir"/vapoursynth-plugins/*.so

# rife model
download_model_files \
  "https://raw.githubusercontent.com/styler00dollar/VapourSynth-RIFE-ncnn-Vulkan/c3ec6aabc07c8fa37a4f58d7fed9e2ad1fc1b13f/models/rife-v4.26_ensembleFalse" \
  "rife-v4.26_ensembleFalse" \
  "flownet.bin" "flownet.param"

echo "done"
