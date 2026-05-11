__version__ = "3.22.38"

__all__ = [
    "Backend", "BackendV2",
    "Waifu2x", "Waifu2xModel",
    "DPIR", "DPIRModel",
    "RealESRGAN", "RealESRGANModel",
    "RealESRGANv2", "RealESRGANv2Model",
    "CUGAN",
    "RIFE", "RIFEModel", "RIFEMerge",
    "SAFA", "SAFAModel", "SAFAAdaptiveMode",
    "SCUNet", "SCUNetModel",
    "SwinIR", "SwinIRModel",
    "ArtCNN", "ArtCNNModel",
    "inference",
    "flexible_inference"
]

import copy
from dataclasses import dataclass, field
import enum
from fractions import Fraction
import math
import os
import os.path
import platform
import subprocess
import sys
import tempfile
import time
import typing
import warnings
import zlib

import vapoursynth as vs
from vapoursynth import core


def get_plugins_path() -> str:
    path = b""

    try:
        path = core.ov.Version()["path"]
    except AttributeError:
        pass

    if path == b"":
        try:
            path = core.ort.Version()["path"]
        except AttributeError:
            pass

    if path == b"":
        try:
            path = core.ncnn.Version()["path"]
        except AttributeError:
            pass

    if path == b"":
        try:
            path = core.trt.Version()["path"]
        except AttributeError:
            pass

    if path == b"":
        try:
            path = core.trt_rtx.Version()["path"]
        except AttributeError:
            pass

    if path == b"":
        try:
            path = core.migx.Version()["path"]
        except AttributeError:
            pass

    if path == b"":
        raise RuntimeError("vsmlrt: cannot load any filters")

    return os.path.dirname(path).decode()


plugins_path: str = get_plugins_path()
trtexec_path: str = os.path.join(plugins_path, "vsmlrt-cuda", "trtexec")
migraphx_driver_path: str = os.path.join(plugins_path, "vsmlrt-hip", "migraphx-driver")
tensorrt_rtx_path: str = os.path.join(plugins_path, "vsmlrt-cuda", "tensorrt_rtx")
models_path: str = os.path.join(plugins_path, "models")


class Backend:
    @dataclass(frozen=False)
    class ORT_CPU:
        """ backend for cpus """

        num_streams: int = 1
        verbosity: int = 2
        fp16: bool = False
        fp16_blacklist_ops: typing.Optional[typing.Sequence[str]] = None
        output_format: int = 0 # 0: fp32, 1: fp16

        # internal backend attributes
        supports_onnx_serialization: bool = True

    @dataclass(frozen=False)
    class ORT_CUDA:
        """ backend for nvidia gpus

        basic performance tuning:
        set fp16 = True (on RTX GPUs)

        Semantics of `fp16`:
            Enabling `fp16` will use a built-in quantization that converts a fp32 onnx to a fp16 onnx.
            If the input video is of half-precision floating-point format,
            the generated fp16 onnx will use fp16 input.
            The output format can be controlled by the `output_format` option (0 = fp32, 1 = fp16).

            Disabling `fp16` will not use the built-in quantization.
            However, if the onnx file itself uses fp16 for computation,
            the actual computation will be done in fp16.
            In this case, the input video format should match the input format of the onnx,
            and the output format is inferred from the onnx.
        """

        device_id: int = 0
        cudnn_benchmark: bool = True
        num_streams: int = 1
        verbosity: int = 2
        fp16: bool = False
        use_cuda_graph: bool = False # preview, not supported by all models
        fp16_blacklist_ops: typing.Optional[typing.Sequence[str]] = None
        prefer_nhwc: bool = False
        output_format: int = 0 # 0: fp32, 1: fp16
        tf32: bool = False

        # internal backend attributes
        supports_onnx_serialization: bool = True

    @dataclass(frozen=False)
    class OV_CPU:
        """ backend for x86 cpus

        basic performance tuning:
        set bf16 = True (on Zen4)
        increase num_streams
        """

        fp16: bool = False
        num_streams: typing.Union[int, str] = 1
        bind_thread: bool = True
        fp16_blacklist_ops: typing.Optional[typing.Sequence[str]] = None
        bf16: bool = False
        num_threads: int = 0

        # internal backend attributes
        supports_onnx_serialization: bool = True

    @dataclass(frozen=False)
    class TRT:
        """ backend for nvidia gpus

        basic performance tuning:
        set fp16 = True (on RTX GPUs)
        increase num_streams
        increase workspace
        set use_cuda_graph = True
        """

        max_shapes: typing.Optional[typing.Tuple[int, int]] = None
        opt_shapes: typing.Optional[typing.Tuple[int, int]] = None
        fp16: bool = False
        device_id: int = 0
        workspace: typing.Optional[int] = None
        verbose: bool = False
        use_cuda_graph: bool = False
        num_streams: int = 1
        use_cublas: bool = False # cuBLAS + cuBLASLt
        static_shape: bool = True
        tf32: bool = False
        log: bool = True

        # as of TensorRT 8.4, it can be turned off without performance penalty in most cases
        use_cudnn: bool = False # changed to False since vsmlrt.vpy 3.16
        use_edge_mask_convolutions: bool = True
        use_jit_convolutions: bool = True
        heuristic: bool = False # only supported on Ampere+ with TensorRT 8.5+
        output_format: int = 0 # 0: fp32, 1: fp16
        min_shapes: typing.Tuple[int, int] = (0, 0)
        faster_dynamic_shapes: bool = True
        force_fp16: bool = False
        builder_optimization_level: int = 3
        max_aux_streams: typing.Optional[int] = None
        short_path: typing.Optional[bool] = None # True on Windows by default, False otherwise
        bf16: bool = False
        custom_env: typing.Dict[str, str] = field(default_factory=lambda: {})
        custom_args: typing.List[str] = field(default_factory=lambda: [])
        engine_folder: typing.Optional[str] = None
        max_tactics: typing.Optional[int] = None
        tiling_optimization_level: int = 0
        l2_limit_for_tiling: int = -1

        # internal backend attributes
        supports_onnx_serialization: bool = False

    @dataclass(frozen=False)
    class OV_GPU:
        """ backend for nvidia gpus

        basic performance tuning:
        set fp16 = True
        increase num_streams
        """

        fp16: bool = False
        num_streams: typing.Union[int, str] = 1
        device_id: int = 0
        fp16_blacklist_ops: typing.Optional[typing.Sequence[str]] = None

        # internal backend attributes
        supports_onnx_serialization: bool = True

    @dataclass(frozen=False)
    class NCNN_VK:
        """ backend for vulkan devices

        basic performance tuning:
        set fp16 = True (on modern GPUs)
        increase num_streams
        """

        fp16: bool = False
        device_id: int = 0
        num_streams: int = 1
        output_format: int = 0 # 0: fp32, 1: fp16

        # internal backend attributes
        supports_onnx_serialization: bool = True

    @dataclass(frozen=False)
    class ORT_DML:
        """ backend for directml (d3d12) devices """

        device_id: int = 0
        num_streams: int = 1
        verbosity: int = 2
        fp16: bool = False
        fp16_blacklist_ops: typing.Optional[typing.Sequence[str]] = None
        output_format: int = 0 # 0: fp32, 1: fp16

        # internal backend attributes
        supports_onnx_serialization: bool = True

    @dataclass(frozen=False)
    class MIGX:
        """ backend for amd gpus

        basic performance tuning:
        set fp16 = True
        """

        device_id: int = 0
        fp16: bool = False
        opt_shapes: typing.Optional[typing.Tuple[int, int]] = None
        fast_math: bool = True
        exhaustive_tune: bool = False
        num_streams: int = 1
        short_path: typing.Optional[bool] = None # True on Windows by default, False otherwise
        custom_env: typing.Dict[str, str] = field(default_factory=lambda: {})
        custom_args: typing.List[str] = field(default_factory=lambda: [])
        bf16: bool = False

        # internal backend attributes
        supports_onnx_serialization: bool = False

    @dataclass(frozen=False)
    class OV_NPU:
        """ backend for intel npus
        """

        # internal backend attributes
        supports_onnx_serialization: bool = True

    @dataclass(frozen=False)
    class ORT_COREML:
        """ backend for coreml """
        num_streams: int = 1
        verbosity: int = 0
        fp16: bool = False
        fp16_blacklist_ops: typing.Optional[typing.Sequence[str]] = None
        ml_program: int = 0
        output_format: int = 0 # 0: fp32, 1: fp16

        # internal backend attributes
        supports_onnx_serialization: bool = True

    @dataclass(frozen=False)
    class TRT_RTX:
        """ backend for nvidia rtx gpus

        basic performance tuning:
        set fp16 = True
        increase num_streams
        increase workspace
        set use_cuda_graph = True
        """

        fp16: bool = False
        device_id: int = 0
        workspace: typing.Optional[int] = None
        verbose: bool = False
        use_cuda_graph: bool = False
        num_streams: int = 1

        static_shape: bool = True
        min_shapes: typing.Tuple[int, int] = (0, 0)
        opt_shapes: typing.Optional[typing.Tuple[int, int]] = None
        max_shapes: typing.Optional[typing.Tuple[int, int]] = None

        use_cudnn: bool = False
        use_edge_mask_convolutions: bool = True
        # use_jit_convolutions: bool = True
        # output_format: int = 0 # 0: fp32, 1: fp16
        builder_optimization_level: int = 3
        max_aux_streams: typing.Optional[int] = None
        short_path: typing.Optional[bool] = None # True on Windows by default, False otherwise
        custom_env: typing.Dict[str, str] = field(default_factory=lambda: {})
        custom_args: typing.List[str] = field(default_factory=lambda: [])
        engine_folder: typing.Optional[str] = None
        max_tactics: typing.Optional[int] = None
        tiling_optimization_level: int = 0
        l2_limit_for_tiling: int = -1

        # internal backend attributes
        supports_onnx_serialization: bool = False


backendT = typing.Union[
    Backend.OV_CPU,
    Backend.ORT_CPU,
    Backend.ORT_CUDA,
    Backend.TRT,
    Backend.OV_GPU,
    Backend.NCNN_VK,
    Backend.ORT_DML,
    Backend.MIGX,
    Backend.OV_NPU,
    Backend.ORT_COREML,
    Backend.TRT_RTX,
]


fallback_backend: typing.Optional[backendT] = None


@enum.unique
class Waifu2xModel(enum.IntEnum):
    anime_style_art = 0
    anime_style_art_rgb = 1
    photo = 2
    upconv_7_anime_style_art_rgb = 3
    upconv_7_photo = 4
    upresnet10 = 5
    cunet = 6
    swin_unet_art = 7
    swin_unet_photo = 8 # 20230329
    swin_unet_photo_v2 = 9 # 20230407
    swin_unet_art_scan = 10 # 20230504


def Waifu2x(
    clip: vs.VideoNode,
    noise: typing.Literal[-1, 0, 1, 2, 3] = -1,
    scale: typing.Literal[1, 2, 4] = 2,
    tiles: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    tilesize: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    overlap: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    model: Waifu2xModel = Waifu2xModel.cunet,
    backend: backendT = Backend.OV_CPU(),
    preprocess: bool = True
) -> vs.VideoNode:

    func_name = "vsmlrt.Waifu2x"

    if not isinstance(clip, vs.VideoNode):
        raise TypeError(f'{func_name}: "clip" must be a clip!')

    if clip.format.sample_type != vs.FLOAT or clip.format.bits_per_sample not in [16, 32]:
        raise ValueError(f"{func_name}: only constant format 16/32 bit float input supported")

    if not isinstance(noise, int) or noise not in range(-1, 4):
        raise ValueError(f'{func_name}: "noise" must be -1, 0, 1, 2, or 3')

    if not isinstance(scale, int) or scale not in (1, 2, 4):
        raise ValueError(f'{func_name}: "scale" must be 1, 2 or 4')

    if not isinstance(model, int) or model not in Waifu2xModel.__members__.values():
        raise ValueError(f'{func_name}: invalid "model"')

    if model == 0 and noise == 0:
        raise ValueError(
            f'{func_name}: "anime_style_art" model'
            ' does not support noise reduction level 0'
        )

    if model in range(7) and scale not in (1, 2):
        raise ValueError(f'{func_name}: "scale" must be 1 or 2')

    if model == 0:
        if clip.format.color_family != vs.GRAY:
            raise ValueError(f'{func_name}: "clip" must be of GRAY color family')
    elif clip.format.color_family != vs.RGB:
        raise ValueError(f'{func_name}: "clip" must be of RGB color family')

    if overlap is None:
        overlap_w = overlap_h = [8, 8, 8, 8, 8, 4, 4, 4, 4, 4, 4][model]
    elif isinstance(overlap, int):
        overlap_w = overlap_h = overlap
    else:
        overlap_w, overlap_h = overlap

    if model == 6:
        multiple = 4
    else:
        multiple = 1

    width, height = clip.width, clip.height
    if preprocess and model in (0, 1, 2):
        # emulating cv2.resize(interpolation=cv2.INTER_CUBIC)
        clip = core.resize.Bicubic(
            clip,
            width * 2, height * 2,
            filter_param_a=0, filter_param_b=0.75
        )

    (tile_w, tile_h), (overlap_w, overlap_h) = calc_tilesize(
        tiles=tiles, tilesize=tilesize,
        width=clip.width, height=clip.height,
        multiple=multiple,
        overlap_w=overlap_w, overlap_h=overlap_h
    )

    if tile_w % multiple != 0 or tile_h % multiple != 0:
        raise ValueError(
            f'{func_name}: tile size must be divisible by {multiple} ({tile_w}, {tile_h})'
        )

    backend = init_backend(
        backend=backend,
        trt_opt_shapes=(tile_w, tile_h)
    )

    folder_path = os.path.join(
        models_path,
        "waifu2x",
        tuple(Waifu2xModel.__members__)[model]
    )

    if model in (0, 1, 2):
        if noise == -1:
            model_name = "scale2.0x_model.onnx"
        else:
            model_name = f"noise{noise}_model.onnx"
    elif model in (3, 4, 5):
        if noise == -1:
            model_name = "scale2.0x_model.onnx"
        else:
            model_name = f"noise{noise}_scale2.0x_model.onnx"
    elif model == 6:
        if scale == 1:
            scale_name = ""
        else:
            scale_name = "scale2.0x_"

        if noise == -1:
            model_name = "scale2.0x_model.onnx"
        else:
            model_name = f"noise{noise}_{scale_name}model.onnx"
    elif model == 7:
        if scale == 1:
            scale_name = ""
        elif scale == 2:
            scale_name = "scale2x"
        elif scale == 4:
            scale_name = "scale4x"

        if noise == -1:
            if scale == 1:
                raise ValueError("swin_unet model for \"noise == -1\" and \"scale == 1\" does not exist")

            model_name = f"{scale_name}.onnx"
        else:
            if scale == 1:
                model_name = f"noise{noise}.onnx"
            else:
                model_name = f"noise{noise}_{scale_name}.onnx"
    elif model in (8, 9, 10):
        scale_name = "scale4x"
        if noise == -1:
            model_name = f"{scale_name}.onnx"
        else:
            model_name = f"noise{noise}_{scale_name}.onnx"
    else:
        raise ValueError(f"{func_name}: inavlid model {model}")

    network_path = os.path.join(folder_path, model_name)

    clip = inference_with_fallback(
        clips=[clip], network_path=network_path,
        overlap=(overlap_w, overlap_h), tilesize=(tile_w, tile_h),
        backend=backend
    )

    if model in range(8) and scale == 1 and clip.width // width == 2:
        # emulating cv2.resize(interpolation=cv2.INTER_CUBIC)
        # cr: @AkarinVS

        clip = fmtc_resample(
            clip, scale=0.5,
            kernel="impulse", impulse=[-0.1875, 1.375, -0.1875],
            kovrspl=2
        )

    elif model in (8, 9, 10) and scale != 4:
        clip = core.resize.Bicubic(
            clip, clip.width * scale // 4, clip.height * scale // 4,
            filter_param_a=0, filter_param_b=0.5
        )

    return clip


@enum.unique
class DPIRModel(enum.IntEnum):
    drunet_gray = 0
    drunet_color = 1
    drunet_deblocking_grayscale = 2
    drunet_deblocking_color = 3


def DPIR(
    clip: vs.VideoNode,
    strength: typing.Optional[typing.Union[typing.SupportsFloat, vs.VideoNode]],
    tiles: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    tilesize: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    overlap: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    model: DPIRModel = DPIRModel.drunet_gray,
    backend: backendT = Backend.OV_CPU()
) -> vs.VideoNode:

    func_name = "vsmlrt.DPIR"

    if not isinstance(clip, vs.VideoNode):
        raise TypeError(f'{func_name}: "clip" must be a clip!')

    if clip.format.sample_type != vs.FLOAT or clip.format.bits_per_sample not in [16, 32]:
        raise ValueError(f"{func_name}: only constant format 16/32 bit float input supported")

    if not isinstance(model, int) or model not in DPIRModel.__members__.values():
        raise ValueError(f'{func_name}: invalid "model"')

    if model in [0, 2] and clip.format.color_family != vs.GRAY:
        raise ValueError(f'{func_name}: "clip" must be of GRAY color family')
    elif model in [1, 3] and clip.format.color_family != vs.RGB:
        raise ValueError(f'{func_name}: "clip" must be of RGB color family')

    if strength is None:
        strength = 5.0

    gray_format = vs.GRAYS if clip.format.bits_per_sample == 32 else vs.GRAYH

    if isinstance(strength, vs.VideoNode):
        strength = typing.cast(vs.VideoNode, strength)
        if strength.format.color_family != vs.GRAY:
            raise ValueError(f'{func_name}: "strength" must be of GRAY color family')
        if strength.width != clip.width or strength.height != clip.height:
            raise ValueError(f'{func_name}: "strength" must be of the same size as "clip"')
        if strength.num_frames != clip.num_frames:
            raise ValueError(f'{func_name}: "strength" must be of the same length as "clip"')

        strength = _expr(strength, "x 255 /", format=gray_format)
    else:
        try:
            strength = float(strength)
        except TypeError as e:
            raise TypeError(f'{func_name}: "strength" must be a float or a clip') from e

        strength = core.std.BlankClip(clip, format=gray_format, color=strength / 255, keep=True)

    if overlap is None:
        overlap_w = overlap_h = 16
    elif isinstance(overlap, int):
        overlap_w = overlap_h = overlap
    else:
        overlap_w, overlap_h = overlap

    multiple = 8

    (tile_w, tile_h), (overlap_w, overlap_h) = calc_tilesize(
        tiles=tiles, tilesize=tilesize,
        width=clip.width, height=clip.height,
        multiple=multiple,
        overlap_w=overlap_w, overlap_h=overlap_h
    )

    if tile_w % multiple != 0 or tile_h % multiple != 0:
        raise ValueError(
            f'{func_name}: tile size must be divisible by {multiple} ({tile_w}, {tile_h})'
        )

    backend = init_backend(
        backend=backend,
        trt_opt_shapes=(tile_w, tile_h)
    )

    if isinstance(backend, Backend.TRT) and not backend.force_fp16:
        backend.custom_args.extend([
            "--precisionConstraints=obey",
            "--layerPrecisions=Conv_123:fp32"
        ])

    network_path = os.path.join(
        models_path,
        "dpir",
        f"{tuple(DPIRModel.__members__)[model]}.onnx"
    )

    clip = inference_with_fallback(
        clips=[clip, strength], network_path=network_path,
        overlap=(overlap_w, overlap_h), tilesize=(tile_w, tile_h),
        backend=backend
    )

    return clip


@enum.unique
class RealESRGANModel(enum.IntEnum):
    # v2
    animevideo_xsx2 = 0
    animevideo_xsx4 = 1
    # v3
    animevideov3 = 2 # 4x
    # contributed: janaiV2(2x) https://github.com/the-database/mpv-upscale-2x_animejanai/releases/tag/2.0.0 maintainer: hooke007
    animejanaiV2L1 = 5005
    animejanaiV2L2 = 5006
    animejanaiV2L3 = 5007
    # contributed: janaiV3-hd(2x) https://github.com/the-database/mpv-upscale-2x_animejanai/releases/tag/3.0.0 maintainer: hooke007
    animejanaiV3_HD_L1 = 5008
    animejanaiV3_HD_L2 = 5009
    animejanaiV3_HD_L3 = 5010
    # contributed=Ani4K-v2 https://github.com/Sirosky/Upscale-Hub/releases/tag/Ani4K-v2
    Ani4Kv2_G6i2_Compact = 7000
    Ani4Kv2_G6i2_UltraCompact = 7001

RealESRGANv2Model = RealESRGANModel


def RealESRGAN(
    clip: vs.VideoNode,
    tiles: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    tilesize: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    overlap: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    model: RealESRGANv2Model = RealESRGANv2Model.animevideo_xsx2,
    backend: backendT = Backend.OV_CPU(),
    scale: typing.Optional[float] = None
) -> vs.VideoNode:

    func_name = "vsmlrt.RealESRGAN"

    if not isinstance(clip, vs.VideoNode):
        raise TypeError(f'{func_name}: "clip" must be a clip!')

    if clip.format.sample_type != vs.FLOAT or clip.format.bits_per_sample not in [16, 32]:
        raise ValueError(f"{func_name}: only constant format 16/32 bit float input supported")

    if clip.format.color_family != vs.RGB:
        raise ValueError(f'{func_name}: "clip" must be of RGB color family')

    if not isinstance(model, int) or model not in RealESRGANv2Model.__members__.values():
        raise ValueError(f'{func_name}: invalid "model"')

    if overlap is None:
        overlap_w = overlap_h = 8
    elif isinstance(overlap, int):
        overlap_w = overlap_h = overlap
    else:
        overlap_w, overlap_h = overlap

    multiple = 1

    (tile_w, tile_h), (overlap_w, overlap_h) = calc_tilesize(
        tiles=tiles, tilesize=tilesize,
        width=clip.width, height=clip.height,
        multiple=multiple,
        overlap_w=overlap_w, overlap_h=overlap_h
    )

    if tile_w % multiple != 0 or tile_h % multiple != 0:
        raise ValueError(
            f'{func_name}: tile size must be divisible by {multiple} ({tile_w}, {tile_h})'
        )

    backend = init_backend(
        backend=backend,
        trt_opt_shapes=(tile_w, tile_h)
    )

    if model in [0, 1]:
        network_path = os.path.join(
            models_path,
            "RealESRGANv2",
            f"RealESRGANv2-{tuple(RealESRGANv2Model.__members__)[model]}.onnx".replace('_', '-')
        )
    elif model == 2:
        network_path = os.path.join(
            models_path,
            "RealESRGANv2",
            "realesr-animevideov3.onnx"
        )
    elif model in [5005, 5006, 5007, 5008, 5009, 5010, 7000, 7001]:
        network_path = os.path.join(
            models_path,
            "RealESRGANv2",
            f"{RealESRGANv2Model(model).name}.onnx".replace('_', '-')
        )

    clip_org = clip
    clip = inference_with_fallback(
        clips=[clip], network_path=network_path,
        overlap=(overlap_w, overlap_h), tilesize=(tile_w, tile_h),
        backend=backend
    )

    if scale is not None:
        scale_h = clip.width // clip_org.width
        scale_v = clip.height // clip_org.height

        assert scale_h == scale_v

        if scale != scale_h:
            rescale = scale / scale_h

            if rescale > 1:
                clip = core.resize.Lanczos(clip, int(clip_org.width * scale), int(clip_org.height * scale), filter_param_a=4)
            else:
                clip = fmtc_resample(clip, scale=rescale, kernel="lanczos", taps=4, fh=1/rescale, fv=1/rescale)

    return clip

RealESRGANv2 = RealESRGAN


def CUGAN(
    clip: vs.VideoNode,
    noise: typing.Literal[-1, 0, 1, 2, 3] = -1,
    scale: typing.Literal[2, 3, 4] = 2,
    tiles: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    tilesize: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    overlap: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    backend: backendT = Backend.OV_CPU(),
    alpha: float = 1.0,
    version: typing.Literal[1, 2] = 1, # 1: legacy, 2: pro
    conformance: bool = True # currently specifies dynamic range compression for cugan-pro
) -> vs.VideoNode:
    """
    denoising strength: 0 < -1 < 1 < 2 < 3

    version: (1 or 2)
        1 -> legacy,
        2 -> pro (only models for "noise" in [-1, 0, 3] and "scale" in [2, 3] are published currently)
    """

    func_name = "vsmlrt.CUGAN"

    if not isinstance(clip, vs.VideoNode):
        raise TypeError(f'{func_name}: "clip" must be a clip!')

    if clip.format.sample_type != vs.FLOAT or clip.format.bits_per_sample not in [16, 32]:
        raise ValueError(f"{func_name}: only constant format 16/32 bit float input supported")

    if not isinstance(noise, int) or noise not in range(-1, 4):
        raise ValueError(f'{func_name}: "noise" must be -1, 0, 1, 2, or 3')

    if not isinstance(scale, int) or scale not in (2, 3, 4):
        raise ValueError(f'{func_name}: "scale" must be 2, 3 or 4')

    if scale != 2 and noise in [1, 2]:
        raise ValueError(
            f'{func_name}: "scale={scale}" model'
            f' does not support noise reduction level {noise}'
        )

    if clip.format.color_family != vs.RGB:
        raise ValueError(f'{func_name}: "clip" must be of RGB color family')

    if overlap is None:
        overlap_w = overlap_h = 4
    elif isinstance(overlap, int):
        overlap_w = overlap_h = overlap
    else:
        overlap_w, overlap_h = overlap

    multiple = 2

    (tile_w, tile_h), (overlap_w, overlap_h) = calc_tilesize(
        tiles=tiles, tilesize=tilesize,
        width=clip.width, height=clip.height,
        multiple=multiple,
        overlap_w=overlap_w, overlap_h=overlap_h
    )

    if tile_w % multiple != 0 or tile_h % multiple != 0:
        raise ValueError(
            f'{func_name}: tile size must be divisible by {multiple} ({tile_w}, {tile_h})'
        )

    backend = init_backend(
        backend=backend,
        trt_opt_shapes=(tile_w, tile_h)
    )

    folder_path = os.path.join(models_path, "cugan")

    if version == 1:
        if noise == -1:
            model_name = f"up{scale}x-latest-no-denoise.onnx"
        elif noise == 0:
            model_name = f"up{scale}x-latest-conservative.onnx"
        else:
            model_name = f"up{scale}x-latest-denoise{noise}x.onnx"
    elif version == 2:
        if noise == -1:
            model_name = f"pro-no-denoise3x-up{scale}x.onnx"
        elif noise == 0:
            model_name = f"pro-conservative-up{scale}x.onnx"
        else:
            model_name = f"pro-denoise{noise}x-up{scale}x.onnx"
    else:
        raise ValueError(f'{func_name}: unknown version ({version}), must be 1 (legacy) or 2 (pro)')

    network_path = os.path.join(folder_path, model_name)

    # https://github.com/bilibili/ailab/blob/978f3be762183d7fa79525f29a43e65afb995f6b/Real-CUGAN/upcunet_v3.py#L207
    # mutates network_path
    if alpha != 1.0:
        alpha = float(alpha)

        import numpy as np
        import onnx
        from onnx import numpy_helper

        model = onnx.load(network_path)

        for idx, node in reversed(list(enumerate(model.graph.node))):
            if node.op_type == "ConvTranspose":
                break

        upstream_name = node.input[0]
        downstream_name = node.input[0] + "_mul"
        node.input[0] = downstream_name

        alpha_array = np.array(alpha, dtype=np.float32)
        alpha_tensor = numpy_helper.from_array(alpha_array)
        alpha_constant = onnx.helper.make_node(
            "Constant",
            inputs=[],
            outputs=["alpha"],
            value=alpha_tensor
        )
        model.graph.node.insert(idx, alpha_constant)

        mul_node = onnx.helper.make_node(
            "Mul",
            inputs=[upstream_name, "alpha"],
            outputs=[downstream_name]
        )
        model.graph.node.insert(idx+1, mul_node)

        if backend.supports_onnx_serialization:
            if conformance and version == 2:
                clip = _expr(clip, "x 0.7 * 0.15 +")

            clip = inference_with_fallback(
                clips=[clip], network_path=model.SerializeToString(),
                overlap=(overlap_w, overlap_h), tilesize=(tile_w, tile_h),
                backend=backend, path_is_serialization=True
            )

            if conformance and version == 2:
                clip = _expr(clip, "x 0.15 - 0.7 /")

            return clip

        network_path = f"{network_path}_alpha{alpha!r}.onnx"
        onnx.save(model, network_path)

    # https://github.com/bilibili/ailab/blob/e102bef22384c629f82552dbec3d6b5bab125639/Real-CUGAN/upcunet_v3.py#L1275-L1276
    if conformance and version == 2:
        clip = _expr(clip, "x 0.7 * 0.15 +")

    clip = inference_with_fallback(
        clips=[clip], network_path=network_path,
        overlap=(overlap_w, overlap_h), tilesize=(tile_w, tile_h),
        backend=backend
    )

    # https://github.com/bilibili/ailab/blob/e102bef22384c629f82552dbec3d6b5bab125639/Real-CUGAN/upcunet_v3.py#L269
    if conformance and version == 2:
        clip = _expr(clip, "x 0.15 - 0.7 /")

    return clip


def get_rife_input(clip: vs.VideoNode) -> typing.List[vs.VideoNode]:
    assert clip.format.sample_type == vs.FLOAT
    gray_format = vs.GRAYS if clip.format.bits_per_sample == 32 else vs.GRAYH


    if (hasattr(core, 'akarin') and
        b"width" in core.akarin.Version()["expr_features"] and
        b"height" in core.akarin.Version()["expr_features"]
    ):
        if b"fp16" in core.akarin.Version()["expr_features"]:
            empty = clip.std.BlankClip(format=gray_format, length=1)
        else:
            empty = clip.std.BlankClip(format=vs.GRAYS, length=1)

        horizontal = bits_as(core.akarin.Expr(empty, 'X 2 * width 1 - / 1 -'), clip)
        vertical = bits_as(core.akarin.Expr(empty, 'Y 2 * height 1 - / 1 -'), clip)
    else:
        empty = clip.std.BlankClip(format=vs.GRAYS, length=1)

        from functools import partial

        def meshgrid_core(n: int, f: vs.VideoFrame, horizontal: bool) -> vs.VideoFrame:
            fout = f.copy()

            is_api4 = hasattr(vs, "__api_version__") and vs.__api_version__.api_major == 4
            if is_api4:
                mem_view = fout[0]
            else:
                mem_view = fout.get_write_array(0)

            height, width = mem_view.shape

            if horizontal:
                for i in range(height):
                    for j in range(width):
                        mem_view[i, j] = 2 * j / (width - 1) - 1
            else:
                for i in range(height):
                    for j in range(width):
                        mem_view[i, j] = 2 * i / (height - 1) - 1

            return fout

        horizontal = bits_as(core.std.ModifyFrame(empty, empty, partial(meshgrid_core, horizontal=True)), clip)
        vertical = bits_as(core.std.ModifyFrame(empty, empty, partial(meshgrid_core, horizontal=False)), clip)

    horizontal = horizontal.std.Loop(clip.num_frames)
    vertical = vertical.std.Loop(clip.num_frames)

    multiplier_h = clip.std.BlankClip(format=gray_format, color=2/(clip.width-1), keep=True)

    multiplier_w = clip.std.BlankClip(format=gray_format, color=2/(clip.height-1), keep=True)

    return [horizontal, vertical, multiplier_h, multiplier_w]


@enum.unique
class RIFEModel(enum.IntEnum):
    """
    Starting from RIFE v4.12 lite, this interface does not provide forward compatiblity in enum values.
    """

    v4_0 = 40
    v4_2 = 42
    v4_3 = 43
    v4_4 = 44
    v4_5 = 45
    v4_6 = 46
    v4_7 = 47
    v4_8 = 48
    v4_9 = 49
    v4_10 = 410
    v4_11 = 411
    v4_12 = 412
    v4_12_lite = 4121
    v4_13 = 413
    v4_13_lite = 4131
    v4_14 = 414
    v4_14_lite = 4141
    v4_15 = 415
    v4_15_lite = 4151
    v4_16_lite = 4161
    v4_17 = 417
    v4_17_lite = 4171
    v4_18 = 418
    v4_19 = 419
    v4_20 = 420
    v4_21 = 421
    v4_22 = 422
    v4_22_lite = 4221
    v4_23 = 423
    v4_24 = 424
    v4_25 = 425
    v4_25_lite = 4251
    v4_25_heavy = 4252
    v4_26 = 426
    v4_26_heavy = 4262


def RIFEMerge(
    clipa: vs.VideoNode,
    clipb: vs.VideoNode,
    mask: vs.VideoNode,
    scale: float = 1.0,
    tiles: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    tilesize: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    overlap: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    model: RIFEModel = RIFEModel.v4_4,
    backend: backendT = Backend.OV_CPU(),
    ensemble: bool = False,
    _implementation: typing.Optional[typing.Literal[1, 2]] = None
) -> vs.VideoNode:
    """ temporal MaskedMerge-like interface for the RIFE model

    Its semantics is similar to core.std.MaskedMerge(clipa, clipb, mask, first_plane=True),
    except that it merges the two clips in the time domain and you specify the "mask" based
    on the time point of the resulting clip (range (0,1)) between the two clips.
    """

    func_name = "vsmlrt.RIFEMerge"

    for clip in (clipa, clipb, mask):
        if not isinstance(clip, vs.VideoNode):
            raise TypeError(f'{func_name}: clip must be a clip!')

        if clip.format.sample_type != vs.FLOAT or clip.format.bits_per_sample not in [16, 32]:
            raise ValueError(f"{func_name}: only constant format 16/32 bit float input supported")

    for clip in (clipa, clipb):
        if clip.format.color_family != vs.RGB:
            raise ValueError(f'{func_name}: "clipa" / "clipb" must be of RGB color family')

        if clip.width != mask.width or clip.height != mask.height:
            raise ValueError(f'{func_name}: video dimensions mismatch')

        if clip.num_frames != mask.num_frames:
            raise ValueError(f'{func_name}: number of frames mismatch')

    if mask.format.color_family != vs.GRAY:
        raise ValueError(f'{func_name}: "mask" must be of GRAY color family')

    if tiles is not None or tilesize is not None or overlap is not None:
        raise ValueError(f'{func_name}: tiling is not supported')

    if overlap is None:
        overlap_w = overlap_h = 0
    elif isinstance(overlap, int):
        overlap_w = overlap_h = overlap
    else:
        overlap_w, overlap_h = overlap

    model_major = int(str(int(model))[0])
    model_minor = int(str(int(model))[1:3])
    if len(str(int(model))) >= 4:
        if str(int(model))[-1] == '1':
            rife_type = "_lite"
        elif str(int(model))[-1] == '2':
            rife_type = "_heavy"
    else:
        rife_type = ""

    if (model_major, model_minor) >= (4, 26):
        tilesize_requirement = 64
    elif (model_major, model_minor, rife_type) == (4, 25, "_lite"):
        tilesize_requirement = 128
    elif (model_major, model_minor, rife_type) == (4, 25, "_heavy"):
        tilesize_requirement = 64
    elif (model_major, model_minor, rife_type) == (4, 26, "_heavy"):
        tilesize_requirement = 64
    else:
        tilesize_requirement = 32

    multiple_frac = tilesize_requirement / Fraction(scale)
    if multiple_frac.denominator != 1:
        raise ValueError(f'{func_name}: ({tilesize_requirement} / Fraction(scale)) must be an integer')
    multiple = int(multiple_frac.numerator)
    scale = float(Fraction(scale))

    if model_major == 4 and (model_minor in (21, 22, 23) or model_minor >= 25) and ensemble:
        raise ValueError(f'{func_name}: ensemble is not supported')

    version = f"v{model_major}.{model_minor}{rife_type}{'_ensemble' if ensemble else ''}"

    if (model_major, model_minor) >= (4, 7) and scale != 1.0:
        raise ValueError("not supported")

    network_path = os.path.join(
        models_path,
        "rife_v2",
        f"rife_{version}.onnx"
    )
    if _implementation == 2 and os.path.exists(network_path) and scale == 1.0:
        implementation_version = 2
        multiple = 1 # v2 implements internal padding
        clips = [clipa, clipb, mask]
    else:
        implementation_version = 1

        network_path = os.path.join(
            models_path,
            "rife",
            f"rife_{version}.onnx"
        )

        clips = [clipa, clipb, mask, *get_rife_input(clipa)]

    (tile_w, tile_h), (overlap_w, overlap_h) = calc_tilesize(
        tiles=tiles, tilesize=tilesize,
        width=clip.width, height=clip.height,
        multiple=multiple,
        overlap_w=overlap_w, overlap_h=overlap_h
    )

    if tile_w % multiple != 0 or tile_h % multiple != 0:
        raise ValueError(
            f'{func_name}: tile size must be divisible by {multiple} ({tile_w}, {tile_h})'
        )

    backend = init_backend(
        backend=backend,
        trt_opt_shapes=(tile_w, tile_h)
    )

    if implementation_version == 2:
        if isinstance(backend, Backend.TRT):
            # https://github.com/AmusementClub/vs-mlrt/issues/66#issuecomment-1791986979
            if (4, 0) <= (model_major, model_minor):
                if backend.force_fp16:
                    backend.force_fp16 = False
                    backend.fp16 = True

                backend.custom_args.extend([
                    "--precisionConstraints=obey",
                    "--layerPrecisions=" + (
                        "/Cast_2:fp32,/Cast_3:fp32,/Cast_5:fp32,/Cast_7:fp32,"
                        "/Reciprocal:fp32,/Reciprocal_1:fp32,"
                        "/Mul:fp32,/Mul_1:fp32,/Mul_8:fp32,/Mul_10:fp32,"
                        "/Sub_5:fp32,/Sub_6:fp32,"
                        # generated by TensorRT's onnx parser
                        "ONNXTRT_Broadcast_236:fp32,ONNXTRT_Broadcast_238:fp32,"
                        "ONNXTRT_Broadcast_273:fp32,ONNXTRT_Broadcast_275:fp32,"
                        # TensorRT 9.0 or later
                        "ONNXTRT_Broadcast_*:fp32"
                    )
                ])

    if scale == 1.0:
        return inference_with_fallback(
            clips=clips, network_path=network_path,
            overlap=(overlap_w, overlap_h), tilesize=(tile_w, tile_h),
            backend=backend
        )
    elif ensemble or implementation_version != 1:
        raise ValueError(f'{func_name}: currently not supported')
    else:
        import onnx
        from onnx.numpy_helper import from_array, to_array

        onnx_model = onnx.load(network_path)

        resize_counter = 0
        for i in range(len(onnx_model.graph.node)):
            node = onnx_model.graph.node[i]
            if len(node.output) == 1 and node.op_type == "Constant" and node.output[0].startswith("onnx::Resize"):
                resize_counter += 1

                array = to_array(node.attribute[0].t).copy()
                if resize_counter % 3 == 2:
                    array[2:4] /= scale
                else:
                    array[2:4] *= scale
                onnx_model.graph.node[i].attribute[0].t.raw_data = from_array(array).raw_data

        if resize_counter != 11:
            raise ValueError("invalid rife model")

        multiplier_counter = 0
        for i in range(len(onnx_model.graph.node)):
            node = onnx_model.graph.node[i]
            if len(node.output) == 1 and node.op_type == "Constant" and node.output[0].startswith("onnx::Mul"):
                multiplier_counter += 1

                array = to_array(node.attribute[0].t).copy()
                if multiplier_counter % 2 == 1:
                    array /= scale
                else:
                    array *= scale
                onnx_model.graph.node[i].attribute[0].t.raw_data = from_array(array).raw_data

        if multiplier_counter != 7:
            raise ValueError("invalid rife model")

        if backend.supports_onnx_serialization:
            return inference_with_fallback(
                clips=clips, network_path=onnx_model.SerializeToString(),
                overlap=(overlap_w, overlap_h), tilesize=(tile_w, tile_h),
                backend=backend, path_is_serialization=True
            )
        else:
            network_path = f"{network_path}_scale{scale!r}.onnx"
            onnx.save(onnx_model, network_path)

            return inference_with_fallback(
                clips=clips, network_path=network_path,
                overlap=(overlap_w, overlap_h), tilesize=(tile_w, tile_h),
                backend=backend
            )


def RIFE(
    clip: vs.VideoNode,
    multi: typing.Union[int, Fraction] = 2,
    scale: float = 1.0,
    tiles: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    tilesize: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    overlap: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    model: RIFEModel = RIFEModel.v4_4,
    backend: backendT = Backend.OV_CPU(),
    ensemble: bool = False,
    video_player: bool = False,
    _implementation: typing.Optional[typing.Literal[1, 2]] = None
) -> vs.VideoNode:
    """ RIFE: Real-Time Intermediate Flow Estimation for Video Frame Interpolation

    multi, scale is based on vs-rife.

    For the best results, you need to perform scene detection on the input clip
    (e.g. misc.SCDetect, mv.SCDetection) before passing it to RIFE.
    Also note that the quality of result is strongly dependent on high quality
    scene detection and you might need to tweak the scene detection parameters
    and/or filter to achieve the best quality.

    Args:
        multi: Multiple of the frame counts, can be a fractions.Fraction.
            Default: 2.

        scale: Controls the process resolution for optical flow model.
            32 / fractions.Fraction(scale) must be an integer.
            scale=0.5 is recommended for 4K video.

        _implementation: (None, 1 or 2, experimental and maybe removed in the future)
            Switch between different onnx implementation.
            Implmementation will be selected based on internal heuristic if it is None.
    """

    func_name = "vsmlrt.RIFE"

    if not isinstance(clip, vs.VideoNode):
        raise TypeError(f'{func_name}: "clip" must be a clip!')

    if clip.format.sample_type != vs.FLOAT or clip.format.bits_per_sample not in [16, 32]:
        raise ValueError(f"{func_name}: only constant format 16/32 bit float input supported")

    if clip.format.color_family != vs.RGB:
        raise ValueError(f'{func_name}: "clip" must be of RGB color family')

    if not isinstance(multi, (int, Fraction)):
        raise TypeError(f'{func_name}: "multi" must be an integer or a fractions.Fraction!')

    if tiles is not None or tilesize is not None or overlap is not None:
        raise ValueError(f'{func_name}: tiling is not supported')

    gray_format = vs.GRAYS if clip.format.bits_per_sample == 32 else vs.GRAYH

    if int(multi) == multi:
        multi = int(multi)

        if multi < 2:
            raise ValueError(f'{func_name}: RIFE: multi must be at least 2')

        initial = core.std.Interleave([clip] * (multi - 1))

        terminal = clip.std.DuplicateFrames(frames=clip.num_frames - 1).std.Trim(first=1)
        terminal = core.std.Interleave([terminal] * (multi - 1))

        timepoint = core.std.Interleave([
            clip.std.BlankClip(format=gray_format, color=i/multi, length=1)
            for i in range(1, multi)
        ]).std.Loop(clip.num_frames)

        output0 = RIFEMerge(
            clipa=initial, clipb=terminal, mask=timepoint,
            scale=scale, tiles=tiles, tilesize=tilesize, overlap=overlap,
            model=model, backend=backend, ensemble=ensemble,
            _implementation=_implementation
        )

        clip = bits_as(clip, output0)
        initial = core.std.Interleave([clip] * (multi - 1))

        if hasattr(core, 'akarin') and hasattr(core.akarin, 'Select'):
            output = core.akarin.Select([output0, initial], initial, 'x._SceneChangeNext 1 0 ?')
        else:
            def handler(n: int, f: vs.VideoFrame) -> vs.VideoNode:
                if f.props.get('_SceneChangeNext'):
                    return initial
                return output0
            output = core.std.FrameEval(output0, handler, initial)

        if multi == 2:
            res = core.std.Interleave([clip, output])
        else:
            res = core.std.Interleave([
                clip,
                *(output.std.SelectEvery(cycle=multi-1, offsets=i) for i in range(multi - 1))
            ])

        if clip.fps_num != 0 and clip.fps_den != 0:
            return res.std.AssumeFPS(fpsnum = clip.fps_num * multi, fpsden = clip.fps_den)
        else:
            return res
    else:
        if clip.fps_num == 0 or clip.fps_den == 0:
            src_fps = Fraction(1)
        else:
            src_fps = clip.fps

        dst_fps = src_fps * multi
        src_frames = clip.num_frames
        dst_frames = min(int(src_frames * multi), 2 ** 31 - 1)

        duration_rel = src_fps / dst_fps
        dst_duration = duration_rel.numerator
        src_duration = duration_rel.denominator

        # https://github.com/AmusementClub/vs-mlrt/issues/59#issuecomment-1842649342
        if video_player:
            temp = core.std.BlankClip(clip, length=dst_frames, keep=True)

            def left_func(n: int) -> vs.VideoNode:
                return clip[dst_duration * n // src_duration]
            left_clip = core.std.FrameEval(temp, left_func)

            def right_func(n: int) -> vs.VideoNode:
                # no out of range access because of function filter_sc
                return clip[dst_duration * n // src_duration + 1]
            right_clip = core.std.FrameEval(temp, right_func)

            temp_gray = core.std.BlankClip(temp, format=gray_format, keep=True)
            def timepoint_func(n: int) -> vs.VideoNode:
                current_time = dst_duration * n
                left_index = current_time // src_duration
                left_time = src_duration * left_index
                tp = (current_time - left_time) / src_duration
                return temp_gray.std.BlankClip(color=tp, keep=True)
            tp_clip = core.std.FrameEval(temp_gray, timepoint_func)

            output0 = RIFEMerge(
                clipa=left_clip, clipb=right_clip, mask=tp_clip,
                scale=scale, tiles=tiles, tilesize=tilesize, overlap=overlap,
                model=model, backend=backend, ensemble=ensemble,
                _implementation=_implementation
            )

            left0 = bits_as(left_clip, output0)

            def filter_sc(n: int, f: vs.VideoFrame) -> vs.VideoNode:
                current_time = dst_duration * n
                left_index = current_time // src_duration
                if (
                    current_time % src_duration == 0 or
                    left_index + 1 >= src_frames or
                    f.props.get("_SceneChangeNext", False)
                ):
                    return left0
                else:
                    return output0

            res = core.std.FrameEval(output0, filter_sc, left0)
        else:
            if not hasattr(core, 'akarin') or \
                not hasattr(core.akarin, 'PropExpr') or \
                not hasattr(core.akarin, 'PickFrames'):
                raise RuntimeError(
                    'fractional multi requires plugin akarin '
                    '(https://github.com/AkarinVS/vapoursynth-plugin/releases)'
                    ', version v0.96g or later.')

            left_indices = []
            right_indices = []
            timepoints = []
            output_indices = []

            for i in range(dst_frames):
                current_time = dst_duration * i
                if current_time % src_duration == 0:
                    output_indices.append(current_time // src_duration)
                else:
                    left_index = current_time // src_duration
                    if left_index + 1 >= src_frames:
                        # approximate last frame with last frame of source
                        output_indices.append(src_frames - 1)
                        break
                    output_indices.append(src_frames + len(timepoints))
                    left_indices.append(left_index)
                    right_indices.append(left_index + 1)
                    left_time = src_duration * left_index
                    tp = (current_time - left_time) / src_duration
                    timepoints.append(tp)

            left_clip = core.akarin.PickFrames(clip, left_indices)
            right_clip = core.akarin.PickFrames(clip, right_indices)
            tp_clip = core.std.BlankClip(clip, format=gray_format, length=len(timepoints))
            tp_clip = tp_clip.akarin.PropExpr(lambda: dict(_tp=timepoints)).akarin.Expr('x._tp')

            output0 = RIFEMerge(
                clipa=left_clip, clipb=right_clip, mask=tp_clip,
                scale=scale, tiles=tiles, tilesize=tilesize, overlap=overlap,
                model=model, backend=backend, ensemble=ensemble,
                _implementation=_implementation
            )

            clip0 = bits_as(clip, output0)
            left0 = bits_as(left_clip, output0)
            output = core.akarin.Select([output0, left0], left0, 'x._SceneChangeNext 1 0 ?')
            res = core.akarin.PickFrames(clip0 + output, output_indices)

        if clip.fps_num != 0 and clip.fps_den != 0:
            return res.std.AssumeFPS(fpsnum = dst_fps.numerator, fpsden = dst_fps.denominator)
        else:
            return res


@enum.unique
class SAFAModel(enum.IntEnum):
    v0_1 = 1
    v0_2 = 2
    v0_3 = 3
    v0_4 = 4
    v0_5 = 5


@enum.unique
class SAFAAdaptiveMode(enum.IntEnum):
    non_adaptive = 0 # non-adaptive
    adaptive1x = 1 # use adaptive path only at 1x scale
    adaptive = 2 # use adaptive path at 1x, 1/2x and 1/4x scales, proposed algorithm


def SAFA(
    clip: vs.VideoNode,
    tiles: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    tilesize: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    overlap: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    model: SAFAModel = SAFAModel.v0_1,
    adaptive: SAFAAdaptiveMode = SAFAAdaptiveMode.non_adaptive,
    backend: backendT = Backend.OV_CPU(),
) -> vs.VideoNode:
    """ SAFA: Scale-Adaptive Feature Aggregation for Efficient Space-Time Video Super-Resolution
    """

    func_name = "vsmlrt.SAFA"

    if not isinstance(clip, vs.VideoNode):
        raise TypeError(f'{func_name}: "clip" must be a clip!')

    if clip.format.sample_type != vs.FLOAT or clip.format.bits_per_sample not in [16, 32]:
        raise ValueError(f"{func_name}: only constant format 16/32 bit float input supported")

    if clip.format.color_family != vs.RGB:
        raise ValueError(f'{func_name}: "clip" must be of RGB color family')

    if clip.num_frames == 1:
        raise ValueError(f'{func_name}: "clip" too short!')

    if overlap is None:
        overlap_w = overlap_h = 16
    elif isinstance(overlap, int):
        overlap_w = overlap_h = overlap
    else:
        overlap_w, overlap_h = overlap

    # unknown crash
    if model <= 2:
        multiple = 8
    else:
        multiple = 16

    (tile_w, tile_h), (overlap_w, overlap_h) = calc_tilesize(
        tiles=tiles, tilesize=tilesize,
        width=clip.width, height=clip.height,
        multiple=multiple,
        overlap_w=overlap_w, overlap_h=overlap_h
    )

    if tile_w % multiple != 0 or tile_h % multiple != 0:
        raise ValueError(
            f'{func_name}: tile size must be divisible by {multiple} ({tile_w}, {tile_h})'
        )

    backend = init_backend(
        backend=backend,
        trt_opt_shapes=(tile_w, tile_h)
    )

    _adaptive = SAFAAdaptiveMode(adaptive)

    if isinstance(backend, Backend.TRT):
        if backend.force_fp16:
            backend.force_fp16 = False
            backend.fp16 = True

        cast1, cast2 = [(218, 255), (266, 303), (254, 291)][_adaptive]

        backend.custom_args.extend([
            "--precisionConstraints=obey",
            "--layerPrecisions=" + (
                "/Div_2:fp32,/Div_3:fp32,/Div_4:fp32,/Div_5:fp32,/Div_6:fp32,/Div_7:fp32,"
                "/Cast_7:fp32,/Cast_8:fp32,/Cast_10:fp32,/Cast_11:fp32,"
                f"Cast_{cast1}:fp32,Cast_{cast2}:fp32,"
                "/Sub_3:fp32,/Sub_4:fp32"
            )
        ])

    model_version = SAFAModel(model).name.replace('_', '.')
    adaptive_string = _adaptive.name

    network_path = os.path.join(
        models_path,
        "safa",
        f"safa_{model_version}_{adaptive_string}.onnx"
    )

    clip_org = clip

    clips = [clip[::2], clip[1::2]]
    if clips[0].num_frames != clips[1].num_frames:
        clips[1] = core.std.Splice([clips[1], clip[-1]])

    clip2x = inference_with_fallback(
        clips=clips, network_path=network_path,
        overlap=(overlap_w, overlap_h), tilesize=(tile_w, tile_h),
        backend=backend
    )

    up = core.std.Crop(clip2x, bottom=clip2x.height // 2)
    down = core.std.Crop(clip2x, top=clip2x.height // 2)
    clip = core.std.Interleave([up, down])

    if clip.num_frames != clip_org.num_frames:
        clip = clip[:-1]

    return clip


@enum.unique
class SCUNetModel(enum.IntEnum):
    scunet_color_15 = 0
    scunet_color_25 = 1
    scunet_color_50 = 2
    scunet_color_real_psnr = 3
    scunet_color_real_gan = 4
    scunet_gray_15 = 5
    scunet_gray_25 = 6
    scunet_gray_50 = 7


def SCUNet(
    clip: vs.VideoNode,
    tiles: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    tilesize: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    overlap: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    model: SCUNetModel = SCUNetModel.scunet_color_real_psnr,
    backend: backendT = Backend.OV_CPU()
) -> vs.VideoNode:
    """ Practical Blind Denoising via Swin-Conv-UNet and Data Synthesis

    Unlike vs-scunet v1.0.0, the default model is set to scunet_color_real_psnr due to the color shift.
    """

    func_name = "vsmlrt.SCUNet"

    if not isinstance(clip, vs.VideoNode):
        raise TypeError(f'{func_name}: "clip" must be a clip!')

    if clip.format.sample_type != vs.FLOAT or clip.format.bits_per_sample not in [16, 32]:
        raise ValueError(f"{func_name}: only constant format 16/32 bit float input supported")

    if not isinstance(model, int) or model not in SCUNetModel.__members__.values():
        raise ValueError(f'{func_name}: invalid "model"')

    if model in range(5) and clip.format.color_family != vs.RGB:
        raise ValueError(f'{func_name}: "clip" must be of RGB color family')
    elif model in range(5, 8) and clip.format.color_family != vs.GRAY:
        raise ValueError(f'{func_name}: "clip" must be of GRAY color family')

    if overlap is None:
        overlap_w = overlap_h = 64
    elif isinstance(overlap, int):
        overlap_w = overlap_h = overlap
    else:
        overlap_w, overlap_h = overlap

    multiple = 1

    (tile_w, tile_h), (overlap_w, overlap_h) = calc_tilesize(
        tiles=tiles, tilesize=tilesize,
        width=clip.width, height=clip.height,
        multiple=multiple,
        overlap_w=overlap_w, overlap_h=overlap_h
    )

    if tile_w % multiple != 0 or tile_h % multiple != 0:
        raise ValueError(
            f'{func_name}: tile size must be divisible by {multiple} ({tile_w}, {tile_h})'
        )

    backend = init_backend(
        backend=backend,
        trt_opt_shapes=(tile_w, tile_h)
    )

    network_path = os.path.join(
        models_path,
        "scunet",
        f"{tuple(SCUNetModel.__members__)[model]}.onnx"
    )

    clip = inference_with_fallback(
        clips=[clip], network_path=network_path,
        overlap=(overlap_w, overlap_h), tilesize=(tile_w, tile_h),
        backend=backend
    )

    return clip


@enum.unique
class SwinIRModel(enum.IntEnum):
    lightweightSR_DIV2K_s64w8_SwinIR_S_x2 = 0
    lightweightSR_DIV2K_s64w8_SwinIR_S_x3 = 1
    lightweightSR_DIV2K_s64w8_SwinIR_S_x4 = 2
    realSR_BSRGAN_DFOWMFC_s64w8_SwinIR_L_x4_GAN = 3
    # unused
    realSR_BSRGAN_DFOWMFC_s64w8_SwinIR_L_x4_PSNR = 5
    classicalSR_DF2K_s64w8_SwinIR_M_x2 = 6
    classicalSR_DF2K_s64w8_SwinIR_M_x3 = 7
    classicalSR_DF2K_s64w8_SwinIR_M_x4 = 8
    classicalSR_DF2K_s64w8_SwinIR_M_x8 = 9
    realSR_BSRGAN_DFO_s64w8_SwinIR_M_x2_GAN = 10
    realSR_BSRGAN_DFO_s64w8_SwinIR_M_x2_PSNR = 11
    realSR_BSRGAN_DFO_s64w8_SwinIR_M_x4_GAN = 12
    realSR_BSRGAN_DFO_s64w8_SwinIR_M_x4_PSNR = 13
    grayDN_DFWB_s128w8_SwinIR_M_noise15 = 14
    grayDN_DFWB_s128w8_SwinIR_M_noise25 = 15
    grayDN_DFWB_s128w8_SwinIR_M_noise50 = 16
    colorDN_DFWB_s128w8_SwinIR_M_noise15 = 17
    colorDN_DFWB_s128w8_SwinIR_M_noise25 = 18
    colorDN_DFWB_s128w8_SwinIR_M_noise50 = 19
    CAR_DFWB_s126w7_SwinIR_M_jpeg10 = 20
    CAR_DFWB_s126w7_SwinIR_M_jpeg20 = 21
    CAR_DFWB_s126w7_SwinIR_M_jpeg30 = 22
    CAR_DFWB_s126w7_SwinIR_M_jpeg40 = 23
    colorCAR_DFWB_s126w7_SwinIR_M_jpeg10 = 24
    colorCAR_DFWB_s126w7_SwinIR_M_jpeg20 = 25
    colorCAR_DFWB_s126w7_SwinIR_M_jpeg30 = 26
    colorCAR_DFWB_s126w7_SwinIR_M_jpeg40 = 27


def SwinIR(
    clip: vs.VideoNode,
    tiles: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    tilesize: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    overlap: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    model: SwinIRModel = SwinIRModel.lightweightSR_DIV2K_s64w8_SwinIR_S_x2,
    backend: backendT = Backend.OV_CPU()
) -> vs.VideoNode:
    """ SwinIR: Image Restoration Using Swin Transformer """

    func_name = "vsmlrt.SwinIR"

    if not isinstance(clip, vs.VideoNode):
        raise TypeError(f'{func_name}: "clip" must be a clip!')

    if clip.format.sample_type != vs.FLOAT or clip.format.bits_per_sample not in [16, 32]:
        raise ValueError(f"{func_name}: only constant format 16/32 bit float input supported")

    if not isinstance(model, int) or model not in SwinIRModel.__members__.values():
        raise ValueError(f'{func_name}: invalid "model"')

    if model in range(14, 17) or model in range(20, 24):
        if clip.format.color_family != vs.GRAY:
            raise ValueError(f'{func_name}: "clip" must be of GRAY color family')
    elif clip.format.color_family != vs.RGB:
        raise ValueError(f'{func_name}: "clip" must be of RGB color family')

    if overlap is None:
        overlap_w = overlap_h = 16
    elif isinstance(overlap, int):
        overlap_w = overlap_h = overlap
    else:
        overlap_w, overlap_h = overlap

    multiple = 1

    (tile_w, tile_h), (overlap_w, overlap_h) = calc_tilesize(
        tiles=tiles, tilesize=tilesize,
        width=clip.width, height=clip.height,
        multiple=multiple,
        overlap_w=overlap_w, overlap_h=overlap_h
    )

    if tile_w % multiple != 0 or tile_h % multiple != 0:
        raise ValueError(
            f'{func_name}: tile size must be divisible by {multiple} ({tile_w}, {tile_h})'
        )

    backend = init_backend(
        backend=backend,
        trt_opt_shapes=(tile_w, tile_h)
    )

    if model < 4:
        model_name = tuple(SwinIRModel.__members__)[model]
    else:
        model_name = tuple(SwinIRModel.__members__)[model - 1]

    model_name = model_name.replace("SwinIR_", "SwinIR-")

    if model in range(3):
        model_name = f"002_{model_name}"
    elif model in (3, 5):
        model_name = f"003_{model_name}"
    elif model in range(6, 10):
        model_name = f"001_{model_name}"
    elif model in range(10, 14):
        model_name = f"003_{model_name}"
    elif model in range(14, 17):
        model_name = f"004_{model_name}"
    elif model in range(17, 20):
        model_name = f"005_{model_name}"
    elif model in range(20, 28):
        model_name = f"006_{model_name}"

    network_path = os.path.join(
        models_path,
        "swinir",
        f"{model_name}.onnx"
    )

    clip = inference_with_fallback(
        clips=[clip], network_path=network_path,
        overlap=(overlap_w, overlap_h), tilesize=(tile_w, tile_h),
        backend=backend
    )

    return clip


@enum.unique
class ArtCNNModel(enum.IntEnum):
    ArtCNN_C4F32 = 0
    ArtCNN_C4F32_DS = 1
    ArtCNN_C16F64 = 2
    ArtCNN_C16F64_DS = 3
    ArtCNN_C4F32_Chroma = 4
    ArtCNN_C16F64_Chroma = 5
    ArtCNN_R16F96 = 6
    ArtCNN_R8F64 = 7
    ArtCNN_R8F64_DS = 8
    ArtCNN_R8F64_Chroma = 9
    ArtCNN_C4F16 = 10
    ArtCNN_C4F16_DS = 11
    ArtCNN_R16F96_Chroma = 12
    ArtCNN_C4F16_DN = 13
    ArtCNN_C4F32_DN = 14
    ArtCNN_R8F64_JPEG420 = 15
    ArtCNN_R8F64_JPEG444 = 16
    ArtCNN_R8F64_Chroma_DN = 17


def ArtCNN(
    clip: vs.VideoNode,
    tiles: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    tilesize: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    overlap: typing.Optional[typing.Union[int, typing.Tuple[int, int]]] = None,
    model: ArtCNNModel = ArtCNNModel.ArtCNN_C16F64,
    backend: backendT = Backend.OV_CPU()
) -> vs.VideoNode:
    """ ArtCNN (https://github.com/Artoriuz/ArtCNN) """

    func_name = "vsmlrt.ArtCNN"

    if not isinstance(clip, vs.VideoNode):
        raise TypeError(f'{func_name}: "clip" must be a clip!')

    if clip.format.sample_type != vs.FLOAT or clip.format.bits_per_sample not in [16, 32]:
        raise ValueError(f"{func_name}: only constant format 16/32 bit float input supported")

    if not isinstance(model, int) or model not in ArtCNNModel.__members__.values():
        raise ValueError(f'{func_name}: invalid "model"')

    if model in (
        ArtCNNModel.ArtCNN_C4F32_Chroma,
        ArtCNNModel.ArtCNN_C16F64_Chroma,
        ArtCNNModel.ArtCNN_R8F64_Chroma,
        ArtCNNModel.ArtCNN_R16F96_Chroma,
        ArtCNNModel.ArtCNN_R8F64_Chroma_DN,
    ):
        if clip.format.color_family != vs.YUV:
            raise ValueError(f'{func_name}: "clip" must be of YUV color family')
        if clip.format.subsampling_h != 0 or clip.format.subsampling_w != 0:
            raise ValueError(
                f'{func_name}: "clip" must be without subsampling! '
                'Bilinear upsampling is recommended.'
            )
    elif model in (
        ArtCNNModel.ArtCNN_R8F64_JPEG420,
        ArtCNNModel.ArtCNN_R8F64_JPEG444,
    ):
        if clip.format.color_family != vs.RGB:
            raise ValueError(f'{func_name}: "clip" must be of RGB color family')
    elif clip.format.color_family != vs.GRAY:
        raise ValueError(f'{func_name}: "clip" must be of GRAY color family')

    if overlap is None:
        overlap_w = overlap_h = 8
    elif isinstance(overlap, int):
        overlap_w = overlap_h = overlap
    else:
        overlap_w, overlap_h = overlap

    multiple = 1

    (tile_w, tile_h), (overlap_w, overlap_h) = calc_tilesize(
        tiles=tiles, tilesize=tilesize,
        width=clip.width, height=clip.height,
        multiple=multiple,
        overlap_w=overlap_w, overlap_h=overlap_h
    )

    if tile_w % multiple != 0 or tile_h % multiple != 0:
        raise ValueError(
            f'{func_name}: tile size must be divisible by {multiple} ({tile_w}, {tile_h})'
        )

    backend = init_backend(
        backend=backend,
        trt_opt_shapes=(tile_w, tile_h)
    )

    model_name = tuple(ArtCNNModel.__members__)[model]

    network_path = os.path.join(
        models_path,
        "ArtCNN",
        f"{model_name}.onnx"
    )

    if model in (
        ArtCNNModel.ArtCNN_C4F32_Chroma,
        ArtCNNModel.ArtCNN_C16F64_Chroma,
        ArtCNNModel.ArtCNN_R8F64_Chroma,
        ArtCNNModel.ArtCNN_R16F96_Chroma,
        ArtCNNModel.ArtCNN_R8F64_Chroma_DN,
    ):
        clip = _expr(clip, ["", "x 0.5 +"])

        clip_u, clip_v = flexible_inference_with_fallback(
            clips=[clip], network_path=network_path,
            overlap=(overlap_w, overlap_h), tilesize=(tile_w, tile_h),
            backend=backend
        )

        clip = core.std.ShufflePlanes([clip, clip_u, clip_v], [0, 0, 0], vs.YUV)

        clip = _expr(clip, ["", "x 0.5 -"])
    else:
        clip = inference_with_fallback(
            clips=[clip], network_path=network_path,
            overlap=(overlap_w, overlap_h), tilesize=(tile_w, tile_h),
            backend=backend
        )

    return clip


def get_engine_path(
    network_path: str,
    min_shapes: typing.Tuple[int, int],
    opt_shapes: typing.Tuple[int, int],
    max_shapes: typing.Tuple[int, int],
    workspace: typing.Optional[int],
    fp16: bool,
    use_cublas: bool,
    static_shape: bool,
    tf32: bool,
    use_cudnn: bool,
    input_format: int,
    output_format: int,
    builder_optimization_level: int,
    max_aux_streams: typing.Optional[int],
    short_path: typing.Optional[bool],
    bf16: bool,
    engine_folder: typing.Optional[str],
    is_rtx: bool = False,
    trt_version: typing.Tuple[int, int, int] = (0, 0, 0),
    device_name: str = "",
) -> str:

    with open(network_path, "rb") as file:
        checksum = zlib.adler32(file.read())

    if static_shape:
        shape_str = f"{opt_shapes[0]}x{opt_shapes[1]}"
    else:
        shape_str = (
            f"min{min_shapes[0]}x{min_shapes[1]}"
            f"_opt{opt_shapes[0]}x{opt_shapes[1]}"
            f"_max{max_shapes[0]}x{max_shapes[1]}"
        )

    identity = (
        shape_str +
        ("_fp16" if fp16 else "") +
        ("_tf32" if tf32 else "") +
        ("_bf16" if bf16 else "") +
        (f"_workspace{workspace}" if workspace is not None else "") +
        f"_opt{builder_optimization_level}" +
        (f"_max-aux-streams{max_aux_streams}" if max_aux_streams is not None else "") +
        "_trt-" + '.'.join(map(str, trt_version)) +
        ("_cublas" if use_cublas else "") +
        ("_cudnn" if use_cudnn else "") +
        "_I-" + ("fp32" if input_format == 0 else "fp16") +
        "_O-" + ("fp32" if output_format == 0 else "fp16") +
        f"_{device_name}" +
        ("_rtx" if is_rtx else "") +
        f"_{checksum:x}"
    )

    dirname, basename = os.path.split(network_path)

    if engine_folder is not None:
        os.makedirs(engine_folder, exist_ok=True)
        dirname = engine_folder

    use_short_path = False

    if short_path:
        use_short_path = True
    elif platform.system() == "Windows":
        # use short path by default
        if short_path is None:
            use_short_path = True
        # NTFS limitation
        elif len(f"{basename}.{identity}.engine.cache.lock") >= 256:
            use_short_path = True

    if use_short_path:
        return os.path.join(dirname, f"{zlib.crc32((f'{basename}.{identity}').encode()):x}.engine")
    else:
        return f"{os.path.join(dirname, basename)}.{identity}.engine"


def trtexec(
    network_path: str,
    channels: int,
    opt_shapes: typing.Tuple[int, int],
    max_shapes: typing.Tuple[int, int],
    fp16: bool,
    device_id: int,
    workspace: typing.Optional[int] = None,
    verbose: bool = False,
    use_cuda_graph: bool = False,
    use_cublas: bool = False,
    static_shape: bool = True,
    tf32: bool = False,
    log: bool = False,
    use_cudnn: bool = False,
    use_edge_mask_convolutions: bool = True,
    use_jit_convolutions: bool = True,
    heuristic: bool = False,
    input_name: str = "input",
    input_format: int = 0,
    output_format: int = 0,
    min_shapes: typing.Tuple[int, int] = (0, 0),
    faster_dynamic_shapes: bool = True,
    force_fp16: bool = False,
    builder_optimization_level: int = 3,
    max_aux_streams: typing.Optional[int] = None,
    short_path: typing.Optional[bool] = None,
    bf16: bool = False,
    custom_env: typing.Dict[str, str] = {},
    custom_args: typing.List[str] = [],
    engine_folder: typing.Optional[str] = None,
    max_tactics: typing.Optional[int] = None,
    tiling_optimization_level: int = 0,
    l2_limit_for_tiling: int = -1,
) -> str:

    # tensort runtime version
    trt_version = parse_trt_version(int(core.trt.Version()["tensorrt_version"]))

    if isinstance(opt_shapes, int):
        opt_shapes = (opt_shapes, opt_shapes)

    if isinstance(max_shapes, int):
        max_shapes = (max_shapes, max_shapes)

    if force_fp16:
        fp16 = True
        tf32 = False
        bf16 = False

    try:
        device_name = core.trt.DeviceProperties(device_id)["name"].decode()
        device_name = device_name.replace(' ', '-')
    except AttributeError:
        device_name = f"device{device_id}"

    engine_path = get_engine_path(
        network_path=network_path,
        min_shapes=min_shapes,
        opt_shapes=opt_shapes,
        max_shapes=max_shapes,
        workspace=workspace,
        fp16=fp16,
        use_cublas=use_cublas,
        static_shape=static_shape,
        tf32=tf32,
        use_cudnn=use_cudnn,
        input_format=input_format,
        output_format=output_format,
        builder_optimization_level=builder_optimization_level,
        max_aux_streams=max_aux_streams,
        short_path=short_path,
        bf16=bf16,
        engine_folder=engine_folder,
        trt_version=trt_version,
        device_name=device_name,
    )

    if os.access(engine_path, mode=os.R_OK) and os.path.getsize(engine_path) >= 1024:
        return engine_path

    # do not consider alternative path when the engine_folder is given
    if engine_folder is None:
        alter_engine_path = os.path.join(
            tempfile.gettempdir(),
            os.path.splitdrive(engine_path)[1][1:]
        )

        if os.access(alter_engine_path, mode=os.R_OK) and os.path.getsize(alter_engine_path) >= 1024:
            return alter_engine_path

    try:
        # test writability
        with open(engine_path, "w") as f:
            pass
        os.remove(engine_path)
    except PermissionError:
        if engine_folder is None:
            print(f"{engine_path} is not writable", file=sys.stderr)
            engine_path = alter_engine_path
            dirname = os.path.dirname(engine_path)
            if not os.path.exists(dirname):
                os.makedirs(dirname)
            print(f"change engine path to {engine_path}", file=sys.stderr)
        else:
            # do not consider alternative path when the engine_folder is given
            raise PermissionError(f"{engine_path} is not writable")

    args = [
        trtexec_path,
        f"--onnx={network_path}",
        f"--timingCacheFile={engine_path}.cache",
        f"--device={device_id}",
        f"--saveEngine={engine_path}"
    ]

    if workspace is not None:
        if trt_version >= (8, 4, 0):
            args.append(f"--memPoolSize=workspace:{workspace}")
        else:
            args.append(f"--workspace{workspace}")

    if static_shape:
        args.append(f"--shapes={input_name}:1x{channels}x{opt_shapes[1]}x{opt_shapes[0]}")
    else:
        args.extend([
            f"--minShapes={input_name}:1x{channels}x{min_shapes[1]}x{min_shapes[0]}",
            f"--optShapes={input_name}:1x{channels}x{opt_shapes[1]}x{opt_shapes[0]}",
            f"--maxShapes={input_name}:1x{channels}x{max_shapes[1]}x{max_shapes[0]}"
        ])

    if fp16:
        args.append("--fp16")

    if verbose:
        args.append("--verbose")

    preview_features = []
    if (use_cublas or use_cudnn) and (8, 6, 0) <= trt_version < (10, 0, 0):
        preview_features.append("-disableExternalTacticSourcesForCore0805")

    if preview_features and trt_version >= (8, 5, 0):
        args.append(f"--preview={','.join(preview_features)}")

    tactic_sources = []

    if use_cublas:
        tactic_sources.extend(["+CUBLAS", "+CUBLAS_LT"])
    else:
        tactic_sources.extend(["-CUBLAS", "-CUBLAS_LT"])

    if use_cudnn:
        tactic_sources.append("+CUDNN")
    else:
        tactic_sources.append("-CUDNN")

    if trt_version >= (8, 4, 1):
        if use_edge_mask_convolutions:
            tactic_sources.append("+EDGE_MASK_CONVOLUTIONS")
        else:
            tactic_sources.append("-EDGE_MASK_CONVOLUTIONS")

    if trt_version >= (8, 5, 0):
        if use_jit_convolutions:
            tactic_sources.append("+JIT_CONVOLUTIONS")
        else:
            tactic_sources.append("-JIT_CONVOLUTIONS")

    args.append(f"--tacticSources={','.join(tactic_sources)}")

    if use_cuda_graph:
        args.extend((
            "--useCudaGraph",
            "--noDataTransfers"
        ))
    else:
        if trt_version >= (8, 6, 0):
            args.append("--skipInference")
        else:
            args.append("--buildOnly")

    if not tf32:
        args.append("--noTF32")

    if heuristic and trt_version >= (8, 5, 0) and core.trt.DeviceProperties(device_id)["major"] >= 8:
        if trt_version < (8, 6, 0):
            args.append("--heuristic")
        else:
            builder_optimization_level = 2

    args.extend([
        "--inputIOFormats=fp32:chw" if input_format == 0 else "--inputIOFormats=fp16:chw",
        "--outputIOFormats=fp32:chw" if output_format == 0 else "--outputIOFormats=fp16:chw"
    ])

    if faster_dynamic_shapes and not static_shape and (8, 5, 0) <= trt_version < (8, 6, 0):
        args.append("--preview=+fasterDynamicShapes0805")

    if force_fp16:
        if trt_version >= (8, 4, 1):
            args.extend([
                "--layerPrecisions=*:fp16",
                "--layerOutputTypes=*:fp16",
                "--precisionConstraints=obey"
            ])
        else:
            raise ValueError('"force_fp16" is not available')

    if trt_version >= (8, 6, 0):
        args.append(f"--builderOptimizationLevel={builder_optimization_level}")

        if max_aux_streams is not None:
            args.append(f"--maxAuxStreams={max_aux_streams}")

    if trt_version >= (9, 0, 0):
        if bf16:
            args.append("--bf16")

    if trt_version >= (10, 4, 0):
        if max_tactics is not None:
            args.append(f"--maxTactics={max_tactics}")

    if trt_version >= (10, 8, 0) and tiling_optimization_level != 0:
        args.append(f"--tilingOptimizationLevel={tiling_optimization_level}")
        args.append(f"--l2LimitForTiling={l2_limit_for_tiling}")

    args.extend(custom_args)

    if log:
        env_key = "TRTEXEC_LOG_FILE"
        prev_env_value = os.environ.get(env_key)

        if prev_env_value is not None and len(prev_env_value) > 0:
            # env_key has been set, no extra action
            env = {env_key: prev_env_value, "CUDA_MODULE_LOADING": "LAZY"}
            env.update(**custom_env)
            subprocess.run(args, env=env, check=True, stdout=sys.stderr)
        else:
            time_str = time.strftime('%y%m%d_%H%M%S', time.localtime())

            log_filename = os.path.join(
                tempfile.gettempdir(),
                f"trtexec_{time_str}.log"
            )

            env = {env_key: log_filename, "CUDA_MODULE_LOADING": "LAZY"}
            env.update(**custom_env)

            completed_process = subprocess.run(args, env=env, check=False, stdout=sys.stderr)

            if completed_process.returncode == 0:
                try:
                    os.remove(log_filename)
                except FileNotFoundError:
                    # maybe the official trtexec is used?
                    pass
            else:
                if os.path.exists(log_filename):
                    raise RuntimeError(f"trtexec execution fails, log has been written to {log_filename}")
                else:
                    raise RuntimeError(f"trtexec execution fails but no log is found")
    else:
        env = {"CUDA_MODULE_LOADING": "LAZY"}
        env.update(**custom_env)
        subprocess.run(args, env=env, check=True, stdout=sys.stderr)

    return engine_path


def get_mxr_path(
    network_path: str,
    opt_shapes: typing.Tuple[int, int],
    fp16: bool,
    fast_math: bool,
    exhaustive_tune: bool,
    device_id: int,
    short_path: typing.Optional[bool],
    bf16: bool,
) -> str:

    with open(network_path, "rb") as file:
        checksum = zlib.adler32(file.read())

    migx_version = core.migx.Version()["migraphx_version_build"].decode()

    try:
        device_name = core.migx.DeviceProperties(device_id)["name"].decode()
        device_name = device_name.replace(' ', '-')
    except AttributeError:
        device_name = f"device{device_id}"

    shape_str = f"{opt_shapes[0]}x{opt_shapes[1]}"

    identity = (
        shape_str +
        ("_fp16" if fp16 else "") +
        ("_bf16" if bf16 else "") +
        ("_fast" if fast_math else "") +
        ("_exhaustive" if exhaustive_tune else "") +
        f"_migx-{migx_version}" +
        f"_{device_name}" +
        f"_{checksum:x}"
    )

    if short_path or (short_path is None and platform.system() == "Windows"):
        dirname, basename = os.path.split(network_path)
        return os.path.join(dirname, f"{zlib.crc32((basename + identity).encode()):x}.mxr")
    else:
        return f"{network_path}.{identity}.mxr"


def migraphx_driver(
    network_path: str,
    channels: int,
    opt_shapes: typing.Tuple[int, int],
    fp16: bool,
    fast_math: bool,
    exhaustive_tune: bool,
    device_id: int,
    input_name: str = "input",
    short_path: typing.Optional[bool] = None,
    custom_env: typing.Dict[str, str] = {},
    custom_args: typing.List[str] = [],
    bf16: bool = False,
) -> str:

    if isinstance(opt_shapes, int):
        opt_shapes = (opt_shapes, opt_shapes)

    mxr_path = get_mxr_path(
        network_path=network_path,
        opt_shapes=opt_shapes,
        fp16=fp16,
        fast_math=fast_math,
        exhaustive_tune=exhaustive_tune,
        device_id=device_id,
        short_path=short_path,
        bf16=bf16,
    )

    if os.access(mxr_path, mode=os.R_OK) and os.path.getsize(mxr_path) >= 1024:
        return mxr_path

    alter_mxr_path = os.path.join(
        tempfile.gettempdir(),
        os.path.splitdrive(mxr_path)[1][1:]
    )

    if os.access(alter_mxr_path, mode=os.R_OK) and os.path.getsize(mxr_path) >= 1024:
        return alter_mxr_path

    try:
        # test writability
        with open(mxr_path, "w") as f:
            pass
        os.remove(mxr_path)
    except PermissionError:
        print(f"{mxr_path} not writable", file=sys.stderr)
        mxr_path = alter_mxr_path
        dirname = os.path.dirname(mxr_path)
        if not os.path.exists(dirname):
            os.makedirs(dirname)
        print(f"change mxr path to {mxr_path}", file=sys.stderr)

    if device_id != 0:
        raise ValueError('"device_id" must be 0')

    args = [
        migraphx_driver_path,
        "compile",
        "--onnx", f"{network_path}",
        "--gpu",
        # f"--device={device_id}",
        "--optimize",
        "--binary",
        "--output", f"{mxr_path}"
    ]

    args.extend(["--input-dim", f"@{input_name}", "1", f"{channels}", f"{opt_shapes[1]}", f"{opt_shapes[0]}"])

    if fp16:
        args.append("--fp16")

    if not fast_math:
        args.append("--disable-fast-math")

    if exhaustive_tune:
        args.append("--exhaustive-tune")

    # bf16 support is introduced in ROCm 7.2.0, but the version info from the vsmigx library may not be reliable.
    if bf16:
        args.append("--bf16")

    args.extend(custom_args)

    subprocess.run(args, env=custom_env, check=True, stdout=sys.stderr)

    return mxr_path


def tensorrt_rtx(
    network_path: str,
    channels: int,
    fp16: bool,
    device_id: int,
    opt_shapes: typing.Tuple[int, int],
    max_shapes: typing.Tuple[int, int],
    workspace: typing.Optional[int] = None,
    verbose: bool = False,
    use_cuda_graph: bool = False,
    static_shape: bool = True,
    min_shapes: typing.Tuple[int, int] = (0, 0),
    use_cudnn: bool = False,
    use_edge_mask_convolutions: bool = True,
    input_name: str = "input",
    builder_optimization_level: int = 3,
    max_aux_streams: typing.Optional[int] = None,
    short_path: typing.Optional[bool] = None,
    custom_env: typing.Dict[str, str] = {},
    custom_args: typing.List[str] = [],
    engine_folder: typing.Optional[str] = None,
    max_tactics: typing.Optional[int] = None,
    tiling_optimization_level: int = 0,
    l2_limit_for_tiling: int = -1,
    fp16_io: bool = False,
) -> str:

    # tensort runtime version
    trt_version = parse_trt_version(int(core.trt_rtx.Version()["tensorrt_version"]))

    if fp16:
        with open(network_path, "rb") as file:
            checksum = zlib.adler32(file.read())

        dirname, basename = os.path.split(network_path)

        if engine_folder is not None:
            os.makedirs(engine_folder, exist_ok=True)
            dirname = engine_folder

        fp16_network_path = f"{os.path.join(dirname, basename)}_{checksum}_fp16{'_io' if fp16_io else ''}.onnx"
        if not (os.access(fp16_network_path, mode=os.R_OK) and os.path.getsize(fp16_network_path) >= 1024):
            import onnx
            model = onnx.load(network_path)
            try:
                from onnxconverter_common.float16 import convert_float_to_float16
                with warnings.catch_warnings():
                    warnings.simplefilter("ignore")
                    model = convert_float_to_float16(model, keep_io_types=not fp16_io)
            except Exception:
                import logging
                from modelopt.onnx.autocast import convert_to_f16, configure_logging
                configure_logging(logging.ERROR)
                model = convert_to_f16(model, keep_io_types=not fp16_io)
            onnx.save(model, fp16_network_path)
        network_path = fp16_network_path
    elif fp16_io:
        raise ValueError('tensorrt_rtx: "fp16" must be True.')

    try:
        device_name = core.trt_rtx.DeviceProperties(device_id)["name"].decode()
        device_name = device_name.replace(' ', '-')
    except AttributeError:
        device_name = f"device{device_id}"

    engine_path = get_engine_path(
        network_path=network_path,
        min_shapes=min_shapes,
        opt_shapes=opt_shapes,
        max_shapes=max_shapes,
        workspace=workspace,
        fp16=fp16,
        use_cublas=False,
        static_shape=static_shape,
        tf32=False,
        use_cudnn=use_cudnn,
        input_format=int(fp16_io),
        output_format=int(fp16_io),
        builder_optimization_level=builder_optimization_level,
        max_aux_streams=max_aux_streams,
        short_path=short_path,
        bf16=False,
        engine_folder=engine_folder,
        is_rtx=True,
        trt_version=trt_version,
        device_name=device_name,
    )

    if os.access(engine_path, mode=os.R_OK) and os.path.getsize(engine_path) >= 1024:
        return engine_path

    # do not consider alternative path when the engine_folder is given
    if engine_folder is None:
        alter_engine_path = os.path.join(
            tempfile.gettempdir(),
            os.path.splitdrive(engine_path)[1][1:]
        )

        if os.access(alter_engine_path, mode=os.R_OK) and os.path.getsize(alter_engine_path) >= 1024:
            return alter_engine_path

    try:
        # test writability
        with open(engine_path, "w") as f:
            pass
        os.remove(engine_path)
    except PermissionError:
        if engine_folder is None:
            print(f"{engine_path} is not writable", file=sys.stderr)
            engine_path = alter_engine_path
            dirname = os.path.dirname(engine_path)
            if not os.path.exists(dirname):
                os.makedirs(dirname)
            print(f"change engine path to {engine_path}", file=sys.stderr)
        else:
            # do not consider alternative path when the engine_folder is given
            raise PermissionError(f"{engine_path} is not writable")

    args = [
        tensorrt_rtx_path,
        f"--onnx={network_path}",
        f"--timingCacheFile={engine_path}.cache",
        f"--device={device_id}",
        f"--saveEngine={engine_path}",
        "--useGpu",
    ]

    if workspace is not None:
        args.append(f"--memPoolSize=workspace:{workspace}")

    if static_shape:
        args.append(f"--shapes={input_name}:1x{channels}x{opt_shapes[1]}x{opt_shapes[0]}")
    else:
        args.extend([
            f"--minShapes={input_name}:1x{channels}x{min_shapes[1]}x{min_shapes[0]}",
            f"--optShapes={input_name}:1x{channels}x{opt_shapes[1]}x{opt_shapes[0]}",
            f"--maxShapes={input_name}:1x{channels}x{max_shapes[1]}x{max_shapes[0]}",
            "--specializeStrategyDS=eager"
        ])

    if verbose:
        args.append("--verbose")

    tactic_sources = []

    if use_cudnn:
        tactic_sources.append("+CUDNN")
    else:
        tactic_sources.append("-CUDNN")

    if use_edge_mask_convolutions:
        tactic_sources.append("+EDGE_MASK_CONVOLUTIONS")
    else:
        tactic_sources.append("-EDGE_MASK_CONVOLUTIONS")

    args.append(f"--tacticSources={','.join(tactic_sources)}")

    if use_cuda_graph:
        args.extend((
            "--useCudaGraph",
            "--noDataTransfers"
        ))
    else:
        args.append("--skipInference")

    args.append(f"--builderOptimizationLevel={builder_optimization_level}")

    if max_aux_streams is not None:
        args.append(f"--maxAuxStreams={max_aux_streams}")

    if max_tactics is not None:
        args.append(f"--maxTactics={max_tactics}")

    if tiling_optimization_level != 0:
        args.append(f"--tilingOptimizationLevel={tiling_optimization_level}")
        args.append(f"--l2LimitForTiling={l2_limit_for_tiling}")

    args.extend(custom_args)

    env = {"CUDA_MODULE_LOADING": "LAZY"}
    env.update(**custom_env)
    subprocess.run(args, env=env, check=True, stdout=sys.stderr)

    return engine_path


def calc_size(width: int, tiles: int, overlap: int, multiple: int = 1) -> int:
    return math.ceil((width + 2 * overlap * (tiles - 1)) / (tiles * multiple)) * multiple


def calc_tilesize(
    tiles: typing.Optional[typing.Union[int, typing.Tuple[int, int]]],
    tilesize: typing.Optional[typing.Union[int, typing.Tuple[int, int]]],
    width: int,
    height: int,
    multiple: int,
    overlap_w: int,
    overlap_h: int
) -> typing.Tuple[typing.Tuple[int, int], typing.Tuple[int, int]]:

    if tilesize is None:
        if tiles is None:
            overlap_w = 0
            overlap_h = 0
            tile_w = width
            tile_h = height
        elif isinstance(tiles, int):
            tile_w = calc_size(width, tiles, overlap_w, multiple)
            tile_h = calc_size(height, tiles, overlap_h, multiple)
        else:
            tile_w = calc_size(width, tiles[0], overlap_w, multiple)
            tile_h = calc_size(height, tiles[1], overlap_h, multiple)
    elif isinstance(tilesize, int):
        tile_w = tilesize
        tile_h = tilesize
    else:
        tile_w, tile_h = tilesize

    return (tile_w, tile_h), (overlap_w, overlap_h)


def init_backend(
    backend: backendT,
    trt_opt_shapes: typing.Tuple[int, int]
) -> backendT:

    if backend is Backend.ORT_CPU: # type: ignore
        backend = Backend.ORT_CPU()
    elif backend is Backend.ORT_CUDA: # type: ignore
        backend = Backend.ORT_CUDA()
    elif backend is Backend.OV_CPU: # type: ignore
        backend = Backend.OV_CPU()
    elif backend is Backend.TRT: # type: ignore
        backend = Backend.TRT()
    elif backend is Backend.OV_GPU: # type: ignore
        backend = Backend.OV_GPU()
    elif backend is Backend.NCNN_VK: # type: ignore
        backend = Backend.NCNN_VK()
    elif backend is Backend.ORT_DML: # type: ignore
        backend = Backend.ORT_DML()
    elif backend is Backend.MIGX: # type: ignore
        backend = Backend.MIGX()
    elif backend is Backend.OV_NPU: # type: ignore
        backend = Backend.OV_NPU()
    elif backend is Backend.TRT_RTX: # type: ignore
        backend = Backend.TRT_RTX()

    backend = copy.deepcopy(backend)

    if isinstance(backend, (Backend.TRT, Backend.TRT_RTX)):
        if backend.opt_shapes is None:
            backend.opt_shapes = trt_opt_shapes

        if backend.max_shapes is None:
            backend.max_shapes = backend.opt_shapes
    elif isinstance(backend, Backend.MIGX):
        if backend.opt_shapes is None:
            backend.opt_shapes = trt_opt_shapes

    return backend


def _inference(
    clips: typing.List[vs.VideoNode],
    network_path: typing.Union[bytes, str],
    overlap: typing.Tuple[int, int],
    tilesize: typing.Tuple[int, int],
    backend: backendT,
    path_is_serialization: bool = False,
    input_name: str = "input",
    flexible_output_prop: typing.Optional[str] = None,
    batch_size: int = 1
) -> typing.Union[vs.VideoNode, typing.Dict[str, typing.Any]]:

    if not path_is_serialization:
        network_path = typing.cast(str, network_path)
        if not os.path.exists(network_path):
            raise RuntimeError(
                f'"{network_path}" not found, '
                "built-in models can be found at "
                "https://github.com/AmusementClub/vs-mlrt/releases/tag/model-20211209, "
                "https://github.com/AmusementClub/vs-mlrt/releases/tag/model-20220923 and "
                "https://github.com/AmusementClub/vs-mlrt/releases/tag/external-models"
            )

    if path_is_serialization:
        if isinstance(backend, Backend.TRT):
            raise ValueError('"path_is_serialization" must be False for trt backend')
        elif isinstance(backend, Backend.MIGX):
            raise ValueError('"path_is_serialization" must be False for migx backend')
        elif isinstance(backend, Backend.TRT_RTX):
            raise ValueError('"path_is_serialization" must be False for trt_rtx backend')

    if not isinstance(batch_size, int) or batch_size < 1:
        raise ValueError('"batch_size" must be a positve integer')

    if batch_size > 1:
        import numpy as np
        import onnx

        if path_is_serialization:
            model = onnx.load_model_from_string(network_path)
        else:
            model = onnx.load(network_path)

        graph = model.graph
        in_channels = graph.input[0].type.tensor_type.shape.dim[1].dim_value
        graph.input[0].type.tensor_type.shape.dim[1].dim_value *= batch_size
        graph.output[0].type.tensor_type.shape.dim[1].dim_param = "_vsmlrt_output_channels"

        input_name = graph.input[0].name
        output_name = graph.output[0].name
        for node in graph.node:
            for i, name in enumerate(node.input):
                if name == input_name:
                    node.input[i] = "_vsmlrt_input"

            for i, name in enumerate(node.output):
                if name == output_name:
                    node.output[i] = "_vsmlrt_output"

        graph.node.insert(1, onnx.helper.make_node(
            op_type="Constant",
            inputs=[],
            outputs=["_vsmlrt_input_shape"],
            value=onnx.numpy_helper.from_array(np.array([-1, in_channels, 0, 0]))
        ))
        graph.node.insert(2, onnx.helper.make_node(
            op_type="Reshape",
            inputs=[input_name, "_vsmlrt_input_shape"],
            outputs=["_vsmlrt_input"]
        ))

        graph.node.insert(-1, onnx.helper.make_node(
            op_type="Constant",
            inputs=[],
            outputs=["_vsmlrt_output_shape"],
            value=onnx.numpy_helper.from_array(np.array([1, -1, 0, 0]))
        ))
        graph.node.insert(-1, onnx.helper.make_node(
            op_type="Reshape",
            inputs=["_vsmlrt_output", "_vsmlrt_output_shape"],
            outputs=[output_name]
        ))

        if backend.supports_onnx_serialization:
            network_path = model.SerializeToString()
        else:
            network_path = f"{network_path}_batch{batch_size}.onnx"
            onnx.save(model, network_path)

        path_is_serialization = backend.supports_onnx_serialization

        pad = (batch_size - clips[0].num_frames % batch_size) % batch_size
        if pad:
            clips = [clip.std.DuplicateFrames([clip.num_frames - 1] * pad) for clip in clips]

        clips = [
            clip[i::batch_size]
            for i in range(batch_size)
            for clip in clips
        ]

        flexible_output_prop_orig = flexible_output_prop

        if flexible_output_prop is None:
            flexible_output_prop = "vsmlrt_flexible_batch"

    kwargs = dict(overlap=overlap, tilesize=tilesize)
    if flexible_output_prop is not None:
        kwargs["flexible_output_prop"] = flexible_output_prop

    if isinstance(backend, (Backend.ORT_CPU, Backend.ORT_DML, Backend.ORT_COREML, Backend.ORT_CUDA)):
        version_list = core.ort.Version().get("onnxruntime_version", b"0.0.0").split(b'.')
        if len(version_list) != 3:
            version = (0, 0, 0)
        else:
            version = tuple(map(int, version_list))

        if version >= (1, 18, 0):
            kwargs["output_format"] = backend.output_format

    elif isinstance(backend, Backend.NCNN_VK):
        if "output_format" in core.ncnn.Model.signature:
            kwargs["output_format"] = backend.output_format

    if isinstance(backend, Backend.ORT_CPU):
        ret = core.ort.Model(
            clips, network_path,
            provider="CPU", builtin=False,
            num_streams=backend.num_streams,
            verbosity=backend.verbosity,
            fp16=backend.fp16,
            path_is_serialization=path_is_serialization,
            fp16_blacklist_ops=backend.fp16_blacklist_ops,
            **kwargs
        )
    elif isinstance(backend, Backend.ORT_DML):
        ret = core.ort.Model(
            clips, network_path,
            provider="DML", builtin=False,
            device_id=backend.device_id,
            num_streams=backend.num_streams,
            verbosity=backend.verbosity,
            fp16=backend.fp16,
            path_is_serialization=path_is_serialization,
            fp16_blacklist_ops=backend.fp16_blacklist_ops,
            **kwargs
        )
    elif isinstance(backend, Backend.ORT_COREML):
        ret = core.ort.Model(
            clips, network_path,
            provider="COREML", builtin=False,
            num_streams=backend.num_streams,
            verbosity=backend.verbosity,
            fp16=backend.fp16,
            path_is_serialization=path_is_serialization,
            fp16_blacklist_ops=backend.fp16_blacklist_ops,
            ml_program=backend.ml_program,
            **kwargs
        )
    elif isinstance(backend, Backend.ORT_CUDA):
        version_list = core.ort.Version().get("onnxruntime_version", b"0.0.0").split(b'.')
        if len(version_list) != 3:
            version = (0, 0, 0)
        else:
            version = tuple(map(int, version_list))

        if version >= (1, 18, 0):
            kwargs["prefer_nhwc"] = backend.prefer_nhwc
            kwargs["tf32"] = backend.tf32

        ret = core.ort.Model(
            clips, network_path,
            provider="CUDA", builtin=False,
            device_id=backend.device_id,
            num_streams=backend.num_streams,
            verbosity=backend.verbosity,
            cudnn_benchmark=backend.cudnn_benchmark,
            fp16=backend.fp16,
            path_is_serialization=path_is_serialization,
            use_cuda_graph=backend.use_cuda_graph,
            fp16_blacklist_ops=backend.fp16_blacklist_ops,
            **kwargs
        )
    elif isinstance(backend, Backend.OV_CPU):
        version = tuple(map(int, core.ov.Version().get("openvino_version", b"0.0.0").split(b'-')[0].split(b'.')))

        if version >= (2024, 0, 0):
            config_dict = dict(
                NUM_STREAMS=backend.num_streams,
                INFERENCE_NUM_THREADS=backend.num_threads,
                ENABLE_CPU_PINNING="YES" if backend.bind_thread else "NO"
            )
            if backend.fp16:
                config_dict["INFERENCE_PRECISION_HINT"] = "f16"
            elif backend.bf16:
                config_dict["INFERENCE_PRECISION_HINT"] = "bf16"
            else:
                config_dict["INFERENCE_PRECISION_HINT"] = "f32"

            config = lambda: config_dict
        else:
            config = lambda: dict(
                CPU_THROUGHPUT_STREAMS=backend.num_streams,
                CPU_BIND_THREAD="YES" if backend.bind_thread else "NO",
                CPU_THREADS_NUM=backend.num_threads,
                ENFORCE_BF16="YES" if backend.bf16 else "NO"
            )

        ret = core.ov.Model(
            clips, network_path,
            device="CPU", builtin=False,
            fp16=False, # use ov's internal quantization
            config=config,
            path_is_serialization=path_is_serialization,
            fp16_blacklist_ops=backend.fp16_blacklist_ops, # disabled since fp16 = False
            **kwargs
        )
    elif isinstance(backend, Backend.OV_GPU):
        version = tuple(map(int, core.ov.Version().get("openvino_version", b"0.0.0").split(b'-')[0].split(b'.')))

        if version >= (2024, 0, 0):
            config_dict = dict(
                NUM_STREAMS=backend.num_streams,
            )
            if backend.fp16:
                config_dict["INFERENCE_PRECISION_HINT"] = "f16"
            else:
                config_dict["INFERENCE_PRECISION_HINT"] = "f32"

            config = lambda: config_dict
        else:
            config = lambda: dict(
                GPU_THROUGHPUT_STREAMS=backend.num_streams
            )

        ret = core.ov.Model(
            clips, network_path,
            device=f"GPU.{backend.device_id}", builtin=False,
            fp16=False, # use ov's internal quantization
            config=config,
            path_is_serialization=path_is_serialization,
            fp16_blacklist_ops=backend.fp16_blacklist_ops,
            **kwargs
        )
    elif isinstance(backend, Backend.TRT):
        network_path = typing.cast(str, network_path)

        channels = sum(clip.format.num_planes for clip in clips)

        opt_shapes = backend.opt_shapes if backend.opt_shapes is not None else tilesize
        max_shapes = backend.max_shapes if backend.max_shapes is not None else tilesize

        engine_path = trtexec(
            network_path,
            channels=channels,
            opt_shapes=opt_shapes,
            max_shapes=max_shapes,
            fp16=backend.fp16,
            device_id=backend.device_id,
            workspace=backend.workspace,
            verbose=backend.verbose,
            use_cuda_graph=backend.use_cuda_graph,
            use_cublas=backend.use_cublas,
            static_shape=backend.static_shape,
            tf32=backend.tf32,
            log=backend.log,
            use_cudnn=backend.use_cudnn,
            use_edge_mask_convolutions=backend.use_edge_mask_convolutions,
            use_jit_convolutions=backend.use_jit_convolutions,
            heuristic=backend.heuristic,
            input_name=input_name,
            input_format=clips[0].format.bits_per_sample == 16,
            output_format=backend.output_format,
            min_shapes=backend.min_shapes,
            faster_dynamic_shapes=backend.faster_dynamic_shapes,
            force_fp16=backend.force_fp16,
            builder_optimization_level=backend.builder_optimization_level,
            max_aux_streams=backend.max_aux_streams,
            short_path=backend.short_path,
            bf16=backend.bf16,
            custom_env=backend.custom_env,
            custom_args=backend.custom_args,
            engine_folder=backend.engine_folder,
            max_tactics=backend.max_tactics,
            tiling_optimization_level=backend.tiling_optimization_level,
            l2_limit_for_tiling=backend.l2_limit_for_tiling,
        )
        ret = core.trt.Model(
            clips, engine_path,
            device_id=backend.device_id,
            use_cuda_graph=backend.use_cuda_graph,
            num_streams=backend.num_streams,
            verbosity=4 if backend.verbose else 2,
            **kwargs
        )
    elif isinstance(backend, Backend.NCNN_VK):
        ret = core.ncnn.Model(
            clips, network_path,
            device_id=backend.device_id,
            num_streams=backend.num_streams,
            builtin=False,
            fp16=backend.fp16,
            path_is_serialization=path_is_serialization,
            **kwargs
        )
    elif isinstance(backend, Backend.MIGX):
        network_path = typing.cast(str, network_path)

        channels = sum(clip.format.num_planes for clip in clips)

        opt_shapes = backend.opt_shapes if backend.opt_shapes is not None else tilesize

        mxr_path = migraphx_driver(
            network_path,
            channels=channels,
            opt_shapes=opt_shapes,
            fp16=backend.fp16,
            fast_math=backend.fast_math,
            exhaustive_tune=backend.exhaustive_tune,
            device_id=backend.device_id,
            input_name=input_name,
            short_path=backend.short_path,
            custom_env=backend.custom_env,
            custom_args=backend.custom_args,
            bf16=backend.bf16,
        )
        ret = core.migx.Model(
            clips, mxr_path,
            device_id=backend.device_id,
            num_streams=backend.num_streams,
            **kwargs
        )
    elif isinstance(backend, Backend.OV_NPU):
        ret = core.ov.Model(
            clips, network_path,
            device="NPU", builtin=False,
            fp16=False, # use ov's internal quantization
            path_is_serialization=path_is_serialization,
            **kwargs
        )
    elif isinstance(backend, Backend.TRT_RTX):
        network_path = typing.cast(str, network_path)

        channels = sum(clip.format.num_planes for clip in clips)

        opt_shapes = backend.opt_shapes if backend.opt_shapes is not None else tilesize
        max_shapes = backend.max_shapes if backend.max_shapes is not None else tilesize

        engine_path = tensorrt_rtx(
            network_path,
            channels=channels,
            fp16=backend.fp16,
            device_id=backend.device_id,
            opt_shapes=backend.opt_shapes,
            max_shapes=backend.max_shapes,
            workspace=backend.workspace,
            verbose=backend.verbose,
            use_cuda_graph=backend.use_cuda_graph,
            static_shape=backend.static_shape,
            min_shapes=backend.min_shapes,
            use_cudnn=backend.use_cudnn,
            use_edge_mask_convolutions=backend.use_edge_mask_convolutions,
            input_name=input_name,
            builder_optimization_level=backend.builder_optimization_level,
            max_aux_streams=backend.max_aux_streams,
            short_path=backend.short_path,
            custom_env=backend.custom_env,
            custom_args=backend.custom_args,
            engine_folder=backend.engine_folder,
            max_tactics=backend.max_tactics,
            tiling_optimization_level=backend.tiling_optimization_level,
            l2_limit_for_tiling=backend.l2_limit_for_tiling,

            # the following option is experimental
            # input_format=clips[0].format.bits_per_sample == 16,
            # output_format=backend.output_format,
            fp16_io=clips[0].format.bits_per_sample == 16
        )
        ret = core.trt_rtx.Model(
            clips, engine_path,
            device_id=backend.device_id,
            use_cuda_graph=backend.use_cuda_graph,
            num_streams=backend.num_streams,
            verbosity=4 if backend.verbose else 2,
            **kwargs
        )
    else:
        raise TypeError(f'unknown backend {backend}')

    if batch_size > 1:
        clip = ret["clip"]
        num_planes = ret["num_planes"]
        clips = [
            clip.std.PropToClip(prop=f"{flexible_output_prop}{i}")
            for i in range(num_planes)
        ]

        if flexible_output_prop_orig is None:
            if num_planes == batch_size * 3:
                clips = [
                    core.std.ShufflePlanes(clips[i:i+3], [0] * 3, vs.RGB)
                    for i in range(0, num_planes, 3)
                ]
            elif num_planes != batch_size:
                raise ValueError("number of output channels must be 1 or 3")

            ret = core.std.Interleave(clips)
            if pad:
                ret = ret[:-pad]
        else:
            clips = [core.std.Interleave(clips[i::batch_size]) for i in range(num_planes // batch_size)]
            if pad:
                clips = [clip[:-pad] for clip in clips]

            clip = clip.std.BlankClip(keep=True)
            for i in range(len(clips)):
                clip = clip.std.ClipToProp(clips[i], f"{flexible_output_prop_orig}{i}")

            ret = dict(clip=clip, num_planes=len(clips))

    return ret


def inference_with_fallback(
    clips: typing.List[vs.VideoNode],
    network_path: typing.Union[bytes, str],
    overlap: typing.Tuple[int, int],
    tilesize: typing.Tuple[int, int],
    backend: backendT,
    path_is_serialization: bool = False,
    input_name: str = "input",
    batch_size: int = 1 # experimental
) -> vs.VideoNode:

    try:
        ret = _inference(
            clips=clips, network_path=network_path,
            overlap=overlap, tilesize=tilesize,
            backend=backend,
            path_is_serialization=path_is_serialization,
            input_name=input_name,
            batch_size=batch_size
        )
    except Exception as e:
        if fallback_backend is not None:
            import logging
            logger = logging.getLogger("vsmlrt")
            logger.warning(f'"{backend}" fails, trying fallback backend "{fallback_backend}"')

            ret = _inference(
                clips=clips, network_path=network_path,
                overlap=overlap, tilesize=tilesize,
                backend=fallback_backend,
                path_is_serialization=path_is_serialization,
                input_name=input_name,
                batch_size=batch_size
            )
        else:
            raise e

    return typing.cast(vs.VideoNode, ret)


def inference(
    clips: typing.Union[vs.VideoNode, typing.List[vs.VideoNode]],
    network_path: str,
    overlap: typing.Tuple[int, int] = (0, 0),
    tilesize: typing.Optional[typing.Tuple[int, int]] = None,
    backend: backendT = Backend.OV_CPU(),
    input_name: typing.Optional[str] = "input",
    batch_size: int = 1 # experimental
) -> vs.VideoNode:

    if isinstance(clips, vs.VideoNode):
        clips = [clips]

    if tilesize is None:
        tilesize = (clips[0].width, clips[0].height)

    backend = init_backend(backend=backend, trt_opt_shapes=tilesize)

    if input_name is None:
        input_name = get_input_name(network_path)

    return inference_with_fallback(
        clips=clips,
        network_path=network_path,
        overlap=overlap,
        tilesize=tilesize,
        backend=backend,
        path_is_serialization=False,
        input_name=input_name,
        batch_size=batch_size
    )


def flexible_inference_with_fallback(
    clips: typing.List[vs.VideoNode],
    network_path: typing.Union[bytes, str],
    overlap: typing.Tuple[int, int],
    tilesize: typing.Tuple[int, int],
    backend: backendT,
    path_is_serialization: bool = False,
    input_name: str = "input",
    flexible_output_prop: str = "vsmlrt_flexible",
    batch_size: int = 1 # experimental
) -> typing.List[vs.VideoNode]:

    try:
        ret = _inference(
            clips=clips, network_path=network_path,
            overlap=overlap, tilesize=tilesize,
            backend=backend,
            path_is_serialization=path_is_serialization,
            input_name=input_name,
            flexible_output_prop=flexible_output_prop,
            batch_size=batch_size
        )
    except Exception as e:
        if fallback_backend is not None:
            import logging
            logger = logging.getLogger("vsmlrt")
            logger.warning(f'"{backend}" fails, trying fallback backend "{fallback_backend}"')

            ret = _inference(
                clips=clips, network_path=network_path,
                overlap=overlap, tilesize=tilesize,
                backend=fallback_backend,
                path_is_serialization=path_is_serialization,
                input_name=input_name,
                flexible_output_prop=flexible_output_prop,
                batch_size=batch_size
            )
        else:
            raise e

    ret = typing.cast(typing.Dict[str, typing.Any], ret)
    clip = ret["clip"]
    num_planes = ret["num_planes"]

    planes = [
        clip.std.PropToClip(prop=f"{flexible_output_prop}{i}")
        for i in range(num_planes)
    ]

    return planes


def flexible_inference(
    clips: typing.Union[vs.VideoNode, typing.List[vs.VideoNode]],
    network_path: str,
    overlap: typing.Tuple[int, int] = (0, 0),
    tilesize: typing.Optional[typing.Tuple[int, int]] = None,
    backend: backendT = Backend.OV_CPU(),
    input_name: typing.Optional[str] = "input",
    flexible_output_prop: str = "vsmlrt_flexible",
    batch_size: int = 1 # experimental
) -> typing.List[vs.VideoNode]:

    if isinstance(clips, vs.VideoNode):
        clips = [clips]

    if tilesize is None:
        tilesize = (clips[0].width, clips[0].height)

    backend = init_backend(backend=backend, trt_opt_shapes=tilesize)

    if input_name is None:
        input_name = get_input_name(network_path)

    return flexible_inference_with_fallback(
        clips=clips,
        network_path=network_path,
        overlap=overlap,
        tilesize=tilesize,
        backend=backend,
        path_is_serialization=False,
        input_name=input_name,
        flexible_output_prop=flexible_output_prop,
        batch_size=batch_size
    )


def get_input_name(network_path: str) -> str:
    import onnx
    model = onnx.load(network_path)
    return model.graph.input[0].name


def bits_as(clip: vs.VideoNode, target: vs.VideoNode) -> vs.VideoNode:
    if clip.format.bits_per_sample == target.format.bits_per_sample:
        return clip
    else:
        is_api4 = hasattr(vs, "__api_version__") and vs.__api_version__.api_major == 4
        query_video_format = core.query_video_format if is_api4 else core.register_format
        format = query_video_format(
            color_family=clip.format.color_family,
            sample_type=clip.format.sample_type,
            bits_per_sample=target.format.bits_per_sample,
            subsampling_w=clip.format.subsampling_w,
            subsampling_h=clip.format.subsampling_h
        )
        return clip.resize.Point(format=format)


class BackendV2:
    """ simplified backend interfaces with keyword-only arguments

    More exposed arguments may be added for each backend,
    but existing ones will always function in a forward compatible way.
    """

    @staticmethod
    def TRT(*,
        num_streams: int = 1,
        fp16: bool = False,
        tf32: bool = False,
        output_format: int = 0, # 0: fp32, 1: fp16
        workspace: typing.Optional[int] = None,
        use_cuda_graph: bool = False,
        static_shape: bool = True,
        min_shapes: typing.Tuple[int, int] = (0, 0),
        opt_shapes: typing.Optional[typing.Tuple[int, int]] = None,
        max_shapes: typing.Optional[typing.Tuple[int, int]] = None,
        force_fp16: bool = False,
        use_cublas: bool = False,
        use_cudnn: bool = False,
        device_id: int = 0,
        **kwargs
    ) -> Backend.TRT:

        return Backend.TRT(
            num_streams=num_streams,
            fp16=fp16, force_fp16=force_fp16, tf32=tf32, output_format=output_format,
            workspace=workspace, use_cuda_graph=use_cuda_graph,
            static_shape=static_shape,
            min_shapes=min_shapes, opt_shapes=opt_shapes, max_shapes=max_shapes,
            use_cublas=use_cublas, use_cudnn=use_cudnn,
            device_id=device_id,
            **kwargs
        )

    @staticmethod
    def NCNN_VK(*,
        num_streams: int = 1,
        fp16: bool = False,
        device_id: int = 0,
        **kwargs
    ) -> Backend.NCNN_VK:
        return Backend.NCNN_VK(
            num_streams=num_streams,
            fp16=fp16,
            device_id=device_id,
            **kwargs
        )

    @staticmethod
    def ORT_CUDA(*,
        num_streams: int = 1,
        fp16: bool = False,
        cudnn_benchmark: bool = True,
        device_id: int = 0,
        **kwargs
    ) -> Backend.ORT_CUDA:
        return Backend.ORT_CUDA(
            num_streams=num_streams,
            fp16=fp16,
            cudnn_benchmark=cudnn_benchmark,
            device_id=device_id,
            **kwargs
        )

    @staticmethod
    def OV_CPU(*,
        num_streams: typing.Union[int, str] = 1,
        bf16: bool = False,
        bind_thread: bool = True,
        num_threads: int = 0,
        **kwargs
    ) -> Backend.OV_CPU:
        return Backend.OV_CPU(
            num_streams=num_streams,
            bf16=bf16,
            bind_thread=bind_thread,
            num_threads=num_threads,
            **kwargs
        )

    @staticmethod
    def ORT_CPU(*,
        num_streams: int = 1,
        **kwargs
    ) -> Backend.ORT_CPU:
        return Backend.ORT_CPU(
            num_streams=num_streams,
            **kwargs
        )

    @staticmethod
    def OV_GPU(*,
        num_streams: typing.Union[int, str] = 1,
        fp16: bool = False,
        device_id: int = 0,
        **kwargs
    ) -> Backend.OV_GPU:
        return Backend.OV_GPU(
            num_streams=num_streams,
            fp16=fp16,
            device_id=device_id,
            **kwargs
        )

    @staticmethod
    def ORT_DML(*,
        device_id: int = 0,
        num_streams: int = 1,
        fp16: bool = False,
        **kwargs
    ) -> Backend.ORT_DML:
        return Backend.ORT_DML(
            device_id=device_id,
            num_streams=num_streams,
            fp16=fp16,
            **kwargs
        )

    @staticmethod
    def MIGX(*,
        fp16: bool = False,
        opt_shapes: typing.Optional[typing.Tuple[int, int]] = None,
        **kwargs
    ) -> Backend.MIGX:

        return Backend.MIGX(
            fp16=fp16,
            opt_shapes=opt_shapes,
            **kwargs
        )

    @staticmethod
    def OV_NPU(**kwargs
    ) -> Backend.OV_NPU:
        return Backend.OV_NPU(
            **kwargs
        )

    @staticmethod
    def ORT_COREML(*,
        num_streams: int = 1,
        fp16: bool = False,
        **kwargs
    ) -> Backend.ORT_COREML:
        return Backend.ORT_COREML(
            num_streams=num_streams,
            fp16=fp16,
            **kwargs
        )

    @staticmethod
    def TRT_RTX(*,
        num_streams: int = 1,
        fp16: bool = False,
        workspace: typing.Optional[int] = None,
        use_cuda_graph: bool = False,
        static_shape: bool = True,
        min_shapes: typing.Tuple[int, int] = (0, 0),
        opt_shapes: typing.Optional[typing.Tuple[int, int]] = None,
        max_shapes: typing.Optional[typing.Tuple[int, int]] = None,
        device_id: int = 0,
        **kwargs
    ) -> Backend.TRT_RTX:

        return Backend.TRT_RTX(
            num_streams=num_streams,
            fp16=fp16,
            workspace=workspace, use_cuda_graph=use_cuda_graph,
            static_shape=static_shape,
            min_shapes=min_shapes, opt_shapes=opt_shapes, max_shapes=max_shapes,
            device_id=device_id,
            **kwargs
        )


def fmtc_resample(clip: vs.VideoNode, **kwargs) -> vs.VideoNode:
    clip_org = clip

    if clip.format.sample_type == vs.FLOAT and clip.format.bits_per_sample != 32:
        format = clip.format.replace(core=core, bits_per_sample=32)
        clip = core.resize.Point(clip, format=format.id)

    clip = core.fmtc.resample(clip, **kwargs)

    if clip.format.bits_per_sample != clip_org.format.bits_per_sample:
        clip = core.resize.Point(clip, format=clip_org.format.id)

    return clip


def parse_trt_version(version: int) -> typing.Tuple[int, int, int]:
    # before trt 10
    if version < 10000:
        return version // 1000, (version // 100) % 10, version % 100
    else:
        return version // 10000, (version // 100) % 100, version % 100


def _expr(
    clip: vs.VideoNode,
    expr: typing.Union[str, typing.Sequence[str]],
    format: typing.Optional[int] = None
) -> vs.VideoNode:
    try:
        return core.akarin.Expr(clip, expr, format)
    except vs.Error:
        return core.std.Expr(clip, expr, format)
