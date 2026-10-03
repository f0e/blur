"""Makes a 'large motion' version of a RIFE v2 onnx model (as used by vs-mlrt) for blur.

The frames are box-averaged down before the network sees them and the network's flow and blend mask are scaled back
up and applied to the full resolution frames. Estimating motion at half resolution doubles how far RIFE can follow
things between frames, which is what goes wrong on fast motion at low framerates. Box averaging (rather than RIFE's
own scale option, which resizes without antialiasing) keeps thin static detail like a crosshair from being dragged
along with the background.

Every layer added is named lm_*. blur runs these models in fp32 (see interpolate.is_large_motion_model): their
normalised sampling coordinates are off by over half a pixel at 1440p in fp16.

The interface matches the original: input (1, 7, H, W) = frame 0, frame 1, timestep plane. output (1, 3, H, W).
H and W must be divisible by 1 / scale, which blur's padding to 64 covers.

Needs the onnx package. usage: python export_rife_large_motion.py rife_v4.26.onnx rife_v4.26_large_motion.onnx [scale]
"""

import sys

import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper

# the final flow (t->0 then t->1) and blend mask inside the 4.26 model, at its padded processing resolution
FLOW = "/Add_7_output_0"
MASK = "/Sigmoid_output_0"


def toposort(graph):
    available = {i.name for i in graph.input} | {i.name for i in graph.initializer} | {""}
    pending, ordered = list(graph.node), []
    while pending:
        ready, waiting = [], []
        for node in pending:
            (ready if all(i in available for i in node.input) else waiting).append(node)
        if not ready:
            raise RuntimeError(f"missing input near {waiting[0].name}: {list(waiting[0].input)}")
        for node in ready:
            ordered.append(node)
            available.update(node.output)
        pending = waiting
    del graph.node[:]
    graph.node.extend(ordered)


def build(src, scale):
    down = round(1 / scale)
    model = onnx.load(src)
    g = model.graph

    produced = {o for node in g.node for o in node.output}
    if FLOW not in produced or MASK not in produced:
        raise ValueError(f"{src} doesn't look like RIFE 4.26 (no {FLOW} / {MASK})")

    # the network now runs on the shrunk frames, and its own output (a warp at that size) is left unused
    for node in g.node:
        node.input[:] = ["lm_small" if i == "input" else i for i in node.input]
        node.output[:] = ["lm_unused" if o == "output" else o for o in node.output]
    del g.input[:]
    del g.output[:]
    g.input.append(helper.make_tensor_value_info("input", TensorProto.FLOAT, [1, 7, "H", "W"]))
    g.output.append(helper.make_tensor_value_info("output", TensorProto.FLOAT, [1, 3, "H", "W"]))

    count = [0]

    def const(value, dtype=np.float32):
        count[0] += 1
        name = f"lm_c{count[0]}"
        g.initializer.append(numpy_helper.from_array(np.array(value, dtype=dtype), name))
        return name

    def node(op, inputs, n_out=1, **attrs):
        count[0] += 1
        name = f"lm_{op}_{count[0]}"
        outs = [f"{name}_o{i}" for i in range(n_out)]
        g.node.append(helper.make_node(op, inputs, outs, name=name, **attrs))
        return outs[0] if n_out == 1 else outs

    i64 = np.int64
    one = const(1.0)
    g.node.append(
        helper.make_node(
            "AveragePool", ["input"], ["lm_small"], name="lm_pool", kernel_shape=[down, down], strides=[down, down]
        )
    )

    # crop the padded flow and mask back to the shrunk size, then scale them up to the frame size
    hw_small = node("Slice", [node("Shape", ["lm_small"]), const([2], i64), const([4], i64)])
    zeros, axes = const([0, 0], i64), const([2, 3], i64)
    flow_small = node("Slice", [FLOW, zeros, hw_small, axes])
    mask_small = node("Slice", [MASK, zeros, hw_small, axes])
    shape = node("Shape", ["input"])
    hw = node("Slice", [shape, const([2], i64), const([4], i64)])
    resize = {"mode": "linear", "coordinate_transformation_mode": "half_pixel"}
    flow_sizes = node("Concat", [const([1, 4], i64), hw], axis=0)
    flow = node("Mul", [node("Resize", [flow_small, "", "", flow_sizes], **resize), const(float(down))])
    mask = node("Resize", [mask_small, "", "", node("Concat", [const([1, 1], i64), hw], axis=0)], **resize)

    # normalised sampling grid at full resolution
    h = node("Cast", [node("Gather", [shape, const(2, i64)], axis=0)], to=TensorProto.FLOAT)
    w = node("Cast", [node("Gather", [shape, const(3, i64)], axis=0)], to=TensorProto.FLOAT)
    step_y = node("Div", [const(2.0), node("Sub", [h, one])])
    step_x = node("Div", [const(2.0), node("Sub", [w, one])])
    ys = node("Sub", [node("Mul", [node("Range", [const(0.0), h, one]), step_y]), one])
    xs = node("Sub", [node("Mul", [node("Range", [const(0.0), w, one]), step_x]), one])
    ys = node("Unsqueeze", [ys, const([0, 2], i64)])
    xs = node("Unsqueeze", [xs, const([0, 1], i64)])

    frame0, frame1, _ = node("Split", ["input", const([3, 3, 1], i64)], n_out=3, axis=1)
    fx0, fy0, fx1, fy1 = node("Split", [flow, const([1, 1, 1, 1], i64)], n_out=4, axis=1)
    channel, last = const([1], i64), const([-1], i64)
    warped = []
    for frame, fx, fy in ((frame0, fx0, fy0), (frame1, fx1, fy1)):
        gx = node("Add", [xs, node("Mul", [node("Squeeze", [fx, channel]), step_x])])
        gy = node("Add", [ys, node("Mul", [node("Squeeze", [fy, channel]), step_y])])
        grid = node("Concat", [node("Unsqueeze", [gx, last]), node("Unsqueeze", [gy, last])], axis=-1)
        warped.append(node("GridSample", [frame, grid], mode="bilinear", padding_mode="border", align_corners=1))
    blended = node("Add", [node("Mul", [mask, warped[0]]), node("Mul", [node("Sub", [one, mask]), warped[1]])])
    g.node.append(helper.make_node("Identity", [blended], ["output"], name="lm_out"))

    toposort(g)
    onnx.checker.check_model(model)
    return model


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    onnx.save(build(sys.argv[1], float(sys.argv[3]) if len(sys.argv) > 3 else 0.5), sys.argv[2])
    print("wrote", sys.argv[2])
