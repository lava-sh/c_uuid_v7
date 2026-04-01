import os
import re
import subprocess
import sys
import sysconfig
from copy import copy
from pathlib import Path

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext


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

    linux_target = {
        "linux-aarch64": "aarch64-linux-gnu",
        "linux-armv7l": "arm-linux-gnueabihf",
        "linux-i686": "x86-linux-gnu",
        "linux-ppc64le": "powerpc64le-linux-gnu",
        "linux-s390x": "s390x-linux-gnu",
        "linux-x86_64": "x86_64-linux-gnu",
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


class _ZigBuildExt(build_ext):
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

        for include_dir in _python_include_dirs():
            command.extend(["-I", include_dir])

        for include_dir in ext.include_dirs or []:
            command.extend(["-I", include_dir])

        command.extend(_python_link_args())
        command.extend(f"-l{library}" for library in ext.libraries or [])
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

    def _build_windows_extension(
        self,
        zig_source: str,
        ext: Extension,
        ext_path: Path,
        *,
        target: str | None = None,
    ) -> None:
        build_temp = Path(self.build_temp).resolve()
        build_temp.mkdir(parents=True, exist_ok=True)
        object_path = build_temp / f"{ext.name.rsplit('.', 1)[-1]}.obj"

        self.spawn(
            self._zig_command(
                "build-obj",
                zig_source,
                object_path,
                ext,
                target=target,
            ),
        )

        windows_ext = copy(ext)
        windows_ext.sources = []
        windows_ext.extra_objects = [str(object_path), *(ext.extra_objects or [])]
        windows_ext.extra_compile_args = []

        super().build_extension(windows_ext)

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

        platform_targets = _platform_targets()
        if sys.platform == "win32":
            self._build_windows_extension(
                zig_sources[0],
                ext,
                ext_path,
                target=platform_targets[0] if platform_targets else None,
            )
            return

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
