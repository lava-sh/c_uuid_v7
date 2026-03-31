import os
import sys
import sysconfig

from setuptools import Extension, setup


def _macos_arch() -> str | None:
    archflags = os.environ.get("ARCHFLAGS", "").split()

    for index, flag in enumerate(archflags[:-1]):
        if flag == "-arch":
            return archflags[index + 1]

    platform_name = sysconfig.get_platform()
    parts = platform_name.split("-")
    if len(parts) >= 3 and parts[0] == "macosx":
        return parts[-1]

    return None


def _macos_target() -> str | None:
    arch = _macos_arch()
    if arch is None:
        return None

    zig_arch = {
        "x86_64": "x86_64",
        "arm64": "aarch64",
    }.get(arch)
    if zig_arch is None:
        return None

    deployment_target = os.environ.get("MACOSX_DEPLOYMENT_TARGET")
    if deployment_target:
        return f"{zig_arch}-macos.{deployment_target.replace('_', '.')}"

    platform_name = sysconfig.get_platform()
    parts = platform_name.split("-")
    if len(parts) >= 3 and parts[0] == "macosx":
        return f"{zig_arch}-macos.{parts[1].replace('_', '.')}"

    return f"{zig_arch}-macos"


def _zig_compile_args() -> list[str]:
    args = ["-O", "ReleaseFast", "-I", "src"]

    if sys.platform == "darwin":
        target = _macos_target()
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
