import os
import shlex
import shutil
import sys
import sysconfig
from pathlib import Path

from setuptools import setup
from setuptools.command.build_ext import build_ext

ROOT = Path(__file__).resolve().parent


def _split_flags(value: str | None) -> list[str]:
    if not value:
        return []
    return shlex.split(value, posix=os.name != "nt")


def _filter_zig_unix_args(args: list[str]) -> list[str]:
    blocked = {
        "-Wl,-Bsymbolic-functions",
    }
    return [arg for arg in args if arg not in blocked]


class ZigBuildExt(build_ext):
    def build_extensions(self) -> None:
        zig = self._find_zig()
        if zig is not None and os.name != "nt" and self.compiler.compiler_type == "unix":
            compiler = list(self.compiler.compiler)
            compiler_so = list(self.compiler.compiler_so)
            linker_so = list(self.compiler.linker_so)
            linker_exe = list(self.compiler.linker_exe)

            self.compiler.compiler = [zig, "cc", *compiler[1:]]
            self.compiler.compiler_so = [zig, "cc", *compiler_so[1:]]
            self.compiler.linker_so = [zig, "cc", *_filter_zig_unix_args(linker_so[1:])]
            self.compiler.linker_exe = [zig, "cc", *_filter_zig_unix_args(linker_exe[1:])]
        super().build_extensions()

    def build_extension(self, ext) -> None:
        zig = self._find_zig()
        if os.name != "nt" or zig is None:
            super().build_extension(ext)
            return

        ext_path = Path(self.get_ext_fullpath(ext.name))
        ext_path.parent.mkdir(parents=True, exist_ok=True)
        Path(self.build_temp).mkdir(parents=True, exist_ok=True)

        command = [zig, "cc"]
        target = self._windows_target()
        if target is not None:
            command.extend(["-target", target])

        if self.debug:
            command.extend(["-O0", "-g"])
        else:
            command.extend(["-O3", "-DNDEBUG", "-s"])

        command.append("-shared")

        for name, value in ext.define_macros or []:
            if value is None:
                command.append(f"-D{name}")
            else:
                command.append(f"-D{name}={value}")
        for name in ext.undef_macros or []:
            command.append(f"-U{name}")

        include_dirs = list(ext.include_dirs or [])
        for key in ("include", "platinclude"):
            path = sysconfig.get_path(key)
            if path:
                include_dirs.append(path)
        include_dirs.append(sysconfig.get_config_var("INCLUDEPY"))

        seen_include_dirs: set[str] = set()
        for directory in include_dirs:
            if not directory:
                continue
            resolved = str(Path(directory))
            if resolved in seen_include_dirs:
                continue
            seen_include_dirs.add(resolved)
            command.extend(["-I", resolved])

        command.extend(_split_flags(os.environ.get("CPPFLAGS")))
        command.extend(_split_flags(os.environ.get("CFLAGS")))
        command.extend(str(Path(source)) for source in ext.sources)

        library_dirs = list(ext.library_dirs or [])
        libdir = sysconfig.get_config_var("LIBDIR")
        if libdir:
            library_dirs.append(libdir)

        python_home = Path(sys.executable).resolve().parent.parent
        candidate_libdir = python_home / "libs"
        if candidate_libdir.is_dir():
            library_dirs.append(str(candidate_libdir))

        seen_library_dirs: set[str] = set()
        for directory in library_dirs:
            if not directory:
                continue
            resolved = str(Path(directory))
            if resolved in seen_library_dirs:
                continue
            seen_library_dirs.add(resolved)
            command.extend(["-L", resolved])

        libraries = list(ext.libraries or [])
        python_library = self._python_library_name()
        if python_library is not None:
            libraries.append(python_library)
        if any(Path(source).name == "windows.c" for source in ext.sources):
            libraries.append("advapi32")

        seen_libraries: set[str] = set()
        for library in libraries:
            if library in seen_libraries:
                continue
            seen_libraries.add(library)
            command.append(f"-l{library}")

        command.extend(_split_flags(os.environ.get("LDFLAGS")))
        command.extend(str(arg) for arg in ext.extra_compile_args or [])
        command.extend(str(arg) for arg in ext.extra_link_args or [])
        command.extend(["-o", str(ext_path)])

        self.spawn(command)

        for artifact in (
            ext_path.parent / "lib.lib",
            ext_path.parent / "lib.exp",
            ext_path.parent / f"{ext_path.stem}.lib",
            ext_path.parent / f"{ext_path.stem}.exp",
        ):
            if artifact.exists():
                artifact.unlink()

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

    @staticmethod
    def _python_library_name() -> str | None:
        version = sysconfig.get_python_version().replace(".", "")
        libdir = sysconfig.get_config_var("LIBDIR")
        if libdir:
            for stem in (f"python{version}", "python3"):
                if (Path(libdir) / f"{stem}.lib").is_file():
                    return stem

        python_home = Path(sys.executable).resolve().parent.parent / "libs"
        for stem in (f"python{version}", "python3"):
            if (python_home / f"{stem}.lib").is_file():
                return stem

        return None


setup(cmdclass={"build_ext": ZigBuildExt})
