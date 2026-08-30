import vapoursynth as vs
from vapoursynth import core

import sys
import json
from pathlib import Path

# add blur.py folder to path so it can reference scripts
sys.path.insert(1, str(Path(__file__).parent))

import blur.blending
import blur.deduplicate
import blur.interpolate
import blur.mask
import blur.weighting
import blur.utils as u
from blur import log

EXPECTED_PLUGINS = [
    "com.holywu.rife",
    "com.nodame.mvtools",
    "com.svp-team.flow1",
    "com.svp-team.flow2",
    "com.vapoursynth.bestsource",
    "com.vapoursynth.resize",
    "com.vapoursynth.std",
    "com.vapoursynth.text",
    "com.yuygfgg.adjust",
    "fmtconv",
    "info.akarin.vsplugin",
]

LSMASH_PLUGIN = "systems.innocent.lsmas"

try:
    if vars().get("macos_bundled") == "true":
        u.load_plugins(".dylib")
    elif vars().get("linux_bundled") == "true":
        u.load_plugins(".so")

    loaded_plugins = [plugin.identifier for plugin in core.plugins()]

    missing_plugins = [
        plugin for plugin in EXPECTED_PLUGINS if plugin not in loaded_plugins
    ]
    if missing_plugins:
        raise u.BlurException(
            f"Missing required VapourSynth extension{'s' if len(missing_plugins) != 1 else ''}: {', '.join(missing_plugins)}"
        )

    video_path = Path(vars().get("video_path", ""))

    settings = json.loads(vars().get("settings", "{}"))

    fps_num = vars().get("fps_num", -1)
    fps_den = vars().get("fps_den", -1)
    color_range = vars().get("color_range", "")

    settings_path = Path(vars().get("settings_path", ""))

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

    deduplicating = settings["deduplicate"] and settings["deduplicate_range"] != 0

    # masks protect against interpolation, and deduplication fills the gaps it finds by interpolating too, so
    # a mask is worth applying whenever either of them runs
    mask_name = settings["mask"] if settings["interpolate"] or deduplicating else ""

    resize_chromaloc = settings["resize_chromaloc"]
    if resize_chromaloc == "default":
        resize_chromaloc = None

    rife_device_index = settings["rife_device_index"]
    if rife_device_index == -1:  # haven't benchmarked yet..?
        rife_device_index = 0

    tensorrt_device_index = settings["tensorrt_device_index"]
    if tensorrt_device_index == -1:  # haven't benchmarked yet..?
        tensorrt_device_index = 0

    source_plugin = settings["source_plugin"]
    if source_plugin == "LWLibavSource" and LSMASH_PLUGIN not in loaded_plugins:
        log.info("LSMASH isn't available, falling back to BestSource")
        source_plugin = "BestSource"

    if source_plugin == "LWLibavSource":
        video = core.lsmas.LWLibavSource(
            source=video_path,
            cache=0,
            prefer_hw=3 if settings["gpu_decoding"] else 0,
            fpsnum=fps_num if fps_num != -1 else None,
            fpsden=fps_den if fps_den != -1 else None,
        )

        # LWLibavSource doesn't respect mp4 edit lists, so negative pts preroll frames get decoded as real content instead of being skipped.
        # fix this by trimming those frames manually
        preroll_frames = int(vars().get("preroll_frames", 0))
        if preroll_frames > 0:
            video = video[preroll_frames:]
    else:
        video = core.bs.VideoSource(
            source=video_path,
            cachemode=0,
            fpsnum=fps_num if fps_num != -1 else None,
            fpsden=fps_den if fps_den != -1 else None,
        )

    video_info = u.VideoInfo(
        is_full_color_range=color_range == "pc",
        orig_width=video.width,
        orig_height=video.height,
        resize_chromaloc=resize_chromaloc,
    )

    # trimming
    start = int(vars().get("start", 0))
    end = int(vars().get("end", video.num_frames))

    # what an automatic mask gets worked out from. blur passes this separately from the trim above, because a
    # render's start is the user's trim but a preview's is wherever the seek bar is - and a mask that followed
    # the seek bar would be a different mask every time, none of them cacheable. left unset it's the whole
    # video, which is what a preview and an untrimmed render both want
    mask_start = max(0, int(vars().get("mask_start", 0)))
    mask_end = min(int(vars().get("mask_end", video.num_frames)), video.num_frames)
    mask_source = video[mask_start:mask_end]

    video = video[start:end]

    # what masked regions get put back to. taken after trimming so it lines up with the render frame for
    # frame, and before deduplication because its fill frames are interpolated and warp an overlay exactly
    # the way interpolation proper does
    original = video

    # input timescale
    if settings["timescale"]:
        input_timescale = float(settings["input_timescale"])
        if settings["input_timescale"] != 1:
            video = u.assume_scaled_fps(video, 1 / input_timescale)

    # deduplication doesn't render anything here - it works out which frames are repeats, and interpolation
    # renders onto the timeline that describes. see blur/deduplicate.py
    dedupe = None
    debug_dedupe = None
    if deduplicating:
        deduplicate_range: int | None = int(settings["deduplicate_range"])
        if deduplicate_range == -1:  # -1 = infinite
            deduplicate_range = None

        try:
            deduplicate_threshold = float(settings["deduplicate_threshold"])
        except (ValueError, TypeError, KeyError):
            raise u.BlurException(
                f"Deduplicate threshold is not a number: '{settings['deduplicate_threshold']}'"
            )

        dedupe = blur.deduplicate.analyse(
            video,
            threshold=deduplicate_threshold,
            max_gap=deduplicate_range,
        )

        if settings["debug"]:
            # `dedupe` is cleared as soon as an interpolation pass takes it, so the debug overlay - which is
            # drawn right at the end, on frames that are finished and in a format text can go on - keeps its
            # own handle on it, and on the framerate its frame numbers are counted in
            debug_dedupe = dedupe
            debug_source_fps = video.fps

    def interpolate_to(method: str, video: vs.VideoNode, new_fps, dedupe=None):
        """Interpolate `video` up to `new_fps`, filling `dedupe`'s gaps on the way if it was given any."""
        match method:
            case "svp":
                if settings["manual_svp"]:
                    # the framerate goes in unless the hand written string already says one. (retiming
                    # replaces it either way - it interpolates at a rate of its own choosing)
                    smooth_json = json.loads(settings["smooth_string"])
                    if "rate" not in smooth_json:
                        smooth_json["rate"] = {"num": int(new_fps), "abs": True}

                    return blur.interpolate.svp(
                        video,
                        video_info=video_info,
                        super_string=settings["super_string"],
                        vectors_string=settings["vectors_string"],
                        smooth_str=json.dumps(smooth_json),
                        dedupe=dedupe,
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
                    dedupe=dedupe,
                )

            case "rife":
                return blur.interpolate.interpolate_rife(
                    video,
                    video_info=video_info,
                    new_fps=new_fps,
                    model_path=settings["rife_model"],
                    device_index=rife_device_index,
                    dedupe=dedupe,
                )

            case "rife (tensorrt)":
                return blur.interpolate.interpolate_rife_vsmlrt(
                    video,
                    video_info=video_info,
                    new_fps=new_fps,
                    model=settings["rife_trt_model"],
                    device_index=tensorrt_device_index,
                    settings_path=settings_path,
                    dedupe=dedupe,
                )

            case "mvtools":
                return blur.interpolate.interpolate_mvtools(
                    video,
                    new_fps,
                    blocksize=interpolation_blocksize,
                    masking=interpolation_mask_area,
                    dedupe=dedupe,
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

        if (
            settings["interpolation_method"] != settings["pre_interpolation_method"]
            and settings["pre_interpolate"]
        ):
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

                # whichever interpolation runs first is the one that fills deduplication's gaps, since after
                # it there's nothing left of the source timeline to fill them on
                video = interpolate_to(
                    settings["pre_interpolation_method"],
                    video,
                    pre_interpolated_fps,
                    dedupe=dedupe,
                )
                dedupe = None

                fps_added = video.fps - old_fps
                log.info(
                    f"added {fps_added} (interp: {pre_interpolated_fps}. video.fps: {video.fps}/{pre_interpolated_fps})"
                )

        if video.fps < interpolated_fps:
            log.info(
                f"interpolating to {interpolated_fps} with {settings['interpolation_method']}"
            )
            old_fps = video.fps

            video = interpolate_to(
                settings["interpolation_method"], video, interpolated_fps, dedupe=dedupe
            )
            dedupe = None

            fps_added = video.fps - old_fps
            log.info(
                f"added {fps_added} (interp: {interpolated_fps}. video.fps: {video.fps}/{interpolated_fps})"
            )

        interpolated = video.num_frames != frames_before_interpolation

    if dedupe is not None:
        # nothing interpolated, so deduplication fills its own gaps, at the framerate the video already has.
        # this is the only place 'deduplicate method' is read - when interpolation runs it takes the timeline
        # instead, and fills the gaps with whatever method it was already going to use
        method = settings["deduplicate_method"]
        log.info(f"filling duplicate frames with {method}")

        if method == "old":
            # the one method that doesn't retime - it patches a blend over each duplicate instead, so it has
            # no use for the timeline
            video = blur.deduplicate.fill_drops_old(
                video,
                threshold=deduplicate_threshold,
                debug=settings["debug"],
            )
        else:
            video = interpolate_to(method, video, video.fps, dedupe=dedupe)

    # masking. deduplication is included because filling a dropped frame means interpolating one, and it's
    # interpolation that warps an overlay - but if neither actually ran there are no artifacts to put back
    if mask_name and (deduplicating or interpolated):
        log.info(f"applying mask {mask_name}")

        if mask_name == blur.mask.AUTO:
            mask_clip = blur.mask.cached(
                mask_source,
                video_path,
                settings_path / blur.mask.CACHE_FOLDER,
                (mask_start, mask_end),
            )
        else:
            mask_clip = blur.mask.load(settings_path / "masks" / mask_name)

        # generate returns None when it couldn't find an overlay worth protecting, in which case there's
        # nothing sensible to mask and the video is left as it is
        if mask_clip is not None:
            video = blur.mask.protect(video, original, mask_clip)

    # debug: write over the frames deduplication had a hand in, and only those. drawn after masking so the
    # text can't be masked away, and before blending - which averages frames together, and will smear this
    # along with everything else, so turn blur off to read it
    if debug_dedupe is not None:
        video = blur.deduplicate.annotate(
            video, debug_dedupe, video.fps / debug_source_fps
        )

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
                    gaussian_bound=json.loads(
                        settings["blur_weighting_gaussian_bound"]
                    ),
                )

                gamma = float(settings["blur_gamma"])
                if gamma == 1.0:
                    video = blur.blending.average(video, weights)
                else:
                    video = blur.blending.average_bright(
                        video,
                        video_info,
                        gamma,
                        weights,
                    )

        # set exact fps
        video = blur.interpolate.change_fps(video, settings["blur_output_fps"])

    # filters
    if settings["filters"]:
        if (
            settings["brightness"] != 1
            or settings["contrast"] != 1
            or settings["saturation"] != 1
        ):
            video = u.with_format(
                video,
                video_info,
                vs.YUV444PS,
                lambda video: core.adjust.Tweak(
                    video,
                    bright=settings["brightness"] - 1,
                    cont=settings["contrast"],
                    sat=settings["saturation"],
                ),
            )

    # upscaling (to 4K)
    if settings["upscale"] and video.height < 2160:
        HEIGHT_4K = 2160

        scale_factor = HEIGHT_4K / video.height
        video = core.resize.Point(
            video,
            width=int(round(video.width * scale_factor)),
            height=HEIGHT_4K,
        )

    video.set_output()
except u.BlurException as e:
    u.handle_blur_exception(e)
except Exception as e:
    u.handle_unexpected_exception(e)
