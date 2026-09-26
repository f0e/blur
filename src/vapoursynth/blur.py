import json
import sys
from pathlib import Path

import vapoursynth as vs
from vapoursynth import core

# add blur.py folder to path so it can reference scripts
sys.path.insert(1, str(Path(__file__).parent))

import blur.blending
import blur.deduplicate
import blur.frame_timing
import blur.interpolate
import blur.mask
import blur.retime
import blur.utils as u
import blur.weighting
from blur import log

EXPECTED_PLUGINS = [
    "com.f0e.frameblender",
    "com.holywu.rife",
    "com.nodame.mvtools",
    "com.svp-team.flow1",
    "com.svp-team.flow2",
    "com.vapoursynth.bestsource",
    "com.vapoursynth.resize",
    "com.vapoursynth.std",
    "com.vapoursynth.text",
    "fmtconv",
    "info.akarin.vsplugin",
]

LSMASH_PLUGIN = "systems.innocent.lsmas"
AVISOURCE_PLUGIN = "com.vapoursynth.avisource"

BESTSOURCE_HW_DEVICES = {
    "darwin": ["videotoolbox"],
    "win32": ["cuda", "d3d11va"],
    "linux": ["cuda", "vaapi"],
}


def build_mask_clips(
    mask_name: str,
    auto_mask: bool,
    auto_mask_params: blur.mask.Params,
    mask_source: vs.VideoNode,
    video_path: Path,
    settings_path: Path,
    mask_start: int,
    mask_end: int,
) -> list[vs.VideoNode]:
    mask_clips = []

    if mask_name:
        log.info(f"applying mask {mask_name}")
        mask_clips.append(blur.mask.load(settings_path / "masks" / mask_name))

    if auto_mask:
        generated = blur.mask.cached(
            mask_source,
            video_path,
            settings_path / blur.mask.CACHE_FOLDER,
            settings_path / blur.mask.SCORE_CACHE_FOLDER,
            (mask_start, mask_end),
            auto_mask_params,
        )

        if generated is not None:
            mask_clips.append(generated)

    return mask_clips


def main():
    if globals().get("macos_bundled") == "true":
        u.load_plugins(".dylib")
    elif globals().get("linux_bundled") == "true":
        u.load_plugins(".so")

    loaded_plugins = [plugin.identifier for plugin in core.plugins()]

    missing_plugins = [plugin for plugin in EXPECTED_PLUGINS if plugin not in loaded_plugins]
    if missing_plugins:
        raise u.BlurException(
            f"Missing required VapourSynth extension{'s' if len(missing_plugins) != 1 else ''}: {', '.join(missing_plugins)}"
        )

    video_path = Path(globals().get("video_path", ""))
    settings = json.loads(globals().get("settings", "{}"))
    fps_num = globals().get("fps_num", -1)
    fps_den = globals().get("fps_den", -1)
    color_range = globals().get("color_range", "")
    settings_path = Path(globals().get("settings_path", ""))

    source_plugin = settings["source_plugin"]
    if source_plugin == "LWLibavSource" and LSMASH_PLUGIN not in loaded_plugins:
        log.info("LSMASH isn't available, falling back to BestSource")
        source_plugin = "BestSource"

    if globals().get("frameserver") == "true":
        # debugmode frameserver signpost avis only decode through its vfw codec, which ffmpeg (meaning bestsource/lsmas) can't use
        if AVISOURCE_PLUGIN not in loaded_plugins:
            raise u.BlurException("Rendering frameserver output needs the AVISource VapourSynth plugin")

        video = core.avisource.AVISource(str(video_path))
    elif source_plugin == "LWLibavSource":

        def lwlibav_source(prefer_hw: int):
            return core.lsmas.LWLibavSource(
                source=video_path,
                cache=0,
                prefer_hw=prefer_hw,
                fpsnum=fps_num if fps_num != -1 else None,
                fpsden=fps_den if fps_den != -1 else None,
            )

        if settings["gpu_decoding"]:
            # lsmas errors instead of falling back when the gpu can't decode the codec (e.g. av1 on older macs)
            try:
                video = lwlibav_source(3)
            except vs.Error as e:
                log.info(f"GPU decoding failed ({e}), falling back to CPU decoding")
                video = lwlibav_source(0)
        else:
            video = lwlibav_source(0)

        # LWLibavSource ignores mp4 edit lists and outputs a duplicate of the first frame for each negative pts preroll packet,
        # making the clip too long and desyncing it from audio. trim those frames manually
        preroll_frames = int(globals().get("preroll_frames", 0))
        if preroll_frames > 0:
            video = video[preroll_frames:]
    else:

        def bestsource(hwdevice: str | None):
            return core.bs.VideoSource(
                source=video_path,
                cachemode=0,
                apply_rotation=False,  # lsmas doesn't
                fpsnum=fps_num if fps_num != -1 else None,
                fpsden=fps_den if fps_den != -1 else None,
                hwdevice=hwdevice,
            )

        video = None

        if settings["gpu_decoding"]:
            # bestsource also errors instead of falling back when the gpu can't decode the codec (e.g. av1 on older macs)
            for hwdevice in BESTSOURCE_HW_DEVICES.get(sys.platform, []):
                try:
                    video = bestsource(hwdevice)
                    break
                except vs.Error as e:
                    log.info(f"GPU decoding with {hwdevice} failed ({e})")

            if video is None:
                log.info("falling back to CPU decoding")

        if video is None:
            video = bestsource(hwdevice=None)

    # rgb sources (frameservers, lossless captures) can't go through svp or out as y4m
    if video.format.color_family == vs.RGB:
        video = core.resize.Bicubic(
            video,
            format=vs.YUV420P8,
            matrix=u.guess_matrix(video.width, video.height),
            range=vs.RANGE_FULL if color_range == "pc" else vs.RANGE_LIMITED,
        )

    # trimming
    start = int(globals().get("start", 0))
    end = int(globals().get("end", video.num_frames))

    mask_start = max(0, int(globals().get("mask_start", 0)))
    mask_end = min(int(globals().get("mask_end", video.num_frames)), video.num_frames)
    mask_source = video[mask_start:mask_end]

    untrimmed = video
    video = video[start:end]

    # a frame timing log replaces deduplication
    try:
        deduplicate_threshold = float(settings["deduplicate_threshold"])
    except (ValueError, TypeError, KeyError):
        raise u.BlurException(f"Deduplicate threshold is not a number: '{settings['deduplicate_threshold']}'")

    # how far a picture is interpolated across before it's held. 0 means dedupe is off, not a range
    deduplicate_range: int | None = int(settings["deduplicate_range"])
    if deduplicate_range == -1:  # -1 = infinite
        deduplicate_range = None

    hold = blur.retime.MAX_GAP_LIMIT
    if deduplicate_range:
        hold = max(1, min(deduplicate_range, hold))

    logged_timing = None
    if settings["frame_timing_logs"]:
        logged_timing = blur.frame_timing.analyse(
            video.fps,
            untrimmed.num_frames,
            start,
            video.num_frames,
            video_path,
            hold=hold,
        )

    retiming = (settings["deduplicate"] and settings["deduplicate_range"] != 0) or logged_timing is not None
    apply_masks = settings["interpolate"] or retiming
    mask_name = settings["mask"] if apply_masks else ""
    auto_mask = settings["auto_mask"] if apply_masks else False
    auto_mask_params = blur.mask.Params.from_settings(settings)

    if globals().get("preview_mask") == "true":
        mask_clips = build_mask_clips(
            mask_name,
            auto_mask,
            auto_mask_params,
            mask_source,
            video_path,
            settings_path,
            mask_start,
            mask_end,
        )

        preview = blur.mask.preview(video, mask_clips)
        preview.set_output()
        return

    # validate some settings
    svp_interpolation_algorithm = u.coalesce(
        u.safe_int(settings["svp_interpolation_algorithm"]),
        blur.interpolate.DEFAULT_ALGORITHM,
    )

    interpolation_blocksize = u.coalesce(
        u.safe_int(settings["interpolation_blocksize"]),
        blur.interpolate.DEFAULT_BLOCKSIZE,
    )

    interpolation_mask_area = u.coalesce(
        u.safe_int(settings["interpolation_mask_area"]),
        blur.interpolate.DEFAULT_MASKING,
    )

    if settings["blur_output_fps"] <= 0:
        raise u.BlurException("Output FPS must be above 0")

    video_info = u.VideoInfo(
        is_full_color_range=color_range == "pc",
        orig_width=video.width,
        orig_height=video.height,
    )

    # what masked regions get put back to. before deduplication since that interpolates too
    original = video

    # input timescale
    if settings["timescale"]:
        input_timescale = float(settings["input_timescale"])
        if settings["input_timescale"] != 1:
            video = u.assume_scaled_fps(video, 1 / input_timescale)

    # the timeline is rendered during interpolation, see blur/retime.py
    timeline = logged_timing
    debug_timeline = None
    if retiming and logged_timing is None:
        timeline = blur.deduplicate.analyse(
            video,
            threshold=deduplicate_threshold,
            max_gap=deduplicate_range,
            timing=settings["duplicate_timing"],
            future_checks=settings["max_future_checks"],
        )

    if timeline is not None and settings["debug"]:
        # `timeline` is cleared once interpolation takes it, so keep a copy for the debug overlay
        debug_timeline = timeline
        debug_source_fps = video.fps

    def interpolate_to(method: str, video: vs.VideoNode, new_fps, timeline=None):
        """Interpolate `video` up to `new_fps`, filling `timeline`'s gaps on the way if it was given any."""
        match method:
            case "svp":
                if settings["manual_svp"]:
                    # add the framerate unless the manual string already has one
                    smooth_json = json.loads(settings["smooth_string"])
                    if "rate" not in smooth_json:
                        smooth_json["rate"] = {"num": int(new_fps), "abs": True}

                    return blur.interpolate.svp(
                        video,
                        video_info=video_info,
                        super_string=settings["super_string"],
                        vectors_string=settings["vectors_string"],
                        smooth_str=json.dumps(smooth_json),
                        timeline=timeline,
                        new_fps=new_fps,
                    )

                return blur.interpolate.interpolate_svp(
                    video,
                    video_info=video_info,
                    new_fps=new_fps,
                    preset=settings["svp_interpolation_preset"],
                    algorithm=svp_interpolation_algorithm,
                    blocksize=interpolation_blocksize,
                    overlap=0,
                    masking=interpolation_mask_area,
                    gpu=settings["gpu_interpolation"],
                    timeline=timeline,
                )

            case "rife":
                return blur.interpolate.interpolate_rife(
                    video,
                    video_info=video_info,
                    new_fps=new_fps,
                    model_path=settings["rife_model"],
                    device_index=settings["rife_device_index"],
                    timeline=timeline,
                )

            case "rife (tensorrt)":
                return blur.interpolate.interpolate_rife_vsmlrt(
                    video,
                    video_info=video_info,
                    new_fps=new_fps,
                    model_path=settings["rife_trt_model"],
                    device_index=settings["tensorrt_device_index"],
                    settings_path=settings_path,
                    timeline=timeline,
                )

            case "mvtools":
                return blur.interpolate.interpolate_mvtools(
                    video,
                    new_fps,
                    blocksize=interpolation_blocksize,
                    masking=interpolation_mask_area,
                    timeline=timeline,
                )

            case _:
                raise u.BlurException(
                    f"Invalid interpolation method: '{method}'. Should be one of: 'svp', 'rife', "
                    "'rife (tensorrt)', 'mvtools'"
                )

    # interpolation
    interpolated = False
    if settings["interpolate"]:
        frames_before_interpolation = video.num_frames

        def parse_fps_setting(setting_key):
            fps_value = settings[setting_key].strip()

            if fps_value.endswith("x"):
                # ends with x, is a multiplier (e.g. 5x)
                multiplier_str = fps_value[:-1].strip()
                if not multiplier_str:
                    raise u.BlurException(
                        f"Invalid FPS multiplier {setting_key}: '{fps_value}'. Should be something like 5x."
                    )

                try:
                    multiplier = float(multiplier_str)
                except ValueError:
                    raise u.BlurException(
                        f"Invalid FPS multiplier {setting_key}: '{fps_value}'. Should be something like 5x. Do you have non-number characters before the final x?"
                    )

                return video.fps * multiplier

            else:
                # doesn't end with x, is an fps (e.g. 600)
                try:
                    return int(fps_value)
                except ValueError:
                    raise u.BlurException(
                        f"Invalid FPS {setting_key}: '{fps_value}'. It should be either a whole number or a multiplier (e.g. 5x)"
                    )

        interpolated_fps = parse_fps_setting("interpolated_fps")

        if settings["interpolation_method"] != settings["pre_interpolation_method"] and settings["pre_interpolate"]:
            pre_interpolated_fps = parse_fps_setting("pre_interpolated_fps")

            if (
                video.fps < pre_interpolated_fps
            ):  # if can be while if rife limits the max interpolation fps, but i don't think it does
                old_fps = video.fps

                log.info(f"pre-interpolating to {pre_interpolated_fps}")

                if settings["pre_interpolation_method"] not in [
                    "rife",
                    "rife (tensorrt)",
                ]:
                    raise u.BlurException(
                        f"Invalid pre-interpolation method: '{settings['pre_interpolation_method']}'. Should be one of: 'rife', 'rife (tensorrt)'"
                    )

                # the first interpolation pass fills deduplication's gaps
                video = interpolate_to(
                    settings["pre_interpolation_method"],
                    video,
                    pre_interpolated_fps,
                    timeline=timeline,
                )
                timeline = None

                fps_added = video.fps - old_fps
                log.info(
                    f"added {fps_added} (interp: {pre_interpolated_fps}. video.fps: {video.fps}/{pre_interpolated_fps})"
                )

        if video.fps < interpolated_fps:
            log.info(f"interpolating to {interpolated_fps} with {settings['interpolation_method']}")
            old_fps = video.fps

            video = interpolate_to(
                settings["interpolation_method"],
                video,
                interpolated_fps,
                timeline=timeline,
            )
            timeline = None

            fps_added = video.fps - old_fps
            log.info(f"added {fps_added} (interp: {interpolated_fps}. video.fps: {video.fps}/{interpolated_fps})")

        interpolated = video.num_frames != frames_before_interpolation

    if timeline is not None:
        # no interpolation, so deduplication fills its own gaps using 'deduplicate method'
        method = settings["deduplicate_method"]
        log.info(f"filling duplicate frames with {method}")

        if method == "old" and logged_timing is not None:
            log.info("the 'old' method can't use a frame timing log, so rife fills the gaps instead")
            method = "rife"

        if method == "old":
            # the only method that doesn't use the timeline
            video = blur.deduplicate.fill_drops_old(
                video,
                threshold=deduplicate_threshold,
                debug=settings["debug"],
            )
        else:
            video = interpolate_to(method, video, video.fps, timeline=timeline)

    # masking, only needed if something was interpolated
    mask_clips = []
    if (mask_name or auto_mask) and (retiming or interpolated):
        mask_clips = build_mask_clips(
            mask_name,
            auto_mask,
            auto_mask_params,
            mask_source,
            video_path,
            settings_path,
            mask_start,
            mask_end,
        )

    if mask_clips:
        video = blur.mask.protect(video, original, blur.mask.combine(mask_clips))

    # debug: label the frames retiming touched. turn blur off to read it
    if debug_timeline is not None:
        video = blur.retime.annotate(video, debug_timeline, video.fps / debug_source_fps)

    # output timescale
    if settings["timescale"]:
        output_timescale = float(settings["output_timescale"])
        if output_timescale != 1:
            video = u.assume_scaled_fps(video, output_timescale)

    # blurring
    if settings["blur"]:
        if settings["blur_amount"] > 0:
            frame_gap = int(video.fps / settings["blur_output_fps"])
            blur_frames = int(frame_gap * settings["blur_amount"])

            if blur_frames > 0:
                # number of weights must be odd
                if blur_frames % 2 == 0:
                    blur_frames += 1

                weights = blur.weighting.parse(
                    blur_frames,
                    weighting_type=settings["blur_weighting"],
                    gaussian_std_dev=settings["blur_weighting_gaussian_std_dev"],
                    gaussian_mean=settings["blur_weighting_gaussian_mean"],
                    gaussian_bound=json.loads(settings["blur_weighting_gaussian_bound"]),
                )

                video = blur.blending.blend(
                    video,
                    video_info,
                    weights,
                    settings["preserve_brightness"],
                    float(settings["bloom_threshold"]),
                    float(settings["bloom_strength"]) if settings["bloom"] else None,
                )

        # set exact fps
        video = blur.interpolate.change_fps(video, settings["blur_output_fps"])

        skip_frames = int(globals().get("skip_frames", 0))
        if skip_frames > 0:
            video = video[min(skip_frames, video.num_frames - 1) :]

    # filters
    if settings["filters"]:
        brightness = float(settings["brightness"])
        contrast = float(settings["contrast"])
        saturation = float(settings["saturation"])

        if brightness != 1 or contrast != 1 or saturation != 1:
            video = u.grade(video, video_info, brightness, contrast, saturation)

    # upscaling (to 4K)
    if settings["upscale"] and video.height < 2160:
        HEIGHT_4K = 2160

        scale_factor = HEIGHT_4K / video.height
        video = core.resize.Point(
            video,
            width=round(video.width * scale_factor),
            height=HEIGHT_4K,
        )

    video.set_output()


try:
    main()
except u.BlurException as e:
    u.handle_blur_exception(e)
except Exception as e:  # noqa: BLE001 - last resort, reports anything unexpected to blur
    u.handle_unexpected_exception(e)
