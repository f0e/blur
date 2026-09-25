#!/bin/bash
# packages a linux build into an appimage, and optionally a tarball
# run from the repo root after build-dependencies.sh and the cmake build
# usage: ci/linux/appimage/package.sh <build dir> <output appimage> [output tarball]
set -euo pipefail

build_dir="$(realpath "$1")"
output="$(realpath -m "$2")"
tarball="${3:+$(realpath -m "$3")}"

script_dir="$(dirname "$(realpath "$0")")"
ci_dir="$(dirname "$(dirname "$script_dir")")"
repo_dir="$(dirname "$ci_dir")"
download_dir="$ci_dir/download"
appdir="$ci_dir/appdir"

download() {
  local url="$1"
  local path="$2"
  local sha256="$3"

  if [ ! -f "$path" ]; then
    wget -q "$url" -O "$path.part"
    mv "$path.part" "$path"
  fi

  if ! echo "$sha256  $path" | sha256sum -c --quiet -; then
    echo "$url doesn't match its hash" >&2
    rm "$path"
    return 1
  fi
}

mkdir -p "$download_dir"

# libraries every desktop has, which break when a copy from another distro is loaded instead. from
# https://github.com/AppImageCommunity/pkg2appimage/blob/19e30b276ffedf4d3b4b56bc6320f463625a74f8/excludelist
excludelist="$script_dir/excludelist"

appimagetool="$download_dir/appimagetool-x86_64.AppImage"
download \
  "https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage" \
  "$appimagetool" \
  "ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"
chmod +x "$appimagetool"

runtime="$download_dir/appimage-runtime-x86_64"
download \
  "https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64" \
  "$runtime" \
  "2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d"

mkdir -p "$appdir"
find "$appdir" -mindepth 1 -delete

# the app looks for its resources next to its binaries
cp "$build_dir/blur" "$build_dir/blur-cli" "$appdir/"
cp -r "$build_dir/lib" "$appdir/"
cp -a "$ci_dir/out/." "$appdir/"

cp -L "$download_dir/mpv-prefix/lib/libmpv.so.2" "$appdir/"
# shellcheck disable=SC2016 # $ORIGIN is for the loader, not the shell
patchelf --set-rpath '$ORIGIN:$ORIGIN/ffmpeg/lib' "$appdir/libmpv.so.2"

# copy in whatever the binaries and plugins need that isn't on every system, next to whatever needs it. excluded
# libraries aren't followed, their dependencies come from the system along with them
excluded="$(grep -v '^#' "$excludelist" | awk 'NF { print $1 }')"
plugins_dir="$appdir/vapoursynth-plugins"
app_elf_files=("$appdir/blur" "$appdir/blur-cli" "$appdir/libmpv.so.2")
plugin_elf_files=("$plugins_dir"/*.so)

# where the build system's loader finds each library
declare -A library_paths
while read -r name path; do
  library_paths["$name"]="$path"
done < <(LD_LIBRARY_PATH="$appdir:$appdir/ffmpeg/lib:$plugins_dir" ldd "${app_elf_files[@]}" "${plugin_elf_files[@]}" |
  awk '$2 == "=>" && $3 ~ /^\// { print $1, $3 }')

# shellcheck disable=SC2016 # $ORIGIN is for the loader, not the shell
patchelf --set-rpath '$ORIGIN' "${plugin_elf_files[@]}"

bundled=()
queue=("${app_elf_files[@]}" "${plugin_elf_files[@]}")
while [ ${#queue[@]} -gt 0 ]; do
  elf="${queue[0]}"
  queue=("${queue[@]:1}")
  dest_dir="$(dirname "$elf")"

  for name in $(readelf -d "$elf" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p'); do
    if grep -qxF "$name" <<<"$excluded" || [ -e "$dest_dir/$name" ] || [ -e "$appdir/ffmpeg/lib/$name" ]; then
      continue
    fi

    path="${library_paths[$name]:-}"
    if [ -z "$path" ]; then
      echo "Can't find $name, needed by $elf" >&2
      exit 1
    fi

    cp -L "$path" "$dest_dir/$name"
    # shellcheck disable=SC2016 # $ORIGIN is for the loader, not the shell
    patchelf --set-rpath '$ORIGIN' "$dest_dir/$name"
    bundled+=("$(realpath --relative-to="$appdir" "$dest_dir/$name")")
    queue+=("$dest_dir/$name")
  done
done

echo "Bundled libraries: ${bundled[*]:-none}"

missing="$(ldd "${app_elf_files[@]}" "${plugin_elf_files[@]}" | grep 'not found' || true)"
if [ -n "$missing" ]; then
  echo "Unresolved runtime dependencies:" >&2
  echo "$missing" >&2
  exit 1
fi

cp "$repo_dir/installer/linux/AppRun" "$appdir/AppRun"
cp "$repo_dir/installer/linux/blur.desktop" "$appdir/blur.desktop"
cp "$repo_dir/resources/blur.png" "$appdir/blur.png"
ln -s blur.png "$appdir/.DirIcon"

# the tarball is the appdir in a blur folder, without the appimage-only launcher and icon link
if [ -n "$tarball" ]; then
  mkdir -p "$(dirname "$tarball")"
  tar -czf "$tarball" -C "$appdir" --owner=0 --group=0 --exclude ./AppRun --exclude ./.DirIcon \
    --transform 's,^\.,blur,S' .
  echo "Created $tarball ($(du -h "$tarball" | cut -f1))"
fi

mkdir -p "$(dirname "$output")"
rm -f "$output"
# there's no fuse in containers
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 "$appimagetool" --no-appstream --runtime-file "$runtime" --comp zstd \
  "$appdir" "$output"

echo "Created $output ($(du -h "$output" | cut -f1))"
