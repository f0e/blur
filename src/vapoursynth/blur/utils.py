import vapoursynth as vs
from vapoursynth import core

import sys
import json
import math
import traceback
from pathlib import Path
from fractions import Fraction
from dataclasses import dataclass

DEBUG_ENABLED = False


def log(*args):
    if DEBUG_ENABLED:  # printing to stdout garbles video output
        print(*args)


class BlurException(Exception):
    def __init__(
        self,
        user_error: str,
        original_exception: Exception | None = None,
    ):
        self.user_error = user_error
        self.original_exception = original_exception

        tech_parts = []
        if original_exception:
            tech_parts.append(
                f"Original error: {type(original_exception).__name__}: {str(original_exception)}"
            )
            tech_parts.append(
                f"Traceback:\n{''.join(traceback.format_tb(original_exception.__traceback__))}"
            )

        self.full_technical_details = "\n".join(tech_parts)

        super().__init__(user_error)

    def to_json(self) -> str:
        return json.dumps(
            {
                "error_type": "BlurException",
                "user_message": self.user_error,
                "technical_details": self.full_technical_details,
            },
            indent=2,
        )


def handle_blur_exception(e: BlurException):
    print(e.to_json(), file=sys.stderr)
    sys.exit(1)


def handle_unexpected_exception(e: Exception):
    blur_ex = BlurException(
        user_error="An unexpected error occurred during VapourSynth processing.",
        original_exception=e,
    )
    handle_blur_exception(blur_ex)


@dataclass
class VideoInfo:
    is_full_color_range: bool
    orig_width: int
    orig_height: int
    resize_chromaloc: str | None


def load_plugins(extension: str):
    plugin_dir = Path("../vapoursynth-plugins")
    ignored = {
        f"libbestsource{extension}",
    }

    for plugin in plugin_dir.glob(f"*{extension}"):
        if plugin.name not in ignored:
            log("Loading", plugin.name)
            try:
                core.std.LoadPlugin(path=str(plugin))
            except Exception as e:
                raise BlurException(f"Failed to load plugin {plugin.name}: {e}")


def safe_int(value):
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def coalesce(value, fallback):
    return fallback if value is None else value


def check_model_path(rife_model_path: str):
    path = Path(rife_model_path)

    if not path.exists():
        raise BlurException(f"RIFE model not found at {path}")


def assume_scaled_fps(clip, timescale):
    new_fps = clip.fps * timescale
    fps_frac = Fraction(new_fps).limit_denominator()

    return core.std.AssumeFPS(
        clip, fpsnum=fps_frac.numerator, fpsden=fps_frac.denominator
    )


def scale_luminance(video: vs.VideoNode, upscale: bool, y_scale_w: int, y_scale_h: int):
    if y_scale_w == 1 and y_scale_h == 1:
        return video

    y = core.std.ShufflePlanes(video, planes=0, colorfamily=vs.GRAY)
    u = core.std.ShufflePlanes(video, planes=1, colorfamily=vs.GRAY)
    v = core.std.ShufflePlanes(video, planes=2, colorfamily=vs.GRAY)

    if upscale:
        y = core.resize.Point(y, width=y.width * y_scale_w, height=y.height * y_scale_h)
    else:  # downscale
        y = core.resize.Point(y, width=y.width / y_scale_w, height=y.height / y_scale_h)

    video = core.std.ShufflePlanes(
        clips=[y, u, v], planes=[0, 0, 0], colorfamily=vs.YUV
    )

    return video


def with_scaled_luminance(
    video: vs.VideoNode,
    target_format,
    process_func,
):
    in_format_info = core.get_video_format(video.format)
    target_format_info = core.get_video_format(target_format)

    scale_w = 1
    scale_h = 1

    if video.format.color_family == vs.ColorFamily.YUV:
        subsampling_diff_w = (
            target_format_info.subsampling_w - in_format_info.subsampling_w
        )
        subsampling_diff_h = (
            target_format_info.subsampling_h - in_format_info.subsampling_h
        )

        scale_w = (
            # for each increase in subsampling level, chroma resolution halves
            # subsampling=0 -> full res, subsampling=1 -> half res, subsampling=2 -> quarter res
            2**subsampling_diff_w
        )
        scale_h = 2**subsampling_diff_h

        video = scale_luminance(video, True, scale_w, scale_h)

    video = process_func(video)

    if scale_w != 1 or scale_h != 1:
        video = scale_luminance(video, False, scale_w, scale_h)

    return video


def guess_matrix(width: int, height: int) -> int:
    if height >= 720 or width >= 1280:
        return vs.MATRIX_BT709
    else:
        return vs.MATRIX_ST170_M  # BT601, standard for SD


def guess_transfer(width: int, height: int) -> int:
    if height >= 720 or width >= 1280:
        return vs.TRANSFER_BT709
    else:
        return vs.TRANSFER_BT601


def guess_primaries(width: int, height: int) -> int:
    if height >= 720 or width >= 1280:
        return vs.PRIMARIES_BT709
    else:
        return vs.PRIMARIES_ST170_M  # BT601


def with_format(
    video: vs.VideoNode,
    video_info: VideoInfo,
    target_format,
    process_func,
):
    try:
        orig_format = video.format
        needs_conversion = orig_format.id != target_format

        if needs_conversion:
            convert_kwargs = {
                "format": target_format,
                "range_in": video_info.is_full_color_range,
                "range": video_info.is_full_color_range,
            }

            if (
                target_format in [vs.RGBS, vs.RGBH]
                and orig_format.color_family == vs.YUV
            ):
                # @HACK - some videos (adobe -_-) dont set the color transfer & primaries for example which means resizing fails cause it doesnt have the required information
                # here im just making educated guesses as to what they are but this is so dumb
                props = dict(video.get_frame(0).props)

                log("guessing video props. original props:", props)

                set_props = {}

                if props.get("_Matrix", 0) in {0, vs.MATRIX_UNSPECIFIED}:
                    set_props["_Matrix"] = guess_matrix(video.width, video.height)

                if props.get("_Transfer", 0) in {0, vs.TRANSFER_UNSPECIFIED}:
                    set_props["_Transfer"] = guess_transfer(video.width, video.height)

                if props.get("_Primaries", 0) in {0, vs.PRIMARIES_UNSPECIFIED}:
                    set_props["_Primaries"] = guess_primaries(video.width, video.height)

                if set_props:
                    video = core.std.SetFrameProps(video, **set_props)

            if video_info.resize_chromaloc is not None:
                convert_kwargs["chromaloc_s"] = video_info.resize_chromaloc

            log("conversion kwargs", convert_kwargs)

            video = core.resize.Point(video, **convert_kwargs)
    except BlurException:
        raise
    except Exception as e:
        raise BlurException(
            "Failed to convert video format. You may need to convert your input video's colorspace manually.",
            e,
        )

    video = process_func(video)

    try:
        if needs_conversion:
            convert_back_kwargs = {
                "format": orig_format.id,
                "range_in": video_info.is_full_color_range,
                "range": video_info.is_full_color_range,
            }

            if (
                target_format in [vs.RGBS, vs.RGBH]
                and orig_format.color_family == vs.YUV
            ):
                convert_back_kwargs["matrix_s"] = "709"

            log("conversion back kwargs", convert_back_kwargs)

            video = core.resize.Point(video, **convert_back_kwargs)

        return video
    except BlurException:
        raise
    except Exception as e:
        raise BlurException(
            "Failed to convert video format back after processing. Please copy the extended log and report this to GitHub issues or the Discord.",
            e,
        )


def with_padding(
    video: vs.VideoNode,
    multiple: int | None,
    process_func,
):
    if multiple is not None:
        w, h = video.width, video.height
        pad_w = math.ceil(w / multiple) * multiple
        pad_h = math.ceil(h / multiple) * multiple
        needs_padding = pad_w != w or pad_h != h

        if needs_padding:
            video = video.std.AddBorders(right=pad_w - w, bottom=pad_h - h)

    video = process_func(video)

    if multiple is not None:
        if needs_padding:
            video = video.std.Crop(right=pad_w - w, bottom=pad_h - h)

    return video
