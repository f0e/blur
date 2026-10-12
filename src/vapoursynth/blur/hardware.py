import ctypes
import subprocess
import sys
from pathlib import Path

CL_DEVICE_TYPE_GPU = 1 << 2
CL_DEVICE_NAME = 0x102B
CL_DRIVER_VERSION = 0x102D


def cpu_name() -> str:
    # not platform.processor(), it's generic on windows and goes through wmi, which can hang
    try:
        match sys.platform:
            case "win32":
                import winreg

                with winreg.OpenKey(
                    winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0"
                ) as key:
                    return str(winreg.QueryValueEx(key, "ProcessorNameString")[0]).strip()
            case "darwin":
                return subprocess.run(
                    ["sysctl", "-n", "machdep.cpu.brand_string"], capture_output=True, text=True, check=True
                ).stdout.strip()
            case _:
                for line in Path("/proc/cpuinfo").read_text().splitlines():
                    if line.startswith("model name"):
                        return line.split(":", 1)[1].strip()
    except (OSError, subprocess.SubprocessError):
        return ""

    return ""


def gpus() -> list[dict[str, str]]:
    # opencl is what svp uses, and it reports the driver version on every platform
    names = {
        "win32": ["OpenCL.dll"],
        "darwin": ["/System/Library/Frameworks/OpenCL.framework/OpenCL"],
    }.get(sys.platform, ["libOpenCL.so.1", "libOpenCL.so"])

    cl = None
    for name in names:
        try:
            cl = ctypes.CDLL(name)
            break
        except OSError:
            continue

    if cl is None:
        return []

    uint_p = ctypes.POINTER(ctypes.c_uint)
    size_p = ctypes.POINTER(ctypes.c_size_t)
    cl.clGetPlatformIDs.argtypes = [ctypes.c_uint, ctypes.c_void_p, uint_p]
    cl.clGetDeviceIDs.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint, ctypes.c_void_p, uint_p]
    cl.clGetDeviceInfo.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_size_t, ctypes.c_void_p, size_p]

    def device_string(device: ctypes.c_void_p, param: int) -> str:
        size = ctypes.c_size_t()
        if cl.clGetDeviceInfo(device, param, 0, None, ctypes.byref(size)) != 0:
            return ""

        buffer = ctypes.create_string_buffer(size.value)
        if cl.clGetDeviceInfo(device, param, size, buffer, None) != 0:
            return ""

        return buffer.value.decode("utf-8", errors="replace").strip()

    result = []

    platform_count = ctypes.c_uint()
    if cl.clGetPlatformIDs(0, None, ctypes.byref(platform_count)) != 0 or platform_count.value == 0:
        return []

    platforms = (ctypes.c_void_p * platform_count.value)()
    cl.clGetPlatformIDs(platform_count, platforms, None)

    for platform in platforms:
        device_count = ctypes.c_uint()
        if cl.clGetDeviceIDs(ctypes.c_void_p(platform), CL_DEVICE_TYPE_GPU, 0, None, ctypes.byref(device_count)) != 0:
            continue

        devices = (ctypes.c_void_p * device_count.value)()
        cl.clGetDeviceIDs(ctypes.c_void_p(platform), CL_DEVICE_TYPE_GPU, device_count, devices, None)

        for device in devices:
            device = ctypes.c_void_p(device)
            result.append(
                {"name": device_string(device, CL_DEVICE_NAME), "driver": device_string(device, CL_DRIVER_VERSION)}
            )

    return result
