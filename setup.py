import os
import shlex
import shutil
import subprocess
import sys
import sysconfig
from pathlib import Path

from setuptools import setup
from setuptools.command.build_ext import build_ext
from setuptools.extension import Extension


def _split_flags(value: str | None) -> list[str]:
    return shlex.split(value, posix=os.name != "nt") if value else []


def _filter_zig_unix_args(args: list[str]) -> list[str]:
    blocked = {"-Wl,-Bsymbolic-functions"}
    filtered = []
    skip_next = False
    for arg in args:
        if skip_next:
            skip_next = False
            continue
        if arg == "-arch":
            skip_next = True
            continue
        if arg in blocked:
            continue
        filtered.append(arg)
    return filtered


def _set_compiler(
    compiler: build_ext,
    tool: str,
    prefix: list[str],
    extra_args: list[str] | None = None,
) -> None:
    orig = getattr(compiler, tool)
    filtered = _filter_zig_unix_args(orig[1:])
    setattr(compiler, tool, [*prefix, *(extra_args or []), *filtered])


def _iter_unique_paths(
    paths: list[str | Path | None],
    *,
    existing_only: bool = False,
) -> list[str]:
    seen = set()
    result = []
    for path in paths:
        if not path:
            continue
        resolved = Path(path)
        if existing_only and not resolved.is_dir():
            continue
        resolved_str = str(resolved)
        if resolved_str not in seen:
            seen.add(resolved_str)
            result.append(resolved_str)
    return result


def _extend_with_prefixed_paths(
    command: list[str],
    prefix: str,
    paths: list[str | Path | None],
    *,
    existing_only: bool = False,
) -> None:
    for path in _iter_unique_paths(paths, existing_only=existing_only):
        command.extend([prefix, path])


def _extend_with_unique_libraries(
    command: list[str],
    libraries: list[str],
) -> None:
    seen = set()
    for library in libraries:
        if library not in seen:
            seen.add(library)
            command.append(f"-l{library}")


def _python_paths() -> list[str]:
    roots = [
        Path(sys.prefix),
        Path(sys.base_prefix),
        Path(sys.exec_prefix),
        Path(sys.base_exec_prefix),
        Path(sys.executable).resolve().parent,
        Path(sys.executable).resolve().parent.parent,
        *(
            sysconfig.get_config_var(k)
            for k in (
                "BINDIR",
                "LIBDIR",
                "LIBPL",
                "installed_base",
                "installed_platbase",
                "base",
                "platbase",
            )
        ),
        *(
            sysconfig.get_path(k)
            for k in (
                "include",
                "platinclude",
                "stdlib",
                "platstdlib",
                "scripts",
            )
        ),
    ]
    candidates = []
    for root_path in roots:
        if not root_path:
            continue
        root = Path(root_path)
        candidates.extend([
            root,
            root / "libs",
            root / "Libs",
            root.parent / "libs",
            root.parent / "Libs",
        ])
    return _iter_unique_paths(candidates, existing_only=True)


class ZigBuildExt(build_ext):
    def build_extensions(self) -> None:
        zig = self._find_zig()
        if zig is not None and os.name != "nt" and self.compiler.compiler_type == "unix":
            if sys.platform == "darwin" and self._darwin_target() is None:
                super().build_extensions()
                return

            prefix = self._zig_cc_prefix(zig, self._zig_unix_target())
            _set_compiler(self.compiler, "compiler", prefix)
            _set_compiler(
                self.compiler,
                "compiler_so",
                prefix,
                ["-Wno-empty-translation-unit", "-Wno-visibility"],
            )

            if sys.platform == "darwin":
                clang = shutil.which("clang")
                if clang:
                    linker_args = [clang]
                    result = subprocess.run(
                        [
                            clang,
                            "-Wl,-no_warn_version_mismatches",
                            "-shared",
                            "-o",
                            os.devnull,
                            "-x",
                            "c",
                            "-",
                        ],
                        input="",
                        capture_output=True,
                        text=True,
                    )
                    if result.returncode == 0 or "unknown option" not in result.stderr:
                        linker_args.append("-Wl,-no_warn_version_mismatches")
                    _set_compiler(self.compiler, "linker_so", linker_args)
                    _set_compiler(self.compiler, "linker_exe", linker_args)
            else:
                _set_compiler(self.compiler, "linker_so", prefix)
                _set_compiler(self.compiler, "linker_exe", prefix)
        super().build_extensions()

    def build_extension(self, ext: Extension) -> None:
        zig = self._find_zig()
        if os.name != "nt" or zig is None:
            super().build_extension(ext)
            return

        ext_path = Path(self.get_ext_fullpath(ext.name))
        ext_path.parent.mkdir(parents=True, exist_ok=True)
        Path(self.build_temp).mkdir(parents=True, exist_ok=True)

        target = self._windows_target()
        command = self._zig_cc_prefix(zig, target)
        command.extend(self._optimization_flags())
        command.extend(self._windows_arch_macro(target))
        command.extend(["-Wno-empty-translation-unit", "-shared"])
        command.extend(self._macro_flags(ext))
        _extend_with_prefixed_paths(command, "-I", self._include_dirs(ext))

        command.extend(_split_flags(os.environ.get("CPPFLAGS")))
        command.extend(_split_flags(os.environ.get("CFLAGS")))
        command.extend(str(Path(s)) for s in ext.sources)

        _extend_with_prefixed_paths(
            command,
            "-L",
            [*(ext.library_dirs or []), *_python_paths()],
            existing_only=True,
        )

        lib = self._python_import_library()
        if lib is not None:
            command.append(str(lib))

        _extend_with_unique_libraries(command, self._libraries(ext))

        command.extend(_split_flags(os.environ.get("LDFLAGS")))
        command.extend(str(a) for a in ext.extra_compile_args or [])
        command.extend(str(a) for a in ext.extra_link_args or [])
        command.extend(["-o", str(ext_path)])

        self.spawn(command)
        self._cleanup_windows_link_artifacts(ext_path)

    @staticmethod
    def _find_zig() -> str | None:
        return shutil.which("python-zig") or shutil.which("zig")

    def _windows_target(self) -> str | None:
        plat_name = self.plat_name or getattr(self, "get_platform")()
        return {
            "win32": "x86-windows-msvc",
            "win-amd64": "x86_64-windows-msvc",
            "win-arm64": "aarch64-windows-msvc",
        }.get(plat_name)

    def _zig_unix_target(self) -> str | None:
        return None if sys.platform != "darwin" else self._darwin_target()

    def _darwin_target(self) -> str | None:
        plat_name = self.plat_name or getattr(self, "get_platform")()
        if "universal2" in plat_name:
            return None
        if "arm64" in plat_name:
            return "aarch64-macos"
        if "x86_64" in plat_name:
            return "x86_64-macos"
        return None

    @staticmethod
    def _windows_arch_macro(target: str | None) -> list[str]:
        if target is None:
            return []
        macro = {
            "x86-windows-msvc": "-D_X86_",
            "x86_64-windows-msvc": "-D_AMD64_",
            "aarch64-windows-msvc": "-D_ARM64_",
        }.get(target)
        return [macro] if macro else []

    @staticmethod
    def _zig_cc_prefix(zig: str, target: str | None) -> list[str]:
        if target is not None:
            return [zig, "cc", "-target", target]
        return [zig, "cc"]

    def _optimization_flags(self) -> list[str]:
        return ["-O0", "-g"] if self.debug else ["-O3", "-DNDEBUG", "-s"]

    @staticmethod
    def _macro_flags(ext: Extension) -> list[str]:
        flags = []
        for name, value in ext.define_macros or []:
            flags.append(f"-D{name}" if value is None else f"-D{name}={value}")
        flags.extend(f"-U{name}" for name in ext.undef_macros or [])
        return flags

    @staticmethod
    def _include_dirs(ext: Extension) -> list[str | Path | None]:
        return [
            *(ext.include_dirs or []),
            sysconfig.get_path("include"),
            sysconfig.get_path("platinclude"),
            sysconfig.get_config_var("INCLUDEPY"),
        ]

    @staticmethod
    def _libraries(ext: Extension) -> list[str]:
        libraries = list(ext.libraries or [])
        if any(Path(s).name == "windows.c" for s in ext.sources):
            libraries.extend(("advapi32", "Mincore"))
        return libraries

    @staticmethod
    def _cleanup_windows_link_artifacts(ext_path: Path) -> None:
        for name in (
            "lib.lib",
            "lib.exp",
            f"{ext_path.stem}.lib",
            f"{ext_path.stem}.exp",
        ):
            artifact = ext_path.parent / name
            if artifact.exists():
                artifact.unlink()

    @classmethod
    def _python_import_library(cls) -> Path | None:
        version = sysconfig.get_python_version().replace(".", "")
        for directory in _python_paths():
            for stem in (f"python{version}", "python3"):
                candidate = Path(directory) / f"{stem}.lib"
                if candidate.is_file():
                    return candidate
        return None


setup(cmdclass={"build_ext": ZigBuildExt})
