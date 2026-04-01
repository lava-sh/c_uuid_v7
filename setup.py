import os
import re
import subprocess
import sys
import sysconfig
from copy import copy
from dataclasses import dataclass
from pathlib import Path

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext


def _unique(items: list[str]) -> list[str]:
    return list(dict.fromkeys(items))


def _library_name(filename: str | None) -> str | None:
    if not filename:
        return None

    name = Path(filename).name

    if name.endswith(".lib"):
        return Path(name).stem

    suffixes = (".a", ".so", ".dylib", ".dll")
    for suffix in suffixes:
        if name.endswith(suffix):
            name = name.removesuffix(suffix)
            break

    if name.startswith("lib"):
        name = name.removeprefix("lib")

    return name or None


def _python_include_dirs() -> list[str]:
    include_dirs = []

    for path in (
        sysconfig.get_path("include"),
        sysconfig.get_path("platinclude"),
        sysconfig.get_config_var("INCLUDEPY"),
    ):
        if path and path not in include_dirs:
            include_dirs.append(path)

    return include_dirs


def _windows_python_library_dirs() -> list[str]:
    library_dirs = []

    for library_dir in (
        sysconfig.get_config_var("LIBDIR"),
        sysconfig.get_config_var("LIBPL"),
        str(Path(sys.base_prefix) / "libs"),
        str(Path(sys.exec_prefix) / "libs"),
    ):
        if (
            library_dir
            and Path(library_dir).exists()
            and library_dir not in library_dirs
        ):
            library_dirs.append(library_dir)

    return library_dirs


def _windows_python_library_name() -> str | None:
    py_version = sysconfig.get_config_var("py_version_nodot")
    if py_version:
        return f"python{py_version}"

    for library in (
        sysconfig.get_config_var("LDLIBRARY"),
        sysconfig.get_config_var("LIBRARY"),
    ):
        library_name = _library_name(library)
        if library_name:
            return library_name

    return None


def _windows_system_libraries() -> list[str]:
    return [
        "advapi32",
        "bcrypt",
        "kernel32",
        "ntdll",
        "user32",
    ]


def _python_link_args() -> list[str]:
    if sys.platform == "darwin":
        return ["-fallow-shlib-undefined"]

    return []


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

    platform_tag = sysconfig.get_platform()

    if sys.platform == "win32":
        windows_target = {
            "win-amd64": "x86_64-windows-msvc",
            "win-arm64": "aarch64-windows-msvc",
            "win32": "x86-windows-msvc",
        }.get(platform_tag)
        return [windows_target] if windows_target is not None else []

    if sys.platform != "linux":
        return []

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
    if target is None:
        return "ReleaseFast"

    problematic_targets = (
        "powerpc64le-linux-gnu",
        "x86-windows-msvc",
    )
    if target in problematic_targets:
        return "ReleaseSafe"

    return "ReleaseFast"


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


@dataclass(frozen=True, slots=True)
class _WindowsBuildSettings:
    library_dirs: list[str]
    libraries: list[str]


def _windows_build_settings(ext: Extension) -> _WindowsBuildSettings:
    python_library = _windows_python_library_name()
    libraries = _windows_system_libraries()
    if python_library is not None:
        libraries.insert(0, python_library)

    return _WindowsBuildSettings(
        library_dirs=_unique(
            [
                *(_windows_python_library_dirs()),
                *(ext.library_dirs or []),
            ],
        ),
        libraries=_unique([*libraries, *(ext.libraries or [])]),
    )


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

        cache_suffix = target or "native"
        cache_suffix = cache_suffix.replace("-", "_").replace(".", "_")

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

        command.extend(_python_link_args())
        command.extend(f"-l{library}" for library in ext.libraries or [])
        command.extend(ext.extra_link_args or [])
        command.extend(ext.extra_compile_args or [])

        return command

    def _build_zig_extension(
        self,
        zig_source: str,
        ext: Extension,
        ext_path: Path,
        *,
        target: str | None = None,
    ) -> None:
        self.spawn(
            self._zig_command(
                "build-lib",
                zig_source,
                ext_path,
                ext,
                target=target,
            ),
        )

    @staticmethod
    def _windows_zig_extension(ext: Extension) -> Extension:
        windows_ext = copy(ext)
        settings = _windows_build_settings(ext)
        windows_ext.library_dirs = settings.library_dirs
        windows_ext.libraries = settings.libraries
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
            self._build_zig_extension(
                zig_sources[0],
                ext,
                ext_path,
                target=platform_targets[0] if platform_targets else None,
            )
            return

        slice_paths = []
        for target in platform_targets:
            target_suffix = target.replace("-", "_").replace(".", "_")
            slice_path = ext_path.with_name(
                f"{ext_path.stem}.{target_suffix}{ext_path.suffix}",
            )
            self._build_zig_extension(
                zig_sources[0],
                ext,
                slice_path,
                target=target,
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
