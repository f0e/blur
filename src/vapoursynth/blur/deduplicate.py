import vapoursynth as vs
from vapoursynth import core
from typing import Literal

import blur.interpolate
import blur.utils as u

DEBUG_ENABLED = False


def frames_diff(framea: vs.VideoNode, frameb: vs.VideoNode):
    return core.std.PlaneStats(framea, frameb).get_frame(0).props["PlaneStatsDiff"]


def find_next_good_frame(clip, duplicate_index: int, threshold: float, max_frames: int):
    duped_frame = clip[duplicate_index]
    index = duplicate_index + 1
    last_possible_index = len(clip) - 1  # for clarity (this shit always trips me up)

    if max_frames:
        max_permitted = duplicate_index + max_frames
        if last_possible_index > max_permitted:
            last_possible_index = max_permitted

    while index <= last_possible_index:
        if frames_diff(duped_frame, clip[index]) >= threshold:
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
        if frames_diff(duped_frame, clip[index]) >= threshold:
            return index

        index -= 1

    return -1


class DupeState:
    def __init__(self):
        self.cur_interp = None
        self.last_good_idx = -1
        self.next_good_idx = -1
        self.start_idx = -1
        self.end_idx = -1
        self.num_duped_frames = 0
        self.dupe_idx = 0

    def reset_interp(self):
        self.cur_interp = None

    def compute_interp(
        self,
        clip,
        duplicate_index: int,
        threshold: float,
        max_frames: int,
        interp_creator,
        duplicate_mode: Literal["hold_last", "hold_next"],
    ):
        self.num_duped_frames = 0

        if duplicate_mode == "hold_last":  # A, A, A, B
            self.last_good_idx = duplicate_index - 1
            self.next_good_idx = find_next_good_frame(
                clip, duplicate_index, threshold, max_frames
            )

            if self.next_good_idx == -1:
                return

            # TODO: review
            self.start_idx = self.last_good_idx
            self.end_idx = self.next_good_idx
            self.num_duped_frames = self.next_good_idx - self.last_good_idx

        else:  # A, B, B, B
            self.last_good_idx = find_last_good_frame(
                clip, duplicate_index, threshold, max_frames
            )
            self.next_good_idx = duplicate_index

            if self.last_good_idx == -1:
                return

            self.start_idx = self.next_good_idx
            next_frame = find_next_good_frame(
                clip, self.last_good_idx + 1, threshold, max_frames
            )

            if next_frame == -1:
                if not max_frames:
                    # hit end of video, assume things change there
                    next_frame = len(clip)
                else:
                    # just abort to avoid weirdness TODO: check this is right
                    # thoughts in meantime, if no good frame is found then it could very well be a false positive duplicate frame
                    # e.g. only text is moving on the screen, every frame is gonna be detected as a 'dupe', but that's wrong.
                    # if no good frame is found, just back out and don't dedupe,
                    # *nothing's changing anyway, it's not like frames being duplicated matters*
                    return

            self.end_idx = next_frame - 1

            u.log(
                f"last good: {self.last_good_idx}, next good: {self.next_good_idx} (actual: {self.end_idx})"
            )

            self.num_duped_frames = self.end_idx - self.last_good_idx

        # generate fake clip which includes the two good frames. this will be used to interpolate between them.
        # todo: possibly including more frames will result in better results?
        good_frames = clip[self.last_good_idx] + clip[self.next_good_idx]

        u.log(
            f"doing interp. input frames: {self.last_good_idx} and {self.next_good_idx}"
        )

        self.cur_interp = interp_creator(good_frames, self.num_duped_frames)
        self.cur_interp = core.std.AssumeFPS(self.cur_interp, fpsnum=1, fpsden=1)
        self.dupe_idx += 1

        u.log(
            f"interpolated {len(self.cur_interp)} frames from {len(good_frames)} source frames"
        )

    def interpolate_dupes(
        self,
        clip: vs.VideoNode,
        frame_index: int,
        threshold: float,
        max_frames: int,
        interp_creator,
        duplicate_mode: Literal["hold_last", "hold_next"],
    ):
        clip1 = core.std.AssumeFPS(clip, fpsnum=1, fpsden=1)

        if self.cur_interp is None:
            # haven't interpolated yet
            u.log("havent interpolated yet")
            self.compute_interp(
                clip1,
                frame_index,
                threshold,
                max_frames,
                interp_creator,
                duplicate_mode,
            )

        if self.cur_interp is None:
            # interpolated but no dedupe solution. get out
            return clip, False

        u.log("creating interpolated output")

        # combine the good frames with the interpolated ones so that vapoursynth can use them by indexing
        # (i hate how you have to do this, there might be nicer way idk)
        joined = core.std.Trim(clip1, first=0, last=self.start_idx - 1)
        u.log(f"0-{self.start_idx}")

        joined += self.cur_interp
        u.log(f"({len(self.cur_interp)} interpolated frames)")

        if self.end_idx + 1 < len(clip):
            joined += core.std.Trim(clip1, first=self.end_idx + 1)
            u.log(f"{self.end_idx}-end")

        u.log(f"debug len check: {len(clip)} == {len(joined)}")

        if frame_index >= self.end_idx:
            # last frame of this interp, reset it
            self.reset_interp()

        return core.std.AssumeFPS(joined, src=clip), True


def create_frame_handler(
    video,
    threshold,
    max_frames,
    interp_creator,
    debug,
    duplicate_mode: Literal["hold_last", "hold_next"],
):
    state = DupeState()

    def handle_frames(n):
        u.log(f"\n\n--{n}/{len(video) - 1}--\n")

        diff = -1
        debug_parts = []

        if debug:
            debug_parts.append(f"{n}/{len(video)}")

        if state.start_idx <= n <= state.end_idx:
            # inside a dupe range already
            u.log(f"inside a dupe ({state.start_idx}-{state.end_idx})")

            if debug:
                debug_parts.append(
                    f"using previous interp ({state.start_idx} <= {n} <= {state.end_idx})"
                )
        else:
            if duplicate_mode == "hold_next":
                if n + 1 >= len(video):
                    u.log("cant diff past end of vid")
                    return video

                diff = frames_diff(video[n], video[n + 1])
            else:
                if n == 0:
                    return video

                diff = frames_diff(video[n - 1], video[n])

            u.log(f"diff: {diff}/{threshold}")

            if debug:
                debug_parts.append(f"diff: {diff:.6f}")

            # return core.text.Text(
            #     clip=video,
            #     text=f"{n} |{'' if diff >= threshold else ' >DUPE<'} diff: {diff:.6f}/{threshold:.6f}",
            #     alignment=8,
            # )

            if diff >= threshold:
                u.log("not a dupe")
                state.reset_interp()
                return video

            u.log("its a dupe")

        out_video, was_dupe = state.interpolate_dupes(
            video,
            n,
            threshold,
            max_frames,
            interp_creator,
            duplicate_mode,
        )

        if debug:
            if was_dupe:
                debug_parts.append(
                    f"dupe_idx: {state.dupe_idx}, dupe_last_good_idx: {state.last_good_idx}, dupe_next_good_idx: {state.next_good_idx}, num_duped_frames: {state.num_duped_frames}"
                )

            return core.text.Text(
                clip=out_video,
                text=" | ".join(debug_parts),
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
    threshold: float,
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
    threshold: float,
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
    threshold: float,
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
#     threshold: float,
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
