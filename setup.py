import os
import shlex
import shutil
import sys
import sysconfig
from pathlib import Path

from setuptools import setup
from setuptools.command.build_ext import build_ext
from setuptools.extension import Extension

ROOT = Path(__file__).resolve().parent


def _split_flags(value: str | None) -> list[str]:
    if not value:
        return []
    return shlex.split(value, posix=os.name != "nt")


def _filter_zig_unix_args(args: list[str]) -> list[str]:
    blocked = {
        "-Wl,-Bsymbolic-functions",
    }
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


def _iter_unique_paths(
    paths: list[str | Path | None],
    *,
    existing_only: bool = False,
) -> list[str]:
    resolved_paths = []
    seen = set()
    for path in paths:
        if not path:
            continue
        resolved = Path(path)
        if existing_only and not resolved.is_dir():
            continue
        resolved_str = str(resolved)
        if resolved_str in seen:
            continue
        seen.add(resolved_str)
        resolved_paths.append(resolved_str)
    return resolved_paths


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
    libraries: list[str | None],
) -> None:
    seen = set()
    for library in libraries:
        if not library or library in seen:
            continue
        seen.add(library)
        command.append(f"-l{library}")


class ZigBuildExt(build_ext):
    def build_extensions(self) -> None:
        zig = self._find_zig()
        if zig is not None and os.name != "nt" and self.compiler.compiler_type == "unix":
            if sys.platform == "darwin" and self._darwin_target() is None:
                super().build_extensions()
                return

            compiler = list(self.compiler.compiler)
            compiler_so = list(self.compiler.compiler_so)
            prefix = self._zig_cc_prefix(zig, self._zig_unix_target())
            extra = self._macos_version_min()

            self.compiler.compiler = [
                *prefix,
                *extra,
                *_filter_zig_unix_args(compiler[1:]),
            ]
            self.compiler.compiler_so = [
                *prefix,
                *extra,
                *_filter_zig_unix_args(compiler_so[1:]),
            ]
            linker_so = list(self.compiler.linker_so)
            linker_exe = list(self.compiler.linker_exe)
            linker_blocked = {"-g", "-fno-common"}
            linker_so_filtered = [a for a in linker_so[1:] if a not in linker_blocked]
            linker_exe_filtered = [a for a in linker_exe[1:] if a not in linker_blocked]
            self.compiler.linker_so = [
                *prefix,
                *extra,
                *_filter_zig_unix_args(linker_so_filtered),
            ]
            self.compiler.linker_exe = [
                *prefix,
                *extra,
                *_filter_zig_unix_args(linker_exe_filtered),
            ]
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
        command.append("-shared")
        command.extend(self._macro_flags(ext))
        _extend_with_prefixed_paths(command, "-I", self._include_dirs(ext))

        command.extend(_split_flags(os.environ.get("CPPFLAGS")))
        command.extend(_split_flags(os.environ.get("CFLAGS")))
        command.extend(str(Path(source)) for source in ext.sources)

        _extend_with_prefixed_paths(
            command,
            "-L",
            [*(ext.library_dirs or []), *self._python_library_dirs()],
            existing_only=True,
        )

        python_import_library = self._python_import_library()
        if python_import_library is not None:
            command.append(str(python_import_library))

        _extend_with_unique_libraries(command, self._libraries(ext))

        command.extend(_split_flags(os.environ.get("LDFLAGS")))
        command.extend(str(arg) for arg in ext.extra_compile_args or [])
        command.extend(str(arg) for arg in ext.extra_link_args or [])
        command.extend(["-o", str(ext_path)])

        self.spawn(command)
        self._cleanup_windows_link_artifacts(ext_path)

    @staticmethod
    def _find_zig() -> str | None:
        found = shutil.which("python-zig")
        if found is not None:
            return found
        found = shutil.which("zig")
        if found is not None:
            return found
        return None

    def _windows_target(self) -> str | None:
        plat_name = self.plat_name or self.get_platform()
        targets = {
            "win32": "x86-windows-msvc",
            "win-amd64": "x86_64-windows-msvc",
            "win-arm64": "aarch64-windows-msvc",
        }
        return targets.get(plat_name)

    def _zig_unix_target(self) -> str | None:
        if sys.platform != "darwin":
            return None
        return self._darwin_target()

    @staticmethod
    def _windows_arch_macro(target: str | None) -> list[str]:
        arch_macros = {
            "x86-windows-msvc": "-D_X86_",
            "x86_64-windows-msvc": "-D_AMD64_",
            "aarch64-windows-msvc": "-D_ARM64_",
        }
        macro = arch_macros.get(target)
        return [macro] if macro else []

    @staticmethod
    def _macos_version_min() -> list[str]:
        if sys.platform != "darwin":
            return []
        version = os.environ.get("MACOSX_DEPLOYMENT_TARGET")
        if not version:
            version = sysconfig.get_config_var("MACOSX_DEPLOYMENT_TARGET")
        if version:
            return [f"-mmacosx-version-min={version}"]
        return []

    def _darwin_target(self) -> str | None:
        plat_name = self.plat_name or self.get_platform()
        if "universal2" in plat_name:
            return None
        if "arm64" in plat_name:
            return "aarch64-macos"
        if "x86_64" in plat_name:
            return "x86_64-macos"
        return None

    @staticmethod
    def _zig_cc_prefix(zig: str, target: str | None) -> list[str]:
        prefix = [zig, "cc"]
        if target is not None:
            prefix.extend(["-target", target])
        return prefix

    def _optimization_flags(self) -> list[str]:
        if self.debug:
            return ["-O0", "-g"]
        return ["-O3", "-DNDEBUG", "-s"]

    @staticmethod
    def _macro_flags(ext: Extension) -> list[str]:
        flags: list[str] = []
        for name, value in ext.define_macros or []:
            if value is None:
                flags.append(f"-D{name}")
            else:
                flags.append(f"-D{name}={value}")
        flags.extend(f"-U{name}" for name in ext.undef_macros or [])
        return flags

    @staticmethod
    def _include_dirs(ext: Extension) -> list[str | Path | None]:
        directories: list[str | Path | None] = [*(ext.include_dirs or [])]
        directories.extend(sysconfig.get_path(key) for key in ("include", "platinclude"))
        directories.append(sysconfig.get_config_var("INCLUDEPY"))
        return directories

    @staticmethod
    def _libraries(ext: Extension) -> list[str]:
        libraries = list(ext.libraries or [])
        if any(Path(source).name == "windows.c" for source in ext.sources):
            libraries.extend(("advapi32", "Mincore"))
        return libraries

    @staticmethod
    def _cleanup_windows_link_artifacts(ext_path: Path) -> None:
        for artifact in (
            ext_path.parent / "lib.lib",
            ext_path.parent / "lib.exp",
            ext_path.parent / f"{ext_path.stem}.lib",
            ext_path.parent / f"{ext_path.stem}.exp",
        ):
            if artifact.exists():
                artifact.unlink()

    @staticmethod
    def _python_library_dirs() -> list[str]:
        roots: list[str | Path | None] = [
            Path(sys.prefix),
            Path(sys.base_prefix),
            Path(sys.exec_prefix),
            Path(sys.base_exec_prefix),
            Path(sys.executable).resolve().parent,
            Path(sys.executable).resolve().parent.parent,
        ]

        roots.extend(
            sysconfig.get_config_var(key)
            for key in (
                "BINDIR",
                "LIBDIR",
                "LIBPL",
                "installed_base",
                "installed_platbase",
                "base",
                "platbase",
            )
        )
        roots.extend(
            sysconfig.get_path(key)
            for key in ("include", "platinclude", "stdlib", "platstdlib", "scripts")
        )

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

    @classmethod
    def _python_import_library(cls) -> Path | None:
        version = sysconfig.get_python_version().replace(".", "")
        for directory in cls._python_library_dirs():
            for stem in (f"python{version}", "python3"):
                candidate = Path(directory) / f"{stem}.lib"
                if candidate.is_file():
                    return candidate

        return None


setup(cmdclass={"build_ext": ZigBuildExt})
