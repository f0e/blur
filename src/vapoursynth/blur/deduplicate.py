import vapoursynth as vs
from vapoursynth import core
from typing import Literal

import blur.interpolate
import blur.utils as u

cur_interp = None
dupe_last_good_idx = -1
dupe_next_good_idx = -1
dupe_start_idx = -1
dupe_end_idx = -1
num_duped_frames = 0
dupe_idx = 0

DEBUG_ENABLED = False


def debug_print(*args):
    if DEBUG_ENABLED:
        print("[DEBUG]:", *args)


def find_next_good_frame(clip, duplicate_index: int, threshold: float, max_frames: int):
    duped_frame = clip[duplicate_index]
    index = duplicate_index + 1
    last_possible_index = len(clip) - 1  # for clarity (this shit always trips me up)

    if max_frames:
        max_permitted = duplicate_index + max_frames
        if last_possible_index > max_permitted:
            last_possible_index = max_permitted

    while index <= last_possible_index:
        test_frame = clip[index]
        diffclip = core.std.PlaneStats(test_frame, duped_frame)

        for frame2 in diffclip.frames():
            if frame2.props["PlaneStatsDiff"] >= threshold:
                return index

        index += 1

    return -1


def find_last_good_frame(clip, duplicate_index: int, threshold: float, max_frames: int):
    duped_frame = clip[duplicate_index]
    index = duplicate_index - 1
    first_possible_index = 0

    if max_frames:
        max_back = duplicate_index - max_frames
        if first_possible_index < max_back:
            first_possible_index = max_back

    while index >= first_possible_index:
        test_frame = clip[index]
        diffclip = core.std.PlaneStats(test_frame, duped_frame)

        for frame2 in diffclip.frames():
            if frame2.props["PlaneStatsDiff"] >= threshold:
                return index

        index -= 1

    return -1


def get_interp(
    clip,
    duplicate_index: int,
    threshold: float,
    max_frames: int,
    interp_creator,
    duplicate_mode: Literal["hold_last", "hold_next"],
):
    global \
        cur_interp, \
        dupe_last_good_idx, \
        dupe_next_good_idx, \
        num_duped_frames, \
        dupe_idx
    global dupe_start_idx, dupe_end_idx

    num_duped_frames = 0

    if duplicate_mode == "hold_last":  # A, A, A, B
        dupe_last_good_idx = duplicate_index - 1
        dupe_next_good_idx = find_next_good_frame(
            clip, duplicate_index, threshold, max_frames
        )

        if dupe_next_good_idx == -1:
            return

        # TODO: review
        dupe_start_idx = dupe_last_good_idx
        dupe_end_idx = dupe_next_good_idx

        num_duped_frames = dupe_next_good_idx - dupe_last_good_idx

    else:  # A, B, B, B
        dupe_last_good_idx = find_last_good_frame(
            clip, duplicate_index, threshold, max_frames
        )
        dupe_next_good_idx = duplicate_index

        if dupe_last_good_idx == -1:
            return

        dupe_start_idx = dupe_next_good_idx
        next_frame = find_next_good_frame(
            clip, dupe_last_good_idx + 1, threshold, max_frames
        )
        if next_frame == -1:
            if not max_frames:
                # hit end of video, assume things change there
                next_frame = len(clip)
            else:
                # just abort to avoid weirdness TODO: check this is right
                return

        dupe_end_idx = next_frame - 1  # dupes end the frame before the next good frame

        debug_print(
            f"last good: {dupe_last_good_idx}, next good: {dupe_next_good_idx} (actual: {dupe_end_idx})"
        )

        num_duped_frames = dupe_end_idx - dupe_last_good_idx

    # generate fake clip which includes the two good frames. this will be used to interpolate between them.
    # todo: possibly including more frames will result in better results?
    good_frames = clip[dupe_last_good_idx] + clip[dupe_next_good_idx]

    debug_print(
        f"doing interp. input frames: {dupe_last_good_idx} and {dupe_next_good_idx}"
    )
    cur_interp = interp_creator(good_frames, num_duped_frames)
    cur_interp = core.std.AssumeFPS(cur_interp, fpsnum=1, fpsden=1)
    dupe_idx += 1

    debug_print(
        f"interpolated {len(cur_interp)} frames from {len(good_frames)} source frames"
    )


def interpolate_dupes(
    clip: vs.VideoNode,
    frame_index: int,
    threshold: float,
    max_frames: int,
    interp_creator,
    duplicate_mode: Literal["hold_last", "hold_next"],
):
    global cur_interp, dupe_last_good_idx, dupe_next_good_idx
    global dupe_start_idx, dupe_end_idx

    clip1 = core.std.AssumeFPS(clip, fpsnum=1, fpsden=1)

    if cur_interp is None:
        # haven't interpolated yet
        debug_print("havent interpolated yet")
        get_interp(
            clip1,
            frame_index,
            threshold,
            max_frames,
            interp_creator,
            duplicate_mode,
        )

    if cur_interp is None:
        # interpolated but no dedupe solution. get out
        return clip, False

    debug_print("creating interpolated output")

    # combine the good frames with the interpolated ones so that vapoursynth can use them by indexing
    # (i hate how you have to do this, there might be nicer way idk)
    joined = core.std.Trim(clip1, first=0, last=dupe_start_idx - 1)
    debug_print(f"0-{dupe_start_idx}")

    joined += cur_interp
    debug_print(f"({len(cur_interp)} interpolated frames)")

    if dupe_end_idx + 1 < len(clip):
        joined += core.std.Trim(clip1, first=dupe_end_idx + 1)
        debug_print(f"{dupe_end_idx}-end")

    debug_print(f"debug len check: {len(clip)} == {len(joined)}")

    if frame_index >= dupe_end_idx:
        # last frame of this interp, reset it
        cur_interp = None

    return core.std.AssumeFPS(joined, src=clip), True


def create_frame_handler(
    video,
    threshold,
    max_frames,
    interp_creator,
    debug,
    duplicate_mode: Literal["hold_last", "hold_next"],
):
    def handle_frames(n):
        global cur_interp
        debug_print(f"\n\n--{n}--\n\n")

        debug_print(f"start: {n}/{len(video) - 1} ({dupe_start_idx}-{dupe_end_idx})")

        diff = -1

        if n >= dupe_start_idx and n <= dupe_end_idx:
            # inside a dupe range already
            pass
        else:
            if duplicate_mode == "hold_next":
                if n + 1 >= len(video):
                    debug_print("cant diff past end of vid")
                    return video

                diffclip = core.std.PlaneStats(video[n], video[n + 1])
            else:
                if n == 0:
                    return video

                diffclip = core.std.PlaneStats(video[n - 1], video[n])

            diff = next(diffclip.frames()).props["PlaneStatsDiff"]

            debug_print(f"diff: {diff}")

            if diff >= threshold:
                cur_interp = None
                return video

        out_video, was_dupe = interpolate_dupes(
            video,
            n,
            threshold,
            max_frames,
            interp_creator,
            duplicate_mode,
        )

        if debug:
            if was_dupe:
                gap = num_duped_frames - 1
                return core.text.Text(
                    clip=out_video,
                    text=f"{n} | duplicate, {gap} gap, diff: {diff:.4f} | dupe_idx: {dupe_idx}, dupe_last_good_idx: {dupe_last_good_idx}, dupe_next_good_idx: {dupe_next_good_idx}, num_duped_frames: {num_duped_frames} | start: {dupe_start_idx}, end: {dupe_end_idx}",
                    alignment=8,
                )
            else:
                return core.text.Text(
                    clip=out_video,
                    text=f"{n}",
                    alignment=8,
                )

        return out_video

    return handle_frames


def create_rife_interp(good_frames, duped_frames, model_path: str, gpu_index: int):
    interp = blur.interpolate.RIFE(
        good_frames,
        new_fps=duped_frames,
        model_path=model_path,
        gpu_index=gpu_index,
    )

    return interp[1 : 1 + duped_frames]  # first frame is a duplicate


def fill_drops_rife(
    _video: vs.VideoNode,
    video_info: u.VideoInfo,
    model_path: str,
    gpu_index: int,
    threshold: float = 0.1,
    max_frames: int | None = None,
    duplicate_mode: Literal["hold_last", "hold_next"] = "hold_next",
    debug=False,
):
    u.check_model_path(model_path)

    def process(video):
        handler = create_frame_handler(
            video,
            threshold,
            max_frames,
            lambda good_frames, duped_frames: create_rife_interp(
                good_frames,
                duped_frames,
                model_path,
                gpu_index,
            ),
            debug,
            duplicate_mode,
        )
        return core.std.FrameEval(video, handler)

    return u.with_format(
        _video,
        video_info,
        vs.RGBS,
        process,
    )


def create_svp_interp(
    good_frames,
    duped_frames,
    svp_preset: str,
    svp_algorithm: int,
    svp_blocksize: int,
    svp_masking: int,
    svp_gpu: bool,
):
    [super_string, vectors_string, smooth_string] = (
        blur.interpolate.generate_svp_strings(
            new_fps=duped_frames,
            preset=svp_preset,
            algorithm=svp_algorithm,
            blocksize=svp_blocksize,
            # overlap=2,
            # speed="medium",
            masking=svp_masking,
            gpu=svp_gpu,
        )
    )

    interp = blur.interpolate.SVP(
        good_frames, super_string, vectors_string, smooth_string
    )

    return interp[1 : 1 + duped_frames]  # first frame is a duplicate


def fill_drops_svp(
    _video: vs.VideoNode,
    video_info: u.VideoInfo,
    threshold: float = 0.1,
    max_frames: int | None = None,
    duplicate_mode: Literal["hold_last", "hold_next"] = "hold_next",
    svp_preset=blur.interpolate.DEFAULT_PRESET,
    svp_algorithm=blur.interpolate.DEFAULT_ALGORITHM,
    svp_blocksize=blur.interpolate.DEFAULT_BLOCKSIZE,
    svp_masking=blur.interpolate.DEFAULT_MASKING,
    svp_gpu=blur.interpolate.DEFAULT_GPU,
    debug=False,
):
    _video = core.fmtc.bitdepth(_video, bits=8)

    def process(video):
        handler = create_frame_handler(
            video,
            threshold,
            max_frames,
            lambda good_frames, duped_frames: create_svp_interp(
                good_frames,
                duped_frames,
                svp_preset,
                svp_algorithm,
                svp_blocksize,
                svp_masking,
                False,  # TODO: check if false is actually faster (i think it is, gpu initialisation is slow(?), and it has to happen lots here) # svp_gpu,
            ),
            debug,
            duplicate_mode,
        )
        return core.std.FrameEval(video, handler)

    return u.with_scaled_luminance(
        _video,
        vs.YUV420P8,
        process,
    )


def create_mvtools_interp(
    good_frames,
    duped_frames,
    blocksize: int,
    masking: int,
    pel: int,
    sharp: int,
    overlap: int,
    search: int,
    searchparam: int,
    pelsearch: int,
    dct: int,
):
    super = core.mv.Super(
        good_frames, hpad=blocksize, vpad=blocksize, pel=pel, rfilter=1, sharp=sharp
    )

    analyse_args = dict(
        blksize=blocksize,
        overlap=overlap,
        search=search,
        searchparam=searchparam,
        pelsearch=pelsearch,
        dct=dct,
    )

    bv = core.mv.Analyse(super, isb=True, **analyse_args)
    fv = core.mv.Analyse(super, isb=False, **analyse_args)

    interp = core.mv.FlowFPS(
        good_frames,
        super,
        bv,
        fv,
        num=duped_frames,
        den=1,
        blend=False,
        ml=max(masking, 1),
    )

    return interp[1 : 1 + duped_frames]  # first frame is a duplicate


def fill_drops_mvtools(
    video: vs.VideoNode,
    threshold: float = 0.1,
    max_frames: int | None = None,
    duplicate_mode: Literal["hold_last", "hold_next"] = "hold_next",
    blocksize: int = 4,
    masking: int = 100,
    pel: int = 1,
    sharp: int = 0,
    overlap: int = 2,
    search: int = 5,
    searchparam: int = 3,
    pelsearch: int = 1,
    dct: int = 3,
    debug=False,
):
    handler = create_frame_handler(
        video,
        threshold,
        max_frames,
        lambda good_frames, duped_frames: create_mvtools_interp(
            good_frames,
            duped_frames,
            blocksize,
            masking,
            pel,
            sharp,
            overlap,
            search,
            searchparam,
            pelsearch,
            dct,
        ),
        debug,
        duplicate_mode,
    )
    return core.std.FrameEval(video, handler)


def fill_drops_old(clip, threshold=0.1, debug=False):
    if not isinstance(clip, vs.VideoNode):
        raise ValueError("This is not a clip")

    differences = core.std.PlaneStats(clip, clip[0] + clip)

    super = core.mv.Super(clip)
    forward_vectors = core.mv.Analyse(super, isb=False)
    backwards_vectors = core.mv.Analyse(super, isb=True)
    filldrops = core.mv.FlowInter(
        clip, super, mvbw=backwards_vectors, mvfw=forward_vectors, ml=1
    )

    def selectFunc(n, f):
        if f.props["PlaneStatsDiff"] < threshold:
            if debug:
                return core.text.Text(
                    filldrops,
                    f"interpolated, diff: {f.props['PlaneStatsDiff']:.3f}",
                    alignment=8,
                )

            return filldrops
        else:
            return clip

    return core.std.FrameEval(clip, selectFunc, prop_src=differences)


# def fill_drops_old_svp(
#     video,
#     threshold: float = 0.1,
#     svp_preset=blur.interpolate.DEFAULT_PRESET,
#     svp_algorithm=blur.interpolate.DEFAULT_ALGORITHM,
#     svp_blocksize=blur.interpolate.DEFAULT_BLOCKSIZE,
#     svp_masking=blur.interpolate.DEFAULT_MASKING,
#     svp_gpu=blur.interpolate.DEFAULT_GPU,
#     debug=False,
# ):
#     if not isinstance(video, vs.VideoNode):
#         raise ValueError("This is not a video")

#     [super_string, vectors_string, smooth_string] = (
#         blur.interpolate.generate_svp_strings(
#             new_fps=video.fps,
#             preset=svp_preset,
#             algorithm=svp_algorithm,
#             blocksize=svp_blocksize,
#             masking=svp_masking,
#             gpu=svp_gpu,
#         )
#     )

#     super = core.svp1.Super(video, super_string)
#     vectors = core.svp1.Analyse(super["clip"], super["data"], video, vectors_string)
#     filldrops = core.svp2.SmoothFps(
#         video,
#         super["clip"],
#         super["data"],
#         vectors["clip"],
#         vectors["data"],
#         smooth_string,
#         src=video,
#         fps=video.fps,
#     )

#     def selectFunc(n, f):
#         if f.props["PlaneStatsDiff"] >= threshold or n == 0:
#             return video

#         clip_1fps = core.std.AssumeFPS(video, fpsnum=1, fpsden=1)

#         good_frames = clip_1fps[n - 1] + clip_1fps[n + 1]

#         [super_string, vectors_string, smooth_string] = (
#             blur.interpolate.generate_svp_strings(
#                 new_fps=3,
#                 preset=svp_preset,
#                 algorithm=svp_algorithm,
#                 blocksize=svp_blocksize,
#                 # overlap=2,
#                 # speed="medium",
#                 masking=svp_masking,
#                 gpu=svp_gpu,
#             )
#         )

#         super = core.svp1.Super(good_frames, super_string)
#         vectors = core.svp1.Analyse(
#             super["clip"], super["data"], good_frames, vectors_string
#         )

#         cur_interp = core.svp2.SmoothFps(
#             good_frames,
#             super["clip"],
#             super["data"],
#             vectors["clip"],
#             vectors["data"],
#             smooth_string,
#             src=good_frames,
#             fps=good_frames.fps,
#         )

#         # trim edges (they're just the input frames)
#         cur_interp = cur_interp[1:-1]

#         # combine the good frames with the interpolated ones so that vapoursynth can use them by indexing
#         # (i hate how you have to do this, there might be nicer way idk)
#         good_before = core.std.Trim(clip_1fps, first=0, last=n - 1)
#         good_after = core.std.Trim(clip_1fps, first=n + 1)

#         joined = good_before + cur_interp + good_after

#         out_video = core.std.AssumeFPS(joined, src=video)

#         if debug:
#             return core.text.Text(
#                 out_video,
#                 f"interpolated, diff: {f.props['PlaneStatsDiff']:.3f}",
#                 alignment=8,
#             )

#         return out_video

#     differences = core.std.PlaneStats(video, video[0] + video)
#     return core.std.FrameEval(video, selectFunc, prop_src=differences)


# def fill_drops_old_mvtools(clip, threshold=0.1, debug=False):
#     if not isinstance(clip, vs.VideoNode):
#         raise ValueError("This is not a clip")

#     differences = core.std.PlaneStats(clip, clip[0] + clip)

#     pel = 4
#     rfilter = 4
#     sharp = 0
#     blksize = 4
#     overlap = 2
#     search = 5
#     searchparam = 3
#     dct = 5

#     super = core.mv.Super(
#         clip, hpad=blksize, vpad=blksize, pel=pel, rfilter=rfilter, sharp=sharp
#     )

#     analyse_args = dict(
#         blksize=blksize,
#         overlap=overlap,
#         search=search,
#         searchparam=searchparam,
#         dct=dct,
#     )

#     bv = core.mv.Analyse(super, isb=True, **analyse_args)
#     fv = core.mv.Analyse(super, isb=False, **analyse_args)

#     filldrops = core.mv.FlowInter(clip, super, mvbw=bv, mvfw=fv, ml=200)

#     def selectFunc(n, f):
#         if f.props["PlaneStatsDiff"] < threshold:
#             if debug:
#                 return core.text.Text(
#                     filldrops,
#                     f"interpolated, diff: {f.props['PlaneStatsDiff']:.3f}",
#                     alignment=8,
#                 )

#             return filldrops
#         else:
#             return clip

#     return core.std.FrameEval(clip, selectFunc, prop_src=differences)
