import vapoursynth as vs
from vapoursynth import core

import blur.interpolate

cur_interp = None
dupe_last_good_idx = 0
dupe_next_good_idx = 0


def get_interp(
    clip,
    duplicate_index,
    svp_preset,
    svp_algorithm,
    svp_blocksize,
    svp_masking,
    svp_gpu,
):
    global cur_interp
    global dupe_last_good_idx
    global dupe_next_good_idx

    duped_frame = clip[duplicate_index]

    def find_next_good_frame():
        index = duplicate_index + 1
        last_possible_index = len(clip) - 1 # for clarity (this shit always trips me up)

        while index <= last_possible_index:
            test_frame = clip[index]
            diffclip = core.std.PlaneStats(test_frame, duped_frame)

            for frame_index2, frame2 in enumerate(diffclip.frames()):
                if frame2.props["PlaneStatsDiff"] >= 0.001:
                    return index

            index += 1

        return last_possible_index

    dupe_last_good_idx = duplicate_index - 1

    # find the next non-duplicate frame
    dupe_next_good_idx = find_next_good_frame()

    duped_frames = dupe_next_good_idx - duplicate_index

    # generate fake clip which includes the two good frames. this will be used to interpolate between them.
    # todo: possibly including more frames will result in better results?
    good_frames = clip[dupe_last_good_idx] + clip[dupe_next_good_idx]

    [super_string, vectors_string, smooth_string] = (
        blur.interpolate.generate_svp_strings(
            new_fps=duped_frames + 1,
            preset=svp_preset,
            algorithm=svp_algorithm,
            blocksize=svp_blocksize,
            # overlap=2,
            # speed="medium",
            masking=svp_masking,
            gpu=svp_gpu,
        )
    )

    super = core.svp1.Super(good_frames, super_string)
    vectors = core.svp1.Analyse(
        super["clip"], super["data"], good_frames, vectors_string
    )

    cur_interp = core.svp2.SmoothFps(
        good_frames,
        super["clip"],
        super["data"],
        vectors["clip"],
        vectors["data"],
        smooth_string,
        src=good_frames,
        fps=good_frames.fps,
    )

    # trim edges (they're just the input frames)
    cur_interp = cur_interp[1:-1]

    cur_interp = core.std.AssumeFPS(cur_interp, fpsnum=1, fpsden=1)


def interpolate_dupes(
    clip, frame_index, svp_preset, svp_algorithm, svp_blocksize, svp_masking, svp_gpu
):
    global cur_interp
    global dupe_last_good_idx
    global dupe_next_good_idx

    clip1 = core.std.AssumeFPS(clip, fpsnum=1, fpsden=1)

    if cur_interp is None:
        # haven't interpolated yet
        get_interp(
            clip1,
            frame_index,
            svp_preset,
            svp_algorithm,
            svp_blocksize,
            svp_masking,
            svp_gpu,
        )

    # combine the good frames with the interpolated ones so that vapoursynth can use them by indexing
    # (i hate how you have to do this, there might be nicer way idk)
    good_before = core.std.Trim(clip1, first=0, last=dupe_last_good_idx)
    good_after = core.std.Trim(clip1, first=dupe_next_good_idx)

    joined = good_before + cur_interp + good_after

    return core.std.AssumeFPS(joined, src=clip)


def fill_drops(
    clip,
    threshold=0.1,
    svp_preset="default",
    svp_algorithm=13,
    svp_blocksize=8,
    svp_masking=50,
    svp_gpu=True,
    debug=False,
):
    if not isinstance(clip, vs.VideoNode):
        raise ValueError("This is not a clip")

    def handle_frames(n, f):
        global cur_interp

        if f.props["PlaneStatsDiff"] > threshold or n == 0:
            cur_interp = None
            return clip

        # duplicate frame
        interp = interpolate_dupes(
            clip, n, svp_preset, svp_algorithm, svp_blocksize, svp_masking, svp_gpu
        )

        if debug:
            return core.text.Text(
                clip=interp,
                text="duplicate, diff: " + str(f.props["PlaneStatsDiff"]),
                alignment=8,
            )

        return interp

    diffclip = core.std.PlaneStats(clip, clip[0] + clip)
    return core.std.FrameEval(clip, handle_frames, prop_src=diffclip)


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


def fill_drops_svp(
    video,
    preset,
    algorithm,
    blocksize,
    overlap,
    masking,
    gpu,
    threshold=0.1,
    debug=False,
):
    if not isinstance(video, vs.VideoNode):
        raise ValueError("This is not a video")

    differences = core.std.PlaneStats(video, video[0] + video)

    [super_string, vectors_string, smooth_string] = (
        blur.interpolate.generate_svp_strings(
            video.fps,
            preset=preset,
            algorithm=algorithm,
            blocksize=blocksize,
            overlap=overlap,
            masking=masking,
            gpu=gpu,
        )
    )

    super = core.svp1.Super(video, super_string)
    vectors = core.svp1.Analyse(super["clip"], super["data"], video, vectors_string)
    filldrops = core.svp2.SmoothFps(
        video,
        super["clip"],
        super["data"],
        vectors["clip"],
        vectors["data"],
        smooth_string,
        src=video,
        fps=video.fps,
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
            return video

    return core.std.FrameEval(video, selectFunc, prop_src=differences)


def fill_drops_mvtools(clip, threshold=0.1, debug=False):
    if not isinstance(clip, vs.VideoNode):
        raise ValueError("This is not a clip")

    differences = core.std.PlaneStats(clip, clip[0] + clip)

    pel = 4
    rfilter = 4
    sharp = 0
    blksize = 4
    overlap = 2
    search = 5
    searchparam = 3
    dct = 5

    super = core.mv.Super(
        clip, hpad=blksize, vpad=blksize, pel=pel, rfilter=rfilter, sharp=sharp
    )

    analyse_args = dict(
        blksize=blksize,
        overlap=overlap,
        search=search,
        searchparam=searchparam,
        dct=dct,
    )

    bv = core.mv.Analyse(super, isb=True, **analyse_args)
    fv = core.mv.Analyse(super, isb=False, **analyse_args)

    filldrops = core.mv.FlowInter(clip, super, mvbw=bv, mvfw=fv, ml=200)

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
