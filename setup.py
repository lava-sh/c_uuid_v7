import os
import platform
import re
import subprocess
import sys
from pathlib import Path
from sysconfig import get_config_var

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext


def _find_zig() -> str:
    try:
        import ziglang  # type: ignore
    except ModuleNotFoundError:
        return os.environ.get("PY_ZIG", "zig")

    zig_dir = Path(ziglang.__file__).parent
    zig_path = zig_dir / ("zig.exe" if platform.system() == "Windows" else "zig")
    zig_path.chmod(0o755)
    return os.environ.get("PY_ZIG", str(zig_path))


def _parse_archflags(value: str) -> list[str]:
    return re.findall(r"-arch\s+(\S+)", value)


def _version_tuple(value: str) -> tuple[int, ...]:
    return tuple(int(part) for part in value.split("."))


def _version_string(parts: tuple[int, ...]) -> str:
    return ".".join(str(part) for part in parts)


def _max_version(left: str, right: str) -> str:
    return _version_string(max(_version_tuple(left), _version_tuple(right)))


def _default_macos_target() -> str:
    if platform.python_implementation() == "PyPy":
        return "10.15"
    if sys.version_info >= (3, 14):
        return "10.15"
    if sys.version_info >= (3, 12):
        return "10.13"
    return "10.9"


def _macos_deployment_target(arch: str) -> str:
    configured = os.environ.get("MACOSX_DEPLOYMENT_TARGET") or get_config_var("MACOSX_DEPLOYMENT_TARGET")
    target = configured or _default_macos_target()
    if arch == "arm64":
        target = _max_version(target, "11.0")
    return target


def _zig_macos_target(arch: str) -> str:
    zig_arch = {
        "x86_64": "x86_64",
        "arm64": "aarch64",
    }[arch]
    return f"{zig_arch}-macos.{_macos_deployment_target(arch)}"


class ZigBuildExt(build_ext):
    def build_extension(self, ext: Extension) -> None:
        if not ext.sources:
            return

        target = Path(self.get_ext_fullpath(ext.name)).resolve()
        target.parent.mkdir(parents=True, exist_ok=True)

        if platform.system() == "Darwin":
            self._build_macos(ext, target)
            return

        self._run_zig(ext, target, extra_args=list(ext.extra_compile_args))

    def _base_zig_cmd(self, ext: Extension, target: Path) -> list[str]:
        cmd = [
            _find_zig(),
            "build-lib",
            "-dynamic",
            "-fallow-shlib-undefined",
            f"-femit-bin={target}",
            "-lc",
        ]

        inc_dirs_added: set[Path] = set()
        compiler_include_dirs = getattr(self.compiler, "include_dirs", []) or []
        for inc_dir in [*compiler_include_dirs, *(ext.include_dirs or [])]:
            inc_dir_path = Path(inc_dir).resolve()
            if not inc_dir_path.exists() or inc_dir_path in inc_dirs_added:
                continue
            inc_dirs_added.add(inc_dir_path)
            cmd.extend(("-I", str(inc_dir_path)))

        for path_str in ["/usr/include"]:
            path = Path(path_str)
            if path.exists() and path not in inc_dirs_added:
                inc_dirs_added.add(path)
                cmd.extend(("-I", str(path)))

        usr_include = Path("/usr/include")
        if usr_include.exists():
            for path in usr_include.glob("*-linux-gnu"):
                if path in inc_dirs_added:
                    continue
                inc_dirs_added.add(path)
                cmd.extend(("-I", str(path)))

        lib_dirs_added: set[str] = set()
        compiler_library_dirs = getattr(self.compiler, "library_dirs", []) or []
        for lib_dir in [*compiler_library_dirs, *(ext.library_dirs or [])]:
            if not Path(lib_dir).exists() or lib_dir in lib_dirs_added:
                continue
            lib_dirs_added.add(lib_dir)
            cmd.extend(("-L", str(lib_dir)))

        extra_compile_args = list(ext.extra_compile_args or [])
        if "-O" not in extra_compile_args:
            cmd.extend(("-O", "ReleaseFast"))

        if platform.system() == "Windows":
            cmd.append(f"-lpython{sys.version_info.major}{sys.version_info.minor}")

        return cmd

    def _run_zig(self, ext: Extension, target: Path, extra_args: list[str]) -> None:
        cmd = self._base_zig_cmd(ext, target)
        cmd.extend(extra_args)
        cmd.extend(ext.sources)

        print("cmd", " ".join(f'"{arg}"' if " " in arg else arg for arg in cmd))
        print()
        sys.stdout.flush()

        result = subprocess.run(cmd, capture_output=True, encoding="utf-8")
        if result.returncode != 0 or result.stderr:
            print("\nrun return:\n", result)
            print("\n")
            raise RuntimeError(result.stderr)

    def _build_macos(self, ext: Extension, target: Path) -> None:
        archflags = os.environ.get("ARCHFLAGS", "")
        archs = _parse_archflags(archflags)
        if not archs:
            machine = platform.machine().lower()
            if machine in {"arm64", "aarch64"}:
                archs = ["arm64"]
            else:
                archs = ["x86_64"]

        if len(archs) == 1:
            args = [*list(ext.extra_compile_args or []), "-target", _zig_macos_target(archs[0])]
            self._run_zig(ext, target, args)
            return

        if set(archs) != {"x86_64", "arm64"}:
            raise RuntimeError(f"unsupported macOS ARCHFLAGS: {archflags}")

        thin_targets: list[Path] = []
        for arch in ("x86_64", "arm64"):
            thin_target = target.with_name(f"{target.stem}.{arch}{target.suffix}")
            thin_targets.append(thin_target)
            args = [*list(ext.extra_compile_args or []), "-target", _zig_macos_target(arch)]
            self._run_zig(ext, thin_target, args)

        lipo_cmd = ["lipo", "-create", "-output", str(target), *(str(path) for path in thin_targets)]
        print("cmd", " ".join(f'"{arg}"' if " " in arg else arg for arg in lipo_cmd))
        print()
        sys.stdout.flush()

        result = subprocess.run(lipo_cmd, capture_output=True, encoding="utf-8")
        if result.returncode != 0 or result.stderr:
            print("\nrun return:\n", result)
            print("\n")
            raise RuntimeError(result.stderr)

        for thin_target in thin_targets:
            thin_target.unlink(missing_ok=True)


setup(
    cmdclass={"build_ext": ZigBuildExt},
    ext_modules=[
        Extension("c_uuid_v7._core", ["src/sum.zig"]),
    ],
)
