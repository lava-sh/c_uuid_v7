import os
import shlex
import shutil
import sys
import sysconfig
from pathlib import Path

from setuptools import setup
from setuptools.command.build_ext import build_ext
from setuptools.extension import Extension


def _split_flags(value: str | None) -> list[str]:
    return shlex.split(value, posix=os.name != "nt") if value else []


def _unique_paths(
    paths: list[str | Path | None],
    *,
    existing_only: bool = False,
) -> list[str]:
    seen = set()
    result = []
    for p in paths:
        if not p:
            continue
        resolved = Path(p)
        if existing_only and not resolved.is_dir():
            continue
        s = str(resolved)
        if s not in seen:
            seen.add(s)
            result.append(s)
    return result


def _add_include(command: list[str], ext: Extension) -> None:
    for p in _unique_paths([
        *(ext.include_dirs or []),
        sysconfig.get_path("include"),
        sysconfig.get_path("platinclude"),
        sysconfig.get_config_var("INCLUDEPY"),
    ]):
        command.extend(["-I", p])


def _add_library_dirs(command: list[str], ext: Extension) -> None:
    seen = set()
    roots = _unique_paths([
        sys.prefix,
        sys.base_prefix,
        sys.exec_prefix,
        sys.base_exec_prefix,
    ], existing_only=True)
    for p in [*roots, *(ext.library_dirs or [])]:
        for sub in ("libs", "Libs"):
            lib_dir = f"{p}/{sub}"
            if lib_dir not in seen and Path(lib_dir).is_dir():
                seen.add(lib_dir)
                command.extend(["-L", lib_dir])


def _add_libraries(command: list[str], ext: Extension) -> None:
    seen = set()
    for lib in ext.libraries or []:
        if lib not in seen:
            seen.add(lib)
            command.append(f"-l{lib}")
    if os.name == "nt" and any(Path(s).name == "windows.c" for s in ext.sources):
        for lib in ("advapi32", "Mincore"):
            if lib not in seen:
                seen.add(lib)
                command.append(f"-l{lib}")


def _find_python_lib(zig: str, target: str | None) -> str | None:
    version = sysconfig.get_python_version().replace(".", "")
    search_dirs = _unique_paths(
        [
            sys.prefix,
            sys.base_prefix,
            sys.exec_prefix,
            sys.base_exec_prefix,
            Path(sys.executable).resolve().parent.parent,
        ],
        existing_only=True,
    )
    for d in search_dirs:
        for sub in ("libs", "Libs", ""):
            base = f"{d}/{sub}" if sub else d
            for stem in (f"python{version}", "python3"):
                candidate = Path(f"{base}/{stem}.lib")
                if candidate.is_file():
                    return str(candidate)
    return None


class ZigBuildExt(build_ext):
    def build_extensions(self) -> None:
        zig = self._find_zig()
        if zig is None:
            super().build_extensions()
            return

        is_unix = os.name != "nt" and self.compiler.compiler_type == "unix"
        is_macos = sys.platform == "darwin"

        if is_unix or is_macos:
            target = self._macos_target() if is_macos else None
            prefix = [zig, "cc"] + (["-target", target] if target else [])
            cflags = [
                "-Wno-empty-translation-unit",
                "-Wno-visibility",
                "-fvisibility=hidden",
                "-O3",
            ]
            self.compiler.compiler = [*prefix]
            self.compiler.compiler_so = [*prefix, *cflags]
            self.compiler.linker_so = [*prefix, "-s"]
            self.compiler.linker_exe = [*prefix]

        super().build_extensions()

    def build_extension(self, ext: Extension) -> None:
        zig = self._find_zig()
        if zig is None:
            super().build_extension(ext)
            return

        if os.name == "nt":
            self._build_windows(ext, zig)
        elif sys.platform == "darwin":
            plat = self.plat_name or getattr(self, "get_platform")()
            if "universal2" not in plat:
                self._build_macos(ext, zig)
            else:
                super().build_extension(ext)
        else:
            super().build_extension(ext)

    def _build_windows(self, ext: Extension, zig: str) -> None:
        ext_path = Path(self.get_ext_fullpath(ext.name))
        ext_path.parent.mkdir(parents=True, exist_ok=True)

        target = self._windows_target()
        cmd: list[str] = [zig, "cc"]
        if target:
            cmd.extend(["-target", target])
        cmd.extend(self._opt_flags())
        cmd.extend(self._win_arch_macro(target))
        cmd.extend(["-Wno-empty-translation-unit", "-shared"])
        cmd.extend(self._macro_flags(ext))
        _add_include(cmd, ext)
        cmd.extend(_split_flags(os.environ.get("CFLAGS")))
        cmd.extend(str(Path(s)) for s in ext.sources)
        _add_library_dirs(cmd, ext)
        pylib = _find_python_lib(zig, target)
        if pylib:
            cmd.append(pylib)
        _add_libraries(cmd, ext)
        cmd.extend(_split_flags(os.environ.get("LDFLAGS")))
        cmd.extend(str(a) for a in ext.extra_compile_args or [])
        cmd.extend(str(a) for a in ext.extra_link_args or [])
        cmd.extend(["-o", str(ext_path)])

        self.spawn(cmd)
        self._cleanup_artifacts(ext_path)

    def _build_macos(self, ext: Extension, zig: str) -> None:
        ext_path = Path(self.get_ext_fullpath(ext.name))
        ext_path.parent.mkdir(parents=True, exist_ok=True)

        target = self._macos_target()
        cmd = [zig, "cc"] + (["-target", target] if target else [])
        cmd.extend([
            "-O3",
            "-DNDEBUG",
            "-s",
            "-Wno-empty-translation-unit",
            "-Wno-visibility",
            "-shared",
        ])
        cmd.extend(["-undefined", "dynamic_lookup"])
        cmd.extend(self._macro_flags(ext))
        _add_include(cmd, ext)
        cmd.extend(_split_flags(os.environ.get("CFLAGS")))
        cmd.extend(str(Path(s)) for s in ext.sources)
        _add_library_dirs(cmd, ext)
        _add_libraries(cmd, ext)
        cmd.extend(_split_flags(os.environ.get("LDFLAGS")))
        cmd.extend(str(a) for a in ext.extra_compile_args or [])
        cmd.extend(str(a) for a in ext.extra_link_args or [])
        cmd.extend(["-o", str(ext_path)])

        self.spawn(cmd)

    def _find_zig(self) -> str | None:
        return shutil.which("python-zig") or shutil.which("zig")

    def _windows_target(self) -> str | None:
        plat = self.plat_name or getattr(self, "get_platform")()
        return {
            "win32": "x86-windows-msvc",
            "win-amd64": "x86_64-windows-msvc",
            "win-arm64": "aarch64-windows-msvc",
        }.get(plat)

    def _win_arch_macro(self, target: str | None) -> list[str]:
        if target is None:
            return []
        macro = {
            "x86-windows-msvc": "-D_X86_",
            "x86_64-windows-msvc": "-D_AMD64_",
            "aarch64-windows-msvc": "-D_ARM64_",
        }.get(target)
        return [macro] if macro else []

    def _macos_target(self) -> str | None:
        archflags = os.environ.get("ARCHFLAGS", "")
        if "-arch arm64" in archflags:
            return "arm64-macos"
        if "-arch x86_64" in archflags:
            return "x86_64-macos"
        plat = self.plat_name or getattr(self, "get_platform")()
        if "universal2" in plat:
            return None
        if "arm64" in plat:
            return "arm64-macos"
        if "x86_64" in plat:
            return "x86_64-macos"
        return None

    def _opt_flags(self) -> list[str]:
        return ["-O0", "-g"] if self.debug else ["-O3", "-DNDEBUG", "-s"]

    def _macro_flags(self, ext: Extension) -> list[str]:
        flags = []
        for name, value in ext.define_macros or []:
            flags.append(f"-D{name}" if value is None else f"-D{name}={value}")
        flags.extend(f"-U{name}" for name in ext.undef_macros or [])
        return flags

    def _cleanup_artifacts(self, ext_path: Path) -> None:
        for name in (
            "lib.lib",
            "lib.exp",
            f"{ext_path.stem}.lib",
            f"{ext_path.stem}.exp",
        ):
            artifact = ext_path.parent / name
            if artifact.exists():
                artifact.unlink()


setup(cmdclass={"build_ext": ZigBuildExt})
