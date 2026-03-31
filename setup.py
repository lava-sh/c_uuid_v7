import os
import sys
import sysconfig
from pathlib import Path

from setuptools import Extension, setup
from setuptools_zig import BuildExt as ZigBuildExt


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


class _PatchedZigBuildExt(ZigBuildExt):
    def build_extension(self, ext: Extension) -> None:
        if sys.platform == "win32":
            python_lib_name = (
                f"python{sys.version_info.major}{sys.version_info.minor}.lib"
            )
            sanitized_library_dirs: list[str] = []

            for raw_dir in self.compiler.library_dirs:
                library_dir = Path(raw_dir)
                dll_path = library_dir / python_lib_name.replace(".lib", ".dll")

                if dll_path.exists() and not (library_dir / python_lib_name).exists():
                    continue

                sanitized_library_dirs.append(raw_dir)

            self.compiler.library_dirs = sanitized_library_dirs

        super().build_extension(ext)


class _PatchedZigBuildExtension:
    def __init__(self, *, enabled: bool) -> None:
        self._enabled = enabled

    def __call__(self, dist: object) -> _PatchedZigBuildExt:
        return _PatchedZigBuildExt(dist, zig_value=self._enabled)


setup(
    cmdclass={"build_ext": _PatchedZigBuildExtension(enabled=True)},
    ext_modules=[
        Extension(
            "c_uuid_v7._core",
            ["src/lib.zig"],
            extra_compile_args=_zig_compile_args(),
            libraries=["bcrypt"] if sys.platform == "win32" else [],
        ),
    ],
)
