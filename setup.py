# ruff: noqa: SLF001
import sys
import subprocess
import sysconfig
from pathlib import Path
from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext

WINDOWS_TARGETS = {
    "win-amd64": "x86_64-windows-msvc",
    "win-arm64": "aarch64-windows-msvc",
    "win32": "x86-windows-msvc",
}
WINDOWS_LIBRARIES = ["advapi32", "kernel32", "ntdll", "user32"]


class ZigBuildExt(build_ext):
    """Build Zig objects before HPy compilation."""

    def build_extension(self, ext):
        zig_sources = [Path(s) for s in ext.sources if str(s).endswith(".zig")]

        if zig_sources:
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
                f"-femit-bin={object_path}",
                "--cache-dir",
                str(build_temp / "zig-cache"),
                "--global-cache-dir",
                str(build_temp / "zig-global-cache"),
                "-O",
                "Debug" if self.debug else "ReleaseFast",
            ]

            if sys.platform != "win32":
                command.append("-fPIC")

            if sys.platform == "win32":
                target = WINDOWS_TARGETS.get(sysconfig.get_platform())
                if target:
                    command.extend(["-target", target])

            self.spawn(command)

            # Remove .zig from sources, add compiled object
            ext.sources = [s for s in ext.sources if not str(s).endswith(".zig")]
            ext.extra_objects = [*getattr(ext, "extra_objects", []), str(object_path)]

        super().build_extension(ext)


setup(
    cmdclass={"build_ext": ZigBuildExt},
    hpy_ext_modules=[
        Extension(
            "c_uuid_v7._core",
            ["src/hpy_core.c", "src/lib.zig"],
            include_dirs=["src"],
            libraries=WINDOWS_LIBRARIES if sys.platform == "win32" else [],
        ),
    ],
)
