import glob
import logging
import os
import shlex
import shutil
import subprocess
import sys
import sysconfig
from dataclasses import dataclass
from pathlib import Path

from setuptools import find_packages, setup
from setuptools.command.build_ext import build_ext
from setuptools.extension import Extension

logging.basicConfig(
    level=logging.INFO,
    format="%(message)s",
)
logger = logging.getLogger(__name__)


@dataclass(frozen=True)
class PlatformSpec:
    target: str | None = None
    arch_macro: str | None = None
    extra_libs: tuple[str, ...] = ("advapi32", "Mincore")


WINDOWS_PLATFORMS = {
    "win32": PlatformSpec("x86-windows-msvc", "-D_X86_"),
    "win-amd64": PlatformSpec("x86_64-windows-msvc", "-D_AMD64_"),
    "win-arm64": PlatformSpec("aarch64-windows-msvc", "-D_ARM64_"),
}


def _split_flags(value: str | None) -> list[str]:
    return shlex.split(value, posix=os.name != "nt") if value else []


def _unique_paths(
    paths: list[str | Path | None],
    *,
    existing_only: bool = False,
) -> list[str]:
    seen = set()
    result = []
    for raw in paths:
        if not raw:
            continue
        path = Path(raw)
        if existing_only and not path.is_dir():
            continue
        value = str(path)
        if value not in seen:
            seen.add(value)
            result.append(value)
    return result


@dataclass(frozen=True)
class BuildSpec:
    zig: str
    ext: Extension
    ext_path: Path
    platform: PlatformSpec
    debug: bool

    def macro_flags(self) -> list[str]:
        flags = [
            f"-D{name}" if value is None else f"-D{name}={value}"
            for name, value in self.ext.define_macros or []
        ]
        flags.extend(f"-U{name}" for name in self.ext.undef_macros or [])
        return flags

    def include_dirs(self) -> list[str]:
        return _unique_paths([
            *(self.ext.include_dirs or []),
            sysconfig.get_path("include"),
            sysconfig.get_path("platinclude"),
            sysconfig.get_config_var("INCLUDEPY"),
        ])

    def library_dirs(self) -> list[str]:
        roots = _unique_paths(
            [
                sys.prefix,
                sys.base_prefix,
                sys.exec_prefix,
                sys.base_exec_prefix,
            ],
            existing_only=True,
        )
        bases = [*roots, *(self.ext.library_dirs or [])]
        return _unique_paths(
            [*(f"{root}/{sub}" for root in bases for sub in ("libs", "Libs"))],
            existing_only=True,
        )

    def python_lib(self) -> str | None:
        version = sysconfig.get_python_version().replace(".", "")
        roots = _unique_paths(
            [
                sys.prefix,
                sys.base_prefix,
                sys.exec_prefix,
                sys.base_exec_prefix,
                Path(sys.executable).resolve().parent.parent,
            ],
            existing_only=True,
        )
        for root in roots:
            for sub in ("libs", "Libs", ""):
                base = f"{root}/{sub}" if sub else root
                for stem in (f"python{version}", "python3"):
                    candidate = Path(f"{base}/{stem}.lib")
                    if candidate.is_file():
                        return str(candidate)
        return None

    def libraries(self) -> list[str]:
        libs = list(self.ext.libraries or [])

        if sys.platform == "win32" and any(
            Path(source).name == "windows.c" for source in self.ext.sources
        ):
            libs.extend(self.platform.extra_libs)

        return list(dict.fromkeys(libs))

    def command(self) -> list[str]:
        cmd = [self.zig, "cc"]

        if self.platform.target:
            cmd.extend(["-target", self.platform.target])
        cmd.extend(["-O0", "-g"] if self.debug else ["-O3", "-DNDEBUG", "-s"])

        if self.platform.arch_macro:
            cmd.append(self.platform.arch_macro)
        cmd.extend(["-Wno-empty-translation-unit", "-shared"])
        cmd.extend(self.macro_flags())

        for path in self.include_dirs():
            cmd.extend(["-I", path])

        cmd.extend(_split_flags(os.environ.get("CFLAGS")))
        cmd.extend(str(Path(source)) for source in self.ext.sources)

        for path in self.library_dirs():
            cmd.extend(["-L", path])

        if python_lib := self.python_lib():
            cmd.append(python_lib)

        cmd.extend(f"-l{lib}" for lib in self.libraries())
        cmd.extend(_split_flags(os.environ.get("LDFLAGS")))
        cmd.extend(str(arg) for arg in self.ext.extra_compile_args or [])
        cmd.extend(str(arg) for arg in self.ext.extra_link_args or [])
        cmd.extend(["-o", str(self.ext_path)])
        return cmd

    def run(self) -> None:
        cmd = self.command()

        logger.info("⚙️ Running: %s", shlex.join(cmd))

        completed = subprocess.run(
            cmd,
            text=True,
            capture_output=True,
            check=False,

)
        if completed.returncode == 0:
            if completed.stdout:
                sys.stdout.write(completed.stdout)
            if completed.stderr:
                sys.stderr.write(completed.stderr)
            logger.info("✅ Build succeeded")
            return

        lines = [
            f"❌ zig cc failed for target {self.platform.target or '<default>'}",
            f"command: {shlex.join(cmd)}",
            f"exit code: {completed.returncode}",
        ]

        if completed.stdout:
            lines.extend(["\n📤 stdout:", completed.stdout.rstrip()])

        if completed.stderr:
            lines.extend(["\n📥 stderr:", completed.stderr.rstrip()])

        raise RuntimeError("\n".join(lines))

    def cleanup(self) -> None:
        for name in (
            "lib.lib",
            "lib.exp",
            f"{self.ext_path.stem}.lib",
            f"{self.ext_path.stem}.exp",
        ):
            artifact = self.ext_path.parent / name
            if artifact.exists():
                artifact.unlink()


class ZigBuildExt(build_ext):
    def build_extension(self, ext: Extension) -> None:
        # Fix for Windows ARM64
        # See: https://codeberg.org/ziglang/zig/issues/31865#issuecomment-13204506
        ci_zig = os.environ.get("CI_WIN_ARM_64_ZIG_ENV")

        # `python-zig` is Zig from PyPi (https://pypi.org/project/ziglang)
        zig = ci_zig or shutil.which("python-zig")
        logger.info("⚡ Using Zig: %s", zig)

        if zig is None:
            logger.info("⚠️ Zig not found, fallback to setuptools (GCC / Clang)")
            super().build_extension(ext)
            return

        if sys.platform == "darwin":
            logger.info("🍎 macOS detected: using Clang toolchain")
            super().build_extension(ext)
            return

        ext_path = Path(self.get_ext_fullpath(ext.name))
        ext_path.parent.mkdir(parents=True, exist_ok=True)
        plat_name = self.plat_name or getattr(self, "get_platform")()
        build = BuildSpec(
            zig=zig,
            ext=ext,
            ext_path=ext_path,
            platform=WINDOWS_PLATFORMS.get(plat_name, PlatformSpec()),
            debug=self.debug,
        )
        build.run()
        build.cleanup()


setup(
    cmdclass={"build_ext": ZigBuildExt},
    package_dir={"": "py-src"},
    packages=find_packages(where="py-src"),
    ext_modules=[
        Extension(
            name="c_uuid_v7._core",
            sources=glob.glob("src/**/*.c", recursive=True),
        ),
    ],
)
