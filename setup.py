import os
import re
import subprocess
import sys
import sysconfig
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

    if sys.platform != "win32":
        return []

    link_args = []

    for library_dir in (
        sysconfig.get_config_var("LIBDIR"),
        sysconfig.get_config_var("LIBPL"),
        str(Path(sys.base_prefix) / "libs"),
        str(Path(sys.exec_prefix) / "libs"),
    ):
        if library_dir and library_dir not in link_args:
            link_args.extend(["-L", library_dir])

    py_version = sysconfig.get_config_var("py_version_nodot")
    if py_version:
        link_args.append(f"-lpython{py_version}")
        return link_args

    for library in (
        sysconfig.get_config_var("LDLIBRARY"),
        sysconfig.get_config_var("LIBRARY"),
    ):
        library_name = _library_name(library)
        if library_name:
            link_args.append(f"-l{library_name}")
            break

    return link_args


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
    def _build_zig_extension(
        self,
        zig_source: str,
        ext: Extension,
        ext_path: Path,
        *,
        target: str | None = None,
    ) -> None:
        build_temp = Path(self.build_temp).resolve()
        build_temp.mkdir(parents=True, exist_ok=True)

        cache_suffix = target or "native"
        cache_suffix = cache_suffix.replace("-", "_").replace(".", "_")

        command = [
            sys.executable,
            "-m",
            "ziglang",
            "build-lib",
            zig_source,
            "-dynamic",
            "-lc",
            "-femit-bin=" + str(ext_path),
            "--cache-dir",
            str(build_temp / f"zig-cache-{cache_suffix}"),
            "--global-cache-dir",
            str(build_temp / f"zig-global-cache-{cache_suffix}"),
            "-O",
            "Debug" if self.debug else "ReleaseFast",
        ]

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

        self.spawn(command)

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

        macos_targets = _macos_targets()
        if len(macos_targets) <= 1:
            self._build_zig_extension(
                zig_sources[0],
                ext,
                ext_path,
                target=macos_targets[0] if macos_targets else None,
            )
            return

        slice_paths = []
        for target in macos_targets:
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
