import os
import shutil
import subprocess
import sys
import sysconfig
import tempfile
from pathlib import Path

import ziglang
from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext
from setuptools.errors import CompileError


def _platform_parts() -> list[str]:
    return sysconfig.get_platform().split("-")


def _macos_targets() -> list[str]:
    arch_flags = os.environ.get("ARCHFLAGS", "").split()
    archs: list[str] = []

    for index, flag in enumerate(arch_flags[:-1]):
        if flag == "-arch":
            archs.append(arch_flags[index + 1])

    if not archs:
        parts = _platform_parts()
        if len(parts) < 3 or parts[0] != "macosx":
            return []
        arch = parts[-1]
        archs = ["x86_64", "arm64"] if arch == "universal2" else [arch]

    deployment_target = os.environ.get("MACOSX_DEPLOYMENT_TARGET")
    if deployment_target:
        version = deployment_target.replace("_", ".")
    else:
        parts = _platform_parts()
        version = (
            parts[1].replace("_", ".")
            if len(parts) >= 3 and parts[0] == "macosx"
            else ""
        )

    targets: list[str] = []
    for arch in archs:
        zig_arch = {
            "x86_64": "x86_64",
            "arm64": "aarch64",
        }.get(arch)
        if zig_arch is None:
            continue
        targets.append(
            f"{zig_arch}-macos.{version}" if version else f"{zig_arch}-macos",
        )

    return targets


def _windows_target() -> str | None:
    return {
        "win32": "x86-windows-msvc",
        "win-arm64": "aarch64-windows-msvc",
    }.get(sysconfig.get_platform())


def _zig_target() -> str | None:
    if sys.platform == "darwin":
        targets = _macos_targets()
        return targets[0] if len(targets) == 1 else None
    if sys.platform == "win32":
        return _windows_target()
    return None


def _zig_compile_args() -> list[str]:
    args = ["-O", "ReleaseFast", "-I", "src"]
    target = _zig_target()
    if target is not None:
        args.extend(["-target", target])
    return args


def _find_zig() -> str:
    zig = os.environ.get("PY_ZIG")
    if zig:
        return zig

    if ziglang is not None:
        zig_path = Path(ziglang.__file__).parent / (
            "zig.exe" if sys.platform == "win32" else "zig"
        )
        zig_path.chmod(0o755)
        return str(zig_path)

    zig = shutil.which("zig")
    if zig is None:
        msg = "could not find Zig compiler; set PY_ZIG or install ziglang"
        raise CompileError(msg)
    return zig


def _macos_sysroot() -> str | None:
    sysroot = os.environ.get("SDKROOT")
    if sysroot:
        return sysroot

    xcrun = shutil.which("xcrun")
    if xcrun is None:
        return None

    result = subprocess.run(
        [xcrun, "--sdk", "macosx", "--show-sdk-path"],
        capture_output=True,
        encoding="utf-8",
        check=False,
    )
    if result.returncode != 0:
        return None

    sysroot = result.stdout.strip()
    return sysroot or None


def _compile_args(ext: Extension, zig_target: str | None) -> list[str]:
    compile_args: list[str] = []
    skip_next = False
    known_targets = {
        "x86_64-macos",
        "aarch64-macos",
        "x86-windows-msvc",
        "aarch64-windows-msvc",
    }

    for arg in ext.extra_compile_args:
        if skip_next:
            skip_next = False
            continue
        if arg in {"-target", "--target"}:
            skip_next = True
            continue
        if arg.startswith(("-target=", "--target=")):
            continue
        if arg in known_targets or any(
            arg.startswith(f"{target}.") for target in known_targets
        ):
            continue
        if arg.startswith("macosx-"):
            continue
        compile_args.append(arg)

    if zig_target is not None:
        compile_args.extend(["-target", zig_target])

    if sys.platform == "darwin":
        sysroot = _macos_sysroot()
        if sysroot is not None:
            compile_args.extend(["--sysroot", sysroot])

    return compile_args


class _ZigBuildExt(build_ext):
    def build_extension(self, ext: Extension) -> None:
        if not any(source.endswith(".zig") for source in ext.sources):
            super().build_extension(ext)
            return

        if sys.platform == "darwin" and len(_macos_targets()) > 1:
            self._build_macos_universal2(ext)
            return

        self._run_build(self._build_command(ext))

    def _build_command(self, ext: Extension) -> list[str]:
        return self._build_command_for_target(
            ext=ext,
            output_path=Path(self.get_ext_fullpath(ext.name)),
            zig_target=_zig_target(),
        )

    def _build_command_for_target(
        self,
        *,
        ext: Extension,
        output_path: Path,
        zig_target: str | None,
    ) -> list[str]:
        output_path.parent.mkdir(parents=True, exist_ok=True)

        command = [
            _find_zig(),
            "build-lib",
            "-dynamic",
            "-fallow-shlib-undefined",
            f"-femit-bin={output_path.resolve()}",
            "-lc",
        ]
        command.extend(self._include_args(ext))
        command.extend(self._library_args(ext))
        command.extend(_compile_args(ext, zig_target))

        if sys.platform == "win32":
            command.append(f"-lpython{sys.version_info.major}{sys.version_info.minor}")

        command.extend(
            f"-l{library}"
            for library in ext.libraries
            if sys.platform != "win32" or library != "python"
        )
        command.extend(ext.sources)
        return command

    def _include_args(self, ext: Extension) -> list[str]:
        include_args: list[str] = []
        include_dirs: list[Path] = []

        for raw_dir in [*self.compiler.include_dirs, *ext.include_dirs]:
            include_dir = Path(raw_dir).resolve()
            if include_dir.exists() and include_dir not in include_dirs:
                include_dirs.append(include_dir)
                include_args.extend(["-I", str(include_dir)])

        if os.name != "nt":
            system_include_dirs = [
                Path("/usr/include"),
                *Path("/usr/include").glob("*-linux-gnu"),
            ]
            for include_dir in system_include_dirs:
                if include_dir.exists() and include_dir not in include_dirs:
                    include_dirs.append(include_dir)
                    include_args.extend(["-I", str(include_dir)])

        return include_args

    def _library_args(self, ext: Extension) -> list[str]:
        library_args: list[str] = []
        for library_dir in self._library_dirs(ext):
            library_args.extend(["-L", library_dir])
        return library_args

    def _library_dirs(self, ext: Extension) -> list[str]:
        seen: set[str] = set()
        directories: list[str] = []
        python_lib_name = f"python{sys.version_info.major}{sys.version_info.minor}"

        for raw_dir in [*self.compiler.library_dirs, *ext.library_dirs]:
            library_dir = Path(raw_dir)
            if not library_dir.exists():
                continue

            if sys.platform == "win32":
                dll_path = library_dir / f"{python_lib_name}.dll"
                lib_path = library_dir / f"{python_lib_name}.lib"
                if dll_path.exists() and not lib_path.exists():
                    continue

            resolved = str(library_dir.resolve())
            if resolved in seen:
                continue
            seen.add(resolved)
            directories.append(resolved)

        return directories

    def _emit_output(self, result: subprocess.CompletedProcess[str]) -> None:
        if result.stdout:
            self.announce(result.stdout, level=2)
        if result.stderr:
            self.announce(result.stderr, level=3)

    def _run_build(self, command: list[str]) -> None:
        result = subprocess.run(
            command,
            capture_output=True,
            encoding="utf-8",
            check=False,
        )
        self._emit_output(result)
        if result.returncode != 0:
            raise CompileError(result.stderr or "zig build-lib failed")

    def _build_macos_universal2(self, ext: Extension) -> None:
        target_path = Path(self.get_ext_fullpath(ext.name))
        macos_targets = _macos_targets()
        built_slices: list[str] = []

        with tempfile.TemporaryDirectory() as tmpdir:
            tmpdir_path = Path(tmpdir)
            for macos_target in macos_targets:
                arch_name = macos_target.split("-", maxsplit=1)[0]
                slice_path = (
                        tmpdir_path /
                        f"{target_path.stem}-{arch_name}{target_path.suffix}"
                )
                self._run_build(
                    self._build_command_for_target(
                        ext=ext,
                        output_path=slice_path,
                        zig_target=macos_target,
                    ),
                )
                built_slices.append(str(slice_path))

            lipo = shutil.which("lipo")
            if lipo is None:
                msg = "could not find lipo for macOS universal2 build"
                raise CompileError(msg)

            target_path.parent.mkdir(parents=True, exist_ok=True)
            result = subprocess.run(
                [lipo, "-create", "-output", str(target_path), *built_slices],
                capture_output=True,
                encoding="utf-8",
                check=False,
            )
            self._emit_output(result)
            if result.returncode != 0:
                raise CompileError(result.stderr or "lipo failed for universal2 build")


setup(
    cmdclass={"build_ext": _ZigBuildExt},
    ext_modules=[
        Extension(
            "c_uuid_v7._core",
            ["src/lib.zig"],
            extra_compile_args=_zig_compile_args(),
            libraries=["bcrypt"] if sys.platform == "win32" else [],
        ),
    ],
)
