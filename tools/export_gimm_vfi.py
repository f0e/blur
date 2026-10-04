"""Exports GIMM-VFI-R (https://github.com/GSeanCDAT/GIMM-VFI) to onnx for vs-mlrt / tensorrt.

The model gets the same interface as vs-mlrt's RIFE v2 models: input (1, 7, H, W) = frame 0, frame 1, timestep plane.
output (1, 3, H, W). H and W can be any multiple of 64 (blur pads to that).

GIMM-VFI doesn't export as is, so this checks out a pinned commit and changes it:
- its cupy softmax splatting becomes the same maths in plain torch (scatter-add)
- raft's correlation lookups and the decoders' warps become gathers rather than grid_sample. tensorrt fuses those
  grid_samples with the layers around them and gets the result wrong
- a few spots that only work for one resolution, or don't export, are rewritten (see EDITS)

The convolutions and the layers between them are stored in fp16, everything else stays fp32. build it as a strongly
typed tensorrt engine (--stronglyTyped) with fp32 frames in and out. a plain fp16 engine is broken, since sampling
coordinates need fp32, and fp16 frames cost about 1dB.

Needs a cuda gpu, git, and: pip install torch onnx onnxscript omegaconf einops easydict yacs
usage: python export_gimm_vfi.py OUT.onnx [--work DIR] [--iters 8] [--checkpoint plain|lpips] [--check]
"""

import argparse
import importlib
import os
import subprocess
import sys
import types
import urllib.request
from pathlib import Path

import torch
import torch.nn.functional as F

REPO = "https://github.com/GSeanCDAT/GIMM-VFI.git"
COMMIT = "dbc56449994a3c2e045d46fd46bed570239913f8"
WEIGHTS = "https://huggingface.co/GSean/GIMM-VFI/resolve/main/"

# raft's flow is estimated at this fraction of the frame size
FLOW_SCALE = 4

INR = "src/models/generalizable_INR/"

# (file, old, new). each old text has to be found exactly once
EDITS = [
    # dataclass fields with mutable defaults, which python 3.11+ refuses
    (INR + "configs.py", "from dataclasses import dataclass\n", "from dataclasses import dataclass, field\n"),
    (
        INR + "configs.py",
        "    hyponet: HypoNetConfig = HypoNetConfig()\n    coord_range: List[float] = MISSING\n    modulated_layer_idxs: Optional[List[int]] = None\n\n    @classmethod",
        "    hyponet: HypoNetConfig = field(default_factory=HypoNetConfig)\n    coord_range: List[float] = MISSING\n    modulated_layer_idxs: Optional[List[int]] = None\n\n    @classmethod",
    ),
    (
        INR + "configs.py",
        "    hyponet: HypoNetConfig = HypoNetConfig()\n    raft_iter",
        "    hyponet: HypoNetConfig = field(default_factory=HypoNetConfig)\n    raft_iter",
    ),
    (
        INR + "modules/module_config.py",
        "from dataclasses import dataclass\n",
        "from dataclasses import dataclass, field\n",
    ),
    (
        INR + "modules/module_config.py",
        "    activation: HypoNetActivationConfig = HypoNetActivationConfig()\n    initialization: HypoNetInitConfig = HypoNetInitConfig()\n",
        (
            "    activation: HypoNetActivationConfig = field(default_factory=HypoNetActivationConfig)\n"
            "    initialization: HypoNetInitConfig = field(default_factory=HypoNetInitConfig)\n"
        ),
    ),
    # the raft iteration count is hardcoded where it's used
    (
        INR + "gimmvfi_r.py",
        "            im0, im1, return_feat=True, iters=20\n",
        "            im0, im1, return_feat=True, iters=iters\n",
    ),
    (
        INR + "gimmvfi_r.py",
        "            im1, im0, return_feat=True, iters=20\n",
        "            im1, im0, return_feat=True, iters=iters\n",
    ),
    # data dependent, so it can't be exported
    (INR + "gimmvfi_r.py", "            assert c[0][0, 0, 0, 0, 0] == t[idx][0].squeeze()\n", ""),
    # onnx's Transpose doesn't take negative axes
    (
        INR + "gimmvfi_r.py",
        "                    c, modulation_params_dict=None, pixel_latent=pixel_latent[idx]\n                ).permute(0, -1, *permute_idx_range)\n",
        (
            "                    c, modulation_params_dict=None, pixel_latent=pixel_latent[idx]\n                )\n"
            "                outputs = outputs.permute(0, outputs.ndim - 1, *permute_idx_range)\n"
        ),
    ),
    # scale factors worked out from shapes don't export, sizes do
    (
        INR + "gimmvfi_r.py",
        (
            "            flowt0_1 = inv * resize(flowt0_1, scale_factor=inv)\n"
            "            flowt1_1 = inv * resize(flowt1_1, scale_factor=inv)\n"
            "            flow_t0_fullsize = inv * resize(flow_t0_fullsize, scale_factor=inv)\n"
            "            flow_t1_fullsize = inv * resize(flow_t1_fullsize, scale_factor=inv)\n"
            "            mask = resize(mask, scale_factor=inv)\n"
            "            img_res = resize(img_res, scale_factor=inv)\n"
        ),
        (
            "            full = img1.shape[-2:]\n\n"
            "            def up(x):\n"
            '                return F.interpolate(x, size=full, mode="bilinear", align_corners=False)\n\n'
            "            flowt0_1 = inv * up(flowt0_1)\n"
            "            flowt1_1 = inv * up(flowt1_1)\n"
            "            flow_t0_fullsize = inv * up(flow_t0_fullsize)\n"
            "            flow_t1_fullsize = inv * up(flow_t1_fullsize)\n"
            "            mask = up(mask)\n"
            "            img_res = up(img_res)\n"
        ),
    ),
    (
        INR + "modules/fi_components.py",
        (
            "        scale_factor = f_in.shape[2] / img0.shape[2]\n"
            "        img0 = resize(img0, scale_factor=scale_factor)\n"
            "        img1 = resize(img1, scale_factor=scale_factor)\n"
        ),
        (
            '        img0 = torch.nn.functional.interpolate(img0, size=f_in.shape[-2:], mode="bilinear", align_corners=False)\n'
            '        img1 = torch.nn.functional.interpolate(img1, size=f_in.shape[-2:], mode="bilinear", align_corners=False)\n'
        ),
    ),
    # squeeze without axes needs every dimension known
    (
        INR + "gimmvfi_r.py",
        "                unnormalize_flow(normal_inr_flows[i], flow_scalers).squeeze()\n                for i in range(len(coord))\n",
        "                unnormalize_flow(normal_inr_flows[i], flow_scalers).squeeze(2)\n                for i in range(len(coord))\n",
    ),
    # l1_loss doesn't export, with reduction none it's just the absolute difference
    (
        INR + "gimmvfi_r.py",
        (
            '        err01 = (\n            torch.nn.functional.l1_loss(\n                input=f01_warp, target=raft_flow01, reduction="none"\n'
            "            )\n            .mean(1)\n            .unsqueeze(1)\n        )\n"
        ),
        "        err01 = (f01_warp - raft_flow01).abs().mean(1).unsqueeze(1)\n",
    ),
    (
        INR + "gimmvfi_r.py",
        (
            '        err02 = (\n            torch.nn.functional.l1_loss(\n                input=f10_warp, target=raft_flow10, reduction="none"\n'
            "            )\n            .mean(1)\n            .unsqueeze(1)\n        )\n"
        ),
        "        err02 = (f10_warp - raft_flow10).abs().mean(1).unsqueeze(1)\n",
    ),
]


def checkout(work: Path) -> Path:
    repo = work / "GIMM-VFI"
    if not repo.exists():
        subprocess.run(["git", "clone", "--quiet", REPO, str(repo)], check=True)
    subprocess.run(["git", "-C", str(repo), "checkout", "--quiet", "--force", COMMIT], check=True)

    for file, old, new in EDITS:
        path = repo / file
        text = path.read_text(encoding="utf-8").replace("\r\n", "\n")
        if text.count(old) != 1:
            raise RuntimeError(f"edit to {file} didn't match exactly once:\n{old}")
        path.write_text(text.replace(old, new), encoding="utf-8")

    ckpts = repo / "pretrained_ckpt"
    ckpts.mkdir(exist_ok=True)
    for name in ("gimmvfi_r_arb_lpips.pt", "gimmvfi_r_arb.pt", "raft-things.pth"):
        if not (ckpts / name).exists():
            print("downloading", name)
            urllib.request.urlretrieve(WEIGHTS + name, ckpts / name)

    return repo


def splat(ten_in, ten_flow):
    """forward bilinear splatting, summed. the same as the cupy kernel softsplat_out"""
    b, c, h, w = ten_in.shape
    ys = torch.arange(h, device=ten_in.device, dtype=ten_in.dtype).view(1, h, 1).expand(b, h, w)
    xs = torch.arange(w, device=ten_in.device, dtype=ten_in.dtype).view(1, 1, w).expand(b, h, w)
    fx, fy = xs + ten_flow[:, 0], ys + ten_flow[:, 1]
    x0, y0 = torch.floor(fx), torch.floor(fy)
    out = torch.zeros(b, c, h * w, device=ten_in.device, dtype=ten_in.dtype)
    vals = ten_in.reshape(b, c, h * w)
    for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1)):
        cx, cy = x0 + dx, y0 + dy
        weight = (1 - (fx - cx).abs()) * (1 - (fy - cy).abs())
        weight = weight * ((cx >= 0) & (cx < w) & (cy >= 0) & (cy < h)).to(weight.dtype)
        index = (cy.clamp(0, h - 1) * w + cx.clamp(0, w - 1)).long().reshape(b, 1, h * w).expand(b, c, h * w)
        out = out.scatter_add(2, index, vals * weight.reshape(b, 1, h * w))
    return out.reshape(b, c, h, w)


def softsplat(tenIn, tenFlow, tenMetric, strMode, return_norm=False):
    """drop-in for modules.softsplat.softsplat in the 'linear-zeroeps' mode gimm-vfi-r uses"""
    assert strMode == "linear-zeroeps" and not return_norm
    out = splat(torch.cat([tenIn * tenMetric, tenMetric], 1), tenFlow)
    norm = out[:, -1:]
    return out[:, :-1] / torch.where(norm == 0, torch.ones_like(norm), norm)


def sample(img, coords, mode="bilinear", mask=False):
    """drop-in for raft's bilinear_sampler (grid_sample with zeros padding, in pixel coordinates), from gathers"""
    assert not mask
    n, c, h, w = img.shape
    x, y = coords[..., 0], coords[..., 1]
    x0, y0 = torch.floor(x), torch.floor(y)
    flat = img.reshape(n, c, h * w)
    out = 0
    for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1)):
        cx, cy = x0 + dx, y0 + dy
        weight = (1 - (x - cx).abs()) * (1 - (y - cy).abs())
        inside = ((cx >= 0) & (cx <= w - 1) & (cy >= 0) & (cy <= h - 1)).to(img.dtype)
        index = (cy.clamp(0, h - 1) * w + cx.clamp(0, w - 1)).long().reshape(n, 1, -1).expand(n, c, -1)
        out = out + torch.gather(flat, 2, index).reshape(n, c, *coords.shape[1:3]) * (weight * inside).unsqueeze(1)
    return out


def warp(tenInput, tenFlow):
    """drop-in for fi_utils.warp (backward warp with border padding), from gathers"""
    n, c, h, w = tenInput.shape
    ys = torch.arange(h, device=tenInput.device, dtype=tenInput.dtype).view(1, h, 1)
    xs = torch.arange(w, device=tenInput.device, dtype=tenInput.dtype).view(1, 1, w)
    # border padding is the same as clamping where to sample
    x = (xs + tenFlow[:, 0]).clamp(0, w - 1)
    y = (ys + tenFlow[:, 1]).clamp(0, h - 1)
    x0, y0 = torch.floor(x), torch.floor(y)
    flat = tenInput.reshape(n, c, h * w)
    out = 0
    for dx, dy in ((0, 0), (1, 0), (0, 1), (1, 1)):
        cx, cy = (x0 + dx).clamp(max=w - 1), (y0 + dy).clamp(max=h - 1)
        weight = (1 - (x - (x0 + dx)).abs()).clamp(min=0) * (1 - (y - (y0 + dy)).abs()).clamp(min=0)
        index = (cy * w + cx).long().reshape(n, 1, h * w).expand(n, c, h * w)
        out = out + torch.gather(flat, 2, index).reshape(n, c, h, w) * weight.unsqueeze(1)
    return out


def load(repo: Path, checkpoint: str):
    sys.path.insert(0, str(repo / "src"))

    # the cupy kernels are replaced below, so cupy only has to exist while importing. it can't stay, torch.export
    # treats any loaded module called cupy as the real thing
    stub = "cupy" not in sys.modules
    if stub:
        sys.modules["cupy"] = types.SimpleNamespace(memoize=lambda **_: lambda f: f)
    try:
        from models import create_model
        from models.generalizable_INR.configs import GIMMVFIConfig
        from omegaconf import OmegaConf
    finally:
        if stub:
            del sys.modules["cupy"]

    def module(name):
        # the package exports functions with the same names as some modules, so go through sys.modules
        importlib.import_module(f"models.generalizable_INR.{name}")
        return sys.modules[f"models.generalizable_INR.{name}"]

    module("gimmvfi_r").softsplat = softsplat
    module("gimmvfi_r").warp = warp
    module("modules.fi_components").warp = warp
    module("raft.corr").bilinear_sampler = sample

    config = GIMMVFIConfig.create(OmegaConf.load(repo / "configs" / "gimmvfi" / "gimmvfi_r_arb.yaml").arch)
    cwd = os.getcwd()
    # it loads raft's weights from a path relative to the repo
    os.chdir(repo)
    try:
        model, _ = create_model(config)
    finally:
        os.chdir(cwd)

    suffix = "_lpips" if checkpoint == "lpips" else ""
    state = torch.load(repo / "pretrained_ckpt" / f"gimmvfi_r_arb{suffix}.pt", map_location="cpu")["state_dict"]
    model.load_state_dict(state, strict=True)
    for p in model.parameters():
        p.requires_grad_(False)
    return model.cuda().eval()


# the decoders' convolutions are most of the cost and fine in fp16 (2.5x faster, same output). flow, warps and
# sampling coordinates stay fp32
HALF_OPS = {"Conv", "PRelu", "Relu", "LeakyRelu", "BatchNormalization", "InstanceNormalization"}

# these only move or add values, so they run in fp16 when all their inputs already are. that keeps the decoders'
# slices, concats and residual adds between convolutions from casting back and forth (8% faster, same output)
FOLLOW_OPS = {"Slice", "Split", "Concat", "Add", "DepthToSpace"}
# the inputs after these are indices or sizes
FIRST_INPUT_ONLY = {"Slice", "Split", "DepthToSpace"}


def half_layers(proto):
    """Runs HALF_OPS in fp16, and FOLLOW_OPS where their inputs are fp16 already. Outputs are cast back up for
    whatever still uses them in fp32."""
    from onnx import TensorProto, helper, numpy_helper

    graph = proto.graph
    weights = {w.name: w for w in graph.initializer if w.data_type == TensorProto.FLOAT}
    # fp32 name -> its fp16 copy
    half_of = {}
    # fp32 names that were worked out in fp16. a value only cast down to feed a convolution doesn't count, it's
    # still needed at full precision wherever else it goes
    computed_half = set()
    halved_weights = {}
    nodes = []
    for node in graph.node:
        data = [name for name in node.input[: 1 if node.op_type in FIRST_INPUT_ONLY else None] if name]
        follows = (
            node.op_type in FOLLOW_OPS
            and any(name in computed_half for name in data)
            and all(name in computed_half or name in weights for name in data)
        )
        if node.op_type not in HALF_OPS and not follows:
            nodes.append(node)
            continue

        inputs = list(node.input)
        for i, name in enumerate(inputs):
            if name not in data:
                continue
            if name in weights:
                if name not in halved_weights:
                    halved_weights[name] = numpy_helper.from_array(
                        numpy_helper.to_array(weights[name]).astype("float16"), name + "_fp16"
                    )
                inputs[i] = name + "_fp16"
            else:
                if name not in half_of:
                    half_of[name] = name + "_fp16"
                    nodes.append(helper.make_node("Cast", [name], [half_of[name]], to=TensorProto.FLOAT16))
                inputs[i] = half_of[name]

        outputs = [name + "_fp16" for name in node.output]
        half = helper.make_node(node.op_type, inputs, outputs, name=node.name)
        half.attribute.extend(node.attribute)
        nodes.append(half)
        for half_name, name in zip(outputs, node.output):
            nodes.append(helper.make_node("Cast", [half_name], [name], to=TensorProto.FLOAT))
            half_of[name] = half_name
            computed_half.add(name)

    # drop the casts back up that nothing reads
    needed = {output.name for output in graph.output}
    kept = []
    for node in reversed(nodes):
        if any(name in needed for name in node.output):
            kept.append(node)
            needed.update(node.input)
    del graph.node[:]
    graph.node.extend(reversed(kept))
    initializers = [w for w in graph.initializer if w.name in needed] + list(halved_weights.values())
    del graph.initializer[:]
    graph.initializer.extend(initializers)


class RifeInterface(torch.nn.Module):
    def __init__(self, model, iters):
        super().__init__()
        self.model, self.iters = model, iters

    def forward(self, x):
        img0, img1 = x[:, 0:3], x[:, 3:6]
        t = x[:, 6:7].mean(dim=(1, 2, 3))

        # (1, 1, h, w, 3) of (t, y, x) at the flow resolution. the model's own sampler can't take t as a tensor
        h, w = x.shape[2] // FLOW_SCALE, x.shape[3] // FLOW_SCALE
        ys = (0.5 + torch.arange(h, device=x.device, dtype=x.dtype)) / h * 2 - 1
        xs = (0.5 + torch.arange(w, device=x.device, dtype=x.dtype)) / w * 2 - 1
        grid = torch.stack(torch.meshgrid(ys, xs, indexing="ij"), -1)[None, None]
        coord = torch.cat([t.reshape(1, 1, 1, 1, 1).expand(1, 1, h, w, 1), grid], -1)

        out = self.model(
            torch.stack([img0, img1], 2), [(coord, None)], t=[t], iters=self.iters, ds_factor=1 / FLOW_SCALE
        )
        return out["imgt_pred"][0].clamp(0, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("out", type=Path)
    parser.add_argument("--work", type=Path, default=Path("gimm-vfi-export"), help="where the checkout and weights go")
    # 8 matches 20 on blur's test footage and is faster
    parser.add_argument("--iters", type=int, default=8, help="raft iterations")
    # lpips is tuned to look sharp, plain scores 0.2dB higher against real frames
    parser.add_argument("--checkpoint", choices=("plain", "lpips"), default="plain")
    parser.add_argument("--check", action="store_true", help="compare the export with torch (needs onnxruntime)")
    args = parser.parse_args()

    # torch's export progress has emoji in it, which a redirected windows console can't encode
    sys.stdout.reconfigure(encoding="utf-8")

    args.work.mkdir(parents=True, exist_ok=True)
    repo = checkout(args.work.resolve())
    model = RifeInterface(load(repo, args.checkpoint), args.iters).eval()

    x = torch.rand(1, 7, 512, 896, device="cuda")
    x[:, 6] = 0.5
    h, w = torch.export.Dim("h", min=2, max=128), torch.export.Dim("w", min=2, max=128)
    with torch.no_grad():
        torch.onnx.export(
            model,
            (x,),
            str(args.out),
            input_names=["input"],
            output_names=["output"],
            opset_version=18,
            dynamo=True,
            dynamic_shapes={"x": {2: 64 * h, 3: 64 * w}},
        )

    import onnx

    proto = onnx.load(str(args.out))
    Path(str(args.out) + ".data").unlink(missing_ok=True)

    if args.check:
        import numpy as np
        import onnxruntime as ort

        # before the fp16 conversion, which onnxruntime's cpu kernels mostly don't cover
        session = ort.InferenceSession(proto.SerializeToString(), providers=["CPUExecutionProvider"])
        for size in ((256, 448), (384, 704)):
            # smooth inputs, random noise makes tiny warp differences look huge
            base = F.interpolate(
                torch.rand(1, 3, 16, 28, device="cuda"), size=size, mode="bicubic", align_corners=False
            )
            moved = torch.roll(base, shifts=(3, 7), dims=(2, 3))
            x = torch.cat([base, moved, torch.full_like(base[:, :1], 0.4)], 1).clamp(0, 1)
            with torch.no_grad():
                ref = model(x).cpu().numpy()
            out = session.run(None, {"input": x.cpu().numpy()})[0]
            print(f"check {size[0]}x{size[1]}: mean difference {np.abs(out - ref).mean():.5f}")

    half_layers(proto)
    # the exporter put the weights in a separate .data file, one file is simpler to ship
    onnx.save(proto, str(args.out))
    print("wrote", args.out)


if __name__ == "__main__":
    main()
