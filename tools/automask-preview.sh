#!/usr/bin/env bash
# Preview what `mask: auto` generates for a video. Temporary dev tool - see tools/automask_preview.py.
#
#   ./tools/automask-preview.sh clip.mp4
#   ./tools/automask-preview.sh --stages *.mp4
#   ./tools/automask-preview.sh --frame 500 --out /tmp/masks clip.mp4
#
# Runs against the built app, since its bundled python is the only one with vapoursynth in it. Build first
# if you haven't: cmake --build out/build/mac-debug

set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

resources=""
for build in Debug RelWithDebInfo Release MinSizeRel; do
	candidate="$repo/bin/$build/Blur.app/Contents/Resources"
	if [ -d "$candidate/python" ] && [ -x "$candidate/ffmpeg/ffmpeg" ]; then
		resources="$candidate"
		break
	fi
done

if [ -z "$resources" ]; then
	echo "couldn't find a built Blur.app under $repo/bin - build it first:" >&2
	echo "  cmake --build out/build/mac-debug" >&2
	exit 1
fi

python="$(ls "$resources"/python/bin/python3.[0-9]* 2>/dev/null | head -1)"
if [ -z "$python" ]; then
	echo "no bundled python in $resources/python/bin" >&2
	exit 1
fi

# the bundled python loads vapoursynth.so, which links against the dylibs sat next to the app
export DYLD_LIBRARY_PATH="$resources/libs${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"

exec "$python" "$repo/tools/automask_preview.py" --resources "$resources" "$@"
