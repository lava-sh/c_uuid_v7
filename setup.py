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
    link_args = []

    for library_dir in (
        sysconfig.get_config_var("LIBDIR"),
        sysconfig.get_config_var("LIBPL"),
    ):
        if library_dir and library_dir not in link_args:
            link_args.extend(["-L", library_dir])

    if sys.platform == "win32":
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


class _ZigBuildExt(build_ext):
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

        build_temp = Path(self.build_temp).resolve()
        build_temp.mkdir(parents=True, exist_ok=True)

        command = [
            sys.executable,
            "-m",
            "ziglang",
            "build-lib",
            zig_sources[0],
            "-dynamic",
            "-lc",
            "-femit-bin=" + str(ext_path),
            "--cache-dir",
            str(build_temp / "zig-cache"),
            "--global-cache-dir",
            str(build_temp / "zig-global-cache"),
            "-O",
            "Debug" if self.debug else "ReleaseFast",
        ]

        for include_dir in _python_include_dirs():
            command.extend(["-I", include_dir])

        for include_dir in ext.include_dirs or []:
            command.extend(["-I", include_dir])

        command.extend(_python_link_args())

        command.extend(f"-l{library}" for library in ext.libraries or [])

        command.extend(ext.extra_compile_args or [])

        self.spawn(command)


setup(
    cmdclass={"build_ext": _ZigBuildExt},
    ext_modules=[
        Extension(
            "c_uuid_v7._core",
            ["src/lib.zig"],
        ),
    ],
)
