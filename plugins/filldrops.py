import vapoursynth as vs
from vapoursynth import core

def FillDrops(clip, thresh=0.1):
    if not isinstance(clip, vs.VideoNode):
        raise ValueError('This is not a clip')

    differences = core.std.PlaneStats(clip, clip[0] + clip)

    super = core.mv.Super(clip)
    forward_vectors = core.mv.Analyse(super, isb=False)
    backwards_vectors = core.mv.Analyse(super, isb=True)
    filldrops = core.mv.FlowInter(clip, super, mvbw=backwards_vectors, mvfw=forward_vectors, ml=1)
    
    def selectFunc(n, f):
        if f.props['PlaneStatsDiff'] < thresh:
            return filldrops
        else:
            return clip

    fixed = core.std.FrameEval(clip, selectFunc, prop_src=differences)
    return fixed
