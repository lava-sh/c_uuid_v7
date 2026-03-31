import os
import sys
import sysconfig

from setuptools import Extension, setup


def _platform_parts() -> list[str]:
    return sysconfig.get_platform().split("-")


def _macos_target() -> str | None:
    arch_flags = os.environ.get("ARCHFLAGS", "").split()

    for index, flag in enumerate(arch_flags[:-1]):
        if flag == "-arch":
            arch = arch_flags[index + 1]
            break
    else:
        parts = _platform_parts()
        if len(parts) < 3 or parts[0] != "macosx":
            return None
        arch = parts[-1]

    zig_arch = {
        "x86_64": "x86_64",
        "arm64": "aarch64",
    }.get(arch)
    if zig_arch is None:
        return None

    deployment_target = os.environ.get("MACOSX_DEPLOYMENT_TARGET")
    if deployment_target:
        version = deployment_target.replace("_", ".")
    else:
        parts = _platform_parts()
        version = (
            parts[1].replace("_", ".")
            if len(parts) >= 3 and parts[0] == "macosx" else ""
        )

    return f"{zig_arch}-macos.{version}" if version else f"{zig_arch}-macos"


def _windows_target() -> str | None:
    return {
        "win32": "x86-windows-msvc",
        "win-arm64": "aarch64-windows-msvc",
    }.get(sysconfig.get_platform())


def _zig_target() -> str | None:
    if sys.platform == "darwin":
        return _macos_target()
    if sys.platform == "win32":
        return _windows_target()
    return None


def _zig_compile_args() -> list[str]:
    args = ["-O", "ReleaseFast", "-I", "src"]
    target = _zig_target()
    if target is not None:
        args.extend(["-target", target])
    return args


setup(
    build_zig=True,
    ext_modules=[
        Extension(
            "c_uuid_v7._core",
            ["src/lib.zig"],
            extra_compile_args=_zig_compile_args(),
            libraries=["bcrypt"] if sys.platform == "win32" else [],
        ),
    ],
)
