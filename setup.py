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

IS_WINDOWS = sys.platform == "win32"
IS_MACOS = sys.platform == "darwin"

_RELEASE_FLAGS: tuple[str, ...] = (
    # Optimization level
    # https://clang.llvm.org/docs/CommandGuide/clang.html#cmdoption-O0
    "-O3",

    # Link Time Optimization
    # https://clang.llvm.org/docs/CommandGuide/clang.html#cmdoption-flto
    "-flto=full",

    # Disables asserts and other debug-only code
    # https://en.cppreference.com/w/c/error/assert
    "-DNDEBUG",

    # Place each global variable in its own section
    # https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html#index-fdata-sections
    "-fdata-sections",

    # Suppress visibility-related warnings
    # https://clang.llvm.org/docs/DiagnosticsReference.html
    "-Wno-visibility",

    # Place each function in its own section
    # https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html#index-ffunction-sections
    "-ffunction-sections",

    # Omit frame pointer
    # https://clang.llvm.org/docs/ClangCommandLineReference.html#cmdoption-clang-fomit-frame-pointer
    "-fomit-frame-pointer",
)  # fmt: off

if IS_WINDOWS:
    RELEASE_FLAGS = (
        *_RELEASE_FLAGS,

        # -s -> remove all symbols
        # https://sourceware.org/binutils/docs/ld/Options.html
        "-s",
    )  # fmt: off

else:
    RELEASE_FLAGS = (
        *_RELEASE_FLAGS,
        # Disable semantic interposition:
        # allows inlining and direct calls instead of PLT indirection
        # https://clang.llvm.org/docs/ClangCommandLineReference.html#cmdoption-fsemantic-interposition
        "-fno-semantic-interposition",

        # Linker flags:
        # --gc-sections -> remove unused sections
        # --as-needed   -> link only required libraries
        # --strip-all   -> remove all symbols
        # https://sourceware.org/binutils/docs/ld/Options.html
        "-Wl,--gc-sections,--as-needed,--strip-all",
    )  # fmt: off


@dataclass(frozen=True)
class PlatformSpec:
    target: str | None = None
    arch_macro: str | None = None
    extra_libs: tuple[str, ...] = ("advapi32", "Mincore")
    debug_flags: tuple[str, ...] = ("-O0", "-g")
    release_flags: tuple[str, ...] = RELEASE_FLAGS


PLATFORMS = {
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
    return list(dict.fromkeys(
        str(path)
        for p in paths
        if p is not None
        for path in (Path(p),)
        if not (existing_only and not path.is_dir())
    ))  # fmt: off


@dataclass(frozen=True)
class BuildSpec:
    zig: str
    ext: Extension
    ext_path: Path
    platform: PlatformSpec
    debug: bool

    def macro_flags(self) -> list[str]:
        flags = [
            f"-D{name}"
            if value is None
            else f"-D{name}={value}"
            for name, value in self.ext.define_macros or []
        ]  # fmt: off
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
        if not IS_WINDOWS:
            return None

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

        if IS_WINDOWS and any(
            Path(source).name == "windows.c"
            for source in self.ext.sources
        ):  # fmt: off
            libs.extend(self.platform.extra_libs)

        return list(dict.fromkeys(libs))

    def command(self) -> list[str]:
        cmd = [self.zig, "cc"]

        if self.platform.target:
            cmd.extend(["-target", self.platform.target])

        if self.debug:
            cmd.extend(self.platform.debug_flags)
        else:
            cmd.extend(self.platform.release_flags)

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

        logger.info("📂 Include dirs: %s", self.include_dirs())
        logger.info("📂 Library dirs: %s", self.library_dirs())
        logger.info("⚙️ Running: %s", shlex.join(cmd))
        logger.info(
            "📄 Sources (%d): %s",
            len(self.ext.sources),
            list(self.ext.sources),
        )

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

        if IS_MACOS:
            logger.info("🍏 macOS detected: using Clang toolchain")
            logger.info(
                "🍏 MACOSX_DEPLOYMENT_TARGET=%s",
                os.environ.get("MACOSX_DEPLOYMENT_TARGET"),
            )
            super().build_extension(ext)
            return

        if zig is None:
            logger.info("⚠️ Zig not found, fallback to setuptools (GCC / Clang)")
            super().build_extension(ext)
            return

        logger.info("⚡ Using Zig: %s", zig)

        ext_path = Path(self.get_ext_fullpath(ext.name))
        ext_path.parent.mkdir(parents=True, exist_ok=True)
        plat_name = self.plat_name or getattr(self, "get_platform")()
        build = BuildSpec(
            zig=zig,
            ext=ext,
            ext_path=ext_path,
            platform=PLATFORMS.get(plat_name, PlatformSpec()),
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
