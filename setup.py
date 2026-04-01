import os
import re
import subprocess
import sys
import sysconfig
from copy import copy
from pathlib import Path

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext

WINDOWS_TARGETS = {
    "win-amd64": "x86_64-windows-msvc",
    "win-arm64": "aarch64-windows-msvc",
    "win32": "x86-windows-msvc",
}
WINDOWS_SYSTEM_LIBRARIES = ("advapi32", "bcrypt", "kernel32", "ntdll", "user32")


def _unique(items: list[str | None], *, existing_only: bool = False) -> list[str]:
    result = []
    for item in items:
        if not item or item in result:
            continue
        if existing_only and not Path(item).exists():
            continue
        result.append(item)
    return result


def _python_include_dirs() -> list[str]:
    return _unique(
        [
            sysconfig.get_path("include"),
            sysconfig.get_path("platinclude"),
            sysconfig.get_config_var("INCLUDEPY"),
        ],
    )


def _python_header_path() -> Path | None:
    for include_dir in _python_include_dirs():
        header_path = Path(include_dir) / "Python.h"
        if header_path.exists():
            return header_path
    return None


def _windows_python_info(ext: Extension) -> tuple[list[str], list[str]]:
    py_version = sysconfig.get_config_var("py_version_nodot")
    python_library = f"python{py_version}" if py_version else None
    if python_library is None:
        for filename in (
            sysconfig.get_config_var("LDLIBRARY"),
            sysconfig.get_config_var("LIBRARY"),
        ):
            if not filename:
                continue
            name = Path(filename).name.removeprefix("lib")
            for suffix in (".lib", ".a", ".so", ".dylib", ".dll"):
                if name.endswith(suffix):
                    name = name.removesuffix(suffix)
                    break
            if name:
                python_library = name
                break

    library_dirs = _unique(
        [
            sysconfig.get_config_var("LIBDIR"),
            sysconfig.get_config_var("LIBPL"),
            str(Path(sys.base_prefix) / "libs"),
            str(Path(sys.exec_prefix) / "libs"),
            *(ext.library_dirs or []),
        ],
        existing_only=True,
    )
    libraries = _unique(
        [
            python_library,
            *WINDOWS_SYSTEM_LIBRARIES,
            *(ext.libraries or []),
        ],
    )
    return library_dirs, libraries


def _macos_targets() -> list[str]:
    if sys.platform != "darwin":
        return []

    deployment_target = os.environ.get("MACOSX_DEPLOYMENT_TARGET")
    platform_tag = sysconfig.get_platform()
    if not deployment_target:
        match = re.match(r"macosx-(\d+(?:[._]\d+)*)-(.+)", platform_tag)
        if match is None:
            return []
        deployment_target = match.group(1).replace("_", ".")

    archflags = os.environ.get("ARCHFLAGS", "")
    arch_matches = re.findall(r"-arch\s+(\S+)", archflags)
    if not arch_matches:
        platform_arch = platform_tag.rsplit("-", 1)[-1]
        if (
            platform_arch == "universal2"
            and "CIBW_BUILD" not in os.environ
            and "CIBUILDWHEEL" not in os.environ
        ):
            platform_arch = os.uname().machine
        arch_matches = {
            "arm64": ["arm64"],
            "universal2": ["x86_64", "arm64"],
            "x86_64": ["x86_64"],
        }.get(platform_arch, [os.uname().machine])

    targets = []
    normalized_target = deployment_target.replace("_", ".")

    def _version_tuple(version: str) -> tuple[int, ...]:
        return tuple(int(part) for part in version.split("."))

    for arch in arch_matches:
        zig_arch = {
            "arm64": "aarch64",
            "aarch64": "aarch64",
            "x86_64": "x86_64",
        }.get(arch)
        if zig_arch is None:
            msg = f"Unsupported macOS arch in ARCHFLAGS: {arch}"
            raise ValueError(msg)
        target_version = normalized_target
        if zig_arch == "aarch64" and _version_tuple(target_version) < (11, 0):
            target_version = "11.0"
        targets.append(f"{zig_arch}-macos.{target_version}")

    return targets


def _platform_targets() -> list[str]:
    if sys.platform == "darwin":
        return _macos_targets()

    if sys.platform == "win32":
        windows_target = WINDOWS_TARGETS.get(sysconfig.get_platform())
        return [windows_target] if windows_target is not None else []

    if sys.platform != "linux":
        return []

    platform_tag = sysconfig.get_platform()
    auditwheel_platform = os.environ.get("AUDITWHEEL_PLAT", "")
    host_gnu_type = sysconfig.get_config_var("HOST_GNU_TYPE") or ""
    linux_abi = "musl" if (
        auditwheel_platform.startswith("musllinux_") or host_gnu_type.endswith("musl")
    ) else "gnu"

    linux_target = {
        "linux-aarch64": f"aarch64-linux-{linux_abi}",
        "linux-armv7l": (
            "arm-linux-musleabihf"
            if linux_abi == "musl"
            else "arm-linux-gnueabihf"
        ),
        "linux-i686": f"x86-linux-{linux_abi}",
        "linux-ppc64le": f"powerpc64le-linux-{linux_abi}",
        "linux-s390x": f"s390x-linux-{linux_abi}",
        "linux-x86_64": f"x86_64-linux-{linux_abi}",
    }.get(platform_tag)
    return [linux_target] if linux_target is not None else []


def _zig_optimize_mode(target: str | None) -> str:
    return (
        "ReleaseSafe"
        if target in {"powerpc64le-linux-gnu", "x86-windows-msvc"}
        else "ReleaseFast"
    )


def _macos_sdk_path() -> str | None:
    if sys.platform != "darwin":
        return None

    sdkroot = os.environ.get("SDKROOT")
    if sdkroot:
        return sdkroot

    try:
        return subprocess.check_output(
            ["xcrun", "--show-sdk-path"],
            text=True,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return None


class _ZigBuildExt(build_ext):
    @staticmethod
    def _add_prefixed_args(
        command: list[str],
        prefix: str,
        values: list[str] | None,
    ) -> None:
        for value in values or []:
            command.extend([prefix, value])

    def _zig_command(
        self,
        action: str,
        zig_source: str,
        output_path: Path,
        ext: Extension,
        *,
        target: str | None = None,
    ) -> list[str]:
        build_temp = Path(self.build_temp).resolve()
        build_temp.mkdir(parents=True, exist_ok=True)

        cache_suffix = (target or "native").replace("-", "_").replace(".", "_")
        command = [
            sys.executable,
            "-m",
            "ziglang",
            action,
            zig_source,
            "-lc",
            "-femit-bin=" + str(output_path),
            "--cache-dir",
            str(build_temp / f"zig-cache-{cache_suffix}"),
            "--global-cache-dir",
            str(build_temp / f"zig-global-cache-{cache_suffix}"),
            "-O",
            "Debug" if self.debug else _zig_optimize_mode(target),
        ]
        if action == "build-lib":
            command.append("-dynamic")
        if target is not None:
            command.extend(["-target", target])

        sdk_path = _macos_sdk_path()
        if sdk_path is not None:
            command.extend(["--sysroot", sdk_path])
            command.extend(["-I", str(Path(sdk_path) / "usr" / "include")])

        self._add_prefixed_args(
            command,
            "-I",
            [*_python_include_dirs(), *(ext.include_dirs or [])],
        )
        self._add_prefixed_args(command, "-L", ext.library_dirs)

        if sys.platform == "darwin":
            command.append("-fallow-shlib-undefined")
        command.extend(f"-l{library}" for library in ext.libraries or [])
        command.extend(ext.extra_link_args or [])
        command.extend(ext.extra_compile_args or [])

        return command

    @staticmethod
    def _windows_zig_extension(ext: Extension) -> Extension:
        windows_ext = copy(ext)
        windows_ext.library_dirs, windows_ext.libraries = _windows_python_info(ext)
        return windows_ext

    def build_extension(self, ext: Extension) -> None:
        zig_sources = [source for source in ext.sources if source.endswith(".zig")]

        if not zig_sources:
            super().build_extension(ext)
            return

        if len(zig_sources) != 1 or len(zig_sources) != len(ext.sources):
            msg = "Zig extensions must declare exactly one .zig source file"
            raise ValueError(msg)

        ext_path = Path(self.get_ext_fullpath(ext.name)).resolve()
        ext_path.parent.mkdir(parents=True, exist_ok=True)

        if sys.platform == "win32":
            ext = self._windows_zig_extension(ext)

        platform_targets = _platform_targets()
        if len(platform_targets) <= 1:
            self.spawn(
                self._zig_command(
                    "build-lib",
                    zig_sources[0],
                    ext_path,
                    ext,
                    target=platform_targets[0] if platform_targets else None,
                ),
            )
            return

        slice_paths = []
        for target in platform_targets:
            target_suffix = target.replace("-", "_").replace(".", "_")
            slice_path = ext_path.with_name(
                f"{ext_path.stem}.{target_suffix}{ext_path.suffix}",
            )
            self.spawn(
                self._zig_command(
                    "build-lib",
                    zig_sources[0],
                    slice_path,
                    ext,
                    target=target,
                ),
            )
            slice_paths.append(slice_path)

        self.spawn(
            [
                "lipo",
                "-create",
                *map(str, slice_paths),
                "-output",
                str(ext_path),
            ],
        )


setup(
    cmdclass={"build_ext": _ZigBuildExt},
    ext_modules=[
        Extension(
            "c_uuid_v7._core",
            ["src/lib.zig"],
        ),
    ],
)
