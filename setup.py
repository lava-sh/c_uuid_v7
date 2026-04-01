import sys
import sysconfig
from pathlib import Path

from hpy.devel import HPyDevel
from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext

WINDOWS_TARGETS = {
    "win-amd64": "x86_64-windows-msvc",
    "win-arm64": "aarch64-windows-msvc",
    "win32": "x86-windows-msvc",
}


class ZigBuildExt(build_ext):
    def build_extension(self, ext: Extension) -> None:
        zig_sources = [Path(source) for source in ext.sources if source.endswith(".zig")]

        if not zig_sources:
            super().build_extension(ext)
            return

        if len(zig_sources) != 1:
            msg = "Zig extensions must declare exactly one .zig source file"
            raise ValueError(msg)

        build_temp = Path(self.build_temp).resolve()
        build_temp.mkdir(parents=True, exist_ok=True)

        object_suffix = ".obj" if sys.platform == "win32" else ".o"
        object_path = build_temp / f"{ext.name.rsplit('.', 1)[-1]}_zig{object_suffix}"
        command = [
            sys.executable,
            "-m",
            "ziglang",
            "build-obj",
            str(zig_sources[0]),
            "-femit-bin=" + str(object_path),
            "--cache-dir",
            str(build_temp / "zig-cache"),
            "--global-cache-dir",
            str(build_temp / "zig-global-cache"),
            "-O",
            "Debug" if self.debug else "ReleaseFast",
        ]
        if sys.platform == "win32":
            target = WINDOWS_TARGETS.get(sysconfig.get_platform())
            if target is not None:
                command.extend(["-target", target])

        self.spawn(command)

        ext.sources = [source for source in ext.sources if not source.endswith(".zig")]
        ext.extra_objects = [*(ext.extra_objects or []), str(object_path)]
        super().build_extension(ext)


hpy_devel = HPyDevel()

setup(
    cmdclass={"build_ext": ZigBuildExt},
    hpy_ext_modules=[
        Extension(
            "c_uuid_v7._core",
            ["src/hpy_core.c", "src/lib.zig"],
            include_dirs=["src"],
            libraries=["advapi32", "kernel32", "ntdll", "user32"],
        ),
    ],
)
