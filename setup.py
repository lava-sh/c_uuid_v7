import os
import platform
import subprocess
import sys
from importlib import import_module
from pathlib import Path
from sysconfig import get_config_var
from types import ModuleType
from typing import Any, ClassVar, cast

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext
from setuptools.dist import Distribution

ROOT = Path(__file__).resolve().parent
LOCAL_HPY_ROOT = ROOT / ".hpy-source" / "hpy-0.9.0"
PREPARE_HPY_SOURCE = ROOT / "scripts" / "prepare_hpy_source.py"
IS_WINDOWS = platform.system() == "Windows"
IS_MACOS = platform.system() == "Darwin"
IS_LINUX = platform.system() == "Linux"

MACOS_ZIG_ARCH = {"x86_64": "x86_64", "arm64": "aarch64"}
WINDOWS_PLAT_ARCH = {"win32": "x86", "win-amd64": "x86_64", "win-arm64": "arm64"}
WINDOWS_ZIG_ARCH = {"x86": "x86", "x86_64": "x86_64", "arm64": "aarch64"}
LINUX_HOST_ARCH = {
    "amd64": "x86_64", "x64": "x86_64", "x86_64": "x86_64",
    "i386": "x86", "i686": "x86", "x86": "x86",
    "aarch64": "arm64", "arm64": "arm64",
    "ppc64le": "ppc64le", "powerpc64le": "ppc64le",
    "riscv64": "riscv64", "s390x": "s390x", "armv7l": "armv7l",
}
LINUX_ZIG_ARCH = {
    "x86": "x86", "x86_64": "x86_64", "arm64": "aarch64",
    "ppc64le": "powerpc64le", "riscv64": "riscv64", "s390x": "s390x", "armv7l": "arm",
}
LINUX_GLIBC_VERSION = {
    "x86": "2.5", "x86_64": "2.17", "arm64": "2.31",
    "ppc64le": "2.17", "riscv64": "2.31", "s390x": "2.17", "armv7l": "2.17",
}
DEBUG_EMULATED_LINUX_ARCHS = {"ppc64le", "riscv64", "s390x"}
SAFE_EMULATED_LINUX_ARCHS = {"armv7l"}


def _ensure_local_hpy_source() -> None:
    marker = LOCAL_HPY_ROOT / "hpy" / "devel" / "__init__.py"
    if marker.exists() or not PREPARE_HPY_SOURCE.exists():
        return
    subprocess.run([sys.executable, str(PREPARE_HPY_SOURCE)], check=True)


def _load_hpy_devel() -> tuple[ModuleType, Path | None]:
    _ensure_local_hpy_source()
    if LOCAL_HPY_ROOT.exists():
        sys.path.insert(0, str(LOCAL_HPY_ROOT))
        module = import_module("hpy.devel")
        return module, LOCAL_HPY_ROOT / "hpy" / "devel"
    return import_module("hpy.devel"), None


HPY_DEVEL, HPY_DEVEL_BASE = _load_hpy_devel()
cast(Any, HPY_DEVEL)._HPY_UNIVERSAL_MODULE_STUB_TEMPLATE = """\
def __bootstrap__():
    from importlib.resources import files
    from os import environ
    from sys import modules
    from hpy.universal import _load_bootstrap
    ext_filepath = str(files(__package__).joinpath({ext_file!r}))
    m = _load_bootstrap({module_name!r}, __name__, __package__, ext_filepath,
                        __loader__, __spec__, environ)
    modules[__name__] = m
__bootstrap__()
"""


class RelativeHPyDevel(HPY_DEVEL.HPyDevel):
    def _relative_path(self, path: Path) -> Path:
        try:
            return Path(path.resolve().relative_to(ROOT))
        except ValueError:
            return path

    def get_extra_include_dirs(self) -> list[str]:
        return [self._relative_path(self.include_dir).as_posix()]

    def get_include_dir_forbid_python_h(self) -> Path:
        return self._relative_path(super().get_include_dir_forbid_python_h())

    def _get_sources_relative(self, sources_method: str) -> list[str]:
        return [
            self._relative_path(Path(path)).as_posix()
            for path in getattr(super(), sources_method)()
        ]

    def get_extra_sources(self) -> list[str]:
        return self._get_sources_relative("get_extra_sources")

    def get_ctx_sources(self) -> list[str]:
        return self._get_sources_relative("get_ctx_sources")


class HPyDistribution(Distribution):
    global_options: ClassVar[list] = [
        *Distribution.global_options,
        ("hpy-abi=", None, "Specify the HPy ABI mode"),
        ("hpy-use-static-libs", None, "Use static HPy libraries when available"),
    ]
    hpy_abi = HPY_DEVEL.DEFAULT_HPY_ABI
    hpy_use_static_libs = False

    def __init__(self, attrs: dict[str, Any] | None = None) -> None:
        self.hpy_ext_modules = []
        super().__init__(attrs)
        if HPY_DEVEL_BASE is None:
            self.hpydevel = RelativeHPyDevel()
        else:
            self.hpydevel = RelativeHPyDevel(base_dir=HPY_DEVEL_BASE)
        if not _hpy_build_is_patched(self):
            self.hpydevel.fix_distribution(self)


def _has_mro_name(cls: type[object], name: str) -> bool:
    return any(base.__name__ == name for base in cls.__mro__)


def _hpy_build_is_patched(dist: Distribution) -> bool:
    build_cmd = dist.cmdclass.get("build")
    if build_cmd is None and getattr(HPY_DEVEL.cmd, "build", None) is not None:
        build_cmd = getattr(HPY_DEVEL.cmd.build, "build", None)
    build_ext_cmd = dist.cmdclass.get("build_ext") or getattr(
        HPY_DEVEL.setuptools.command.build_ext, "build_ext", None,
    )
    return (
            build_cmd is not None
            and _has_mro_name(build_cmd, "build_hpy_mixin")
            and build_ext_cmd is not None
            and _has_mro_name(build_ext_cmd, "build_ext_hpy_mixin")
    )


def _find_zig() -> str:
    try:
        import ziglang  # type: ignore[import-not-found]
    except ModuleNotFoundError:
        return os.environ.get("PY_ZIG", "zig")
    zig_dir = Path(ziglang.__file__).parent
    zig_path = zig_dir / ("zig.exe" if IS_WINDOWS else "zig")
    zig_path.chmod(0o755)
    return os.environ.get("PY_ZIG", str(zig_path))


def _run_command(cmd: list[str], *, tool_name: str) -> None:
    print("cmd", " ".join(f'"{arg}"' if " " in arg else arg for arg in cmd))
    print()
    sys.stdout.flush()
    result = subprocess.run(cmd, capture_output=True, encoding="utf-8", check=False)
    if result.returncode != 0:
        print("\nrun return:\n", result, "\n")
        error_msg = result.stderr
        if not error_msg:
            error_msg = f"{tool_name} failed with exit code {result.returncode}"
        raise RuntimeError(error_msg)
    if result.stderr:
        print(result.stderr)


def _macos_deployment_target(arch: str) -> str:
    env_key = "MACOSX_DEPLOYMENT_TARGET"
    configured = os.environ.get(env_key) or get_config_var(env_key)
    if sys.version_info >= (3, 14):
        default = "10.15"
    elif sys.version_info >= (3, 12):
        default = "10.13"
    else:
        default = "10.9"
    target = configured or default
    if arch == "arm64":
        parts = tuple(int(x) for x in target.split("."))
        target = ".".join(str(p) for p in max(parts, (11, 0)))
    return target


def _zig_macos_target(arch: str) -> str:
    return f"{MACOS_ZIG_ARCH.get(arch, arch)}-macos.{_macos_deployment_target(arch)}"


def _windows_arch(plat_name: str | None = None) -> str:
    normalized = (plat_name or "").lower().replace("_", "-")
    if normalized in WINDOWS_PLAT_ARCH:
        return WINDOWS_PLAT_ARCH[normalized]
    if sys.maxsize <= 2 ** 32:
        return "x86"
    machine = platform.machine().lower()
    return "arm64" if "arm" in machine or machine == "aarch64" else "x86_64"


def _zig_windows_target(plat_name: str | None = None) -> str:
    arch = _windows_arch(plat_name)
    zig_arch = WINDOWS_ZIG_ARCH.get(arch, arch)
    return f"{zig_arch}-windows-msvc"


def _linux_arch() -> str:
    machine = platform.machine().lower()
    return LINUX_HOST_ARCH.get(machine, machine)


def _zig_linux_target() -> str:
    arch = _linux_arch()
    zig_arch = LINUX_ZIG_ARCH.get(arch, arch)
    auditwheel_plat = os.environ.get("AUDITWHEEL_PLAT", "")
    libc = "musl" if "musllinux" in auditwheel_plat else "gnu"
    libc_suffix = libc
    if libc == "gnu":
        libc_abi = "gnueabihf" if arch == "armv7l" else libc
        libc_suffix = f"{libc_abi}.{LINUX_GLIBC_VERSION[arch]}"
    return f"{zig_arch}-linux-{libc_suffix}"


def _zig_target(plat_name: str | None = None) -> str | None:
    if IS_MACOS:
        return None
    if IS_WINDOWS:
        return _zig_windows_target(plat_name)
    if IS_LINUX:
        return _zig_linux_target()
    return None


def _zig_optimize_mode() -> str:
    if IS_LINUX:
        arch = _linux_arch()
        if arch in DEBUG_EMULATED_LINUX_ARCHS:
            return "Debug"
        if arch in SAFE_EMULATED_LINUX_ARCHS:
            return "ReleaseSafe"
    return "ReleaseFast"


def _zig_build_args(output: Path, extra_args: list[str]) -> list[str]:
    return [
        _find_zig(), "build-obj", f"-femit-bin={output}",
        *(["-fPIC"] if not IS_WINDOWS else []),
        "-O", _zig_optimize_mode(), *extra_args,
        *(["-lc"] if not IS_WINDOWS else []),
        "src/lib.zig",
    ]


class ZigHPyBuildExt(build_ext):
    def build_extension(self, ext: Extension) -> None:
        self._ensure_zig_object(ext)
        super().build_extension(ext)

    def _ensure_zig_object(self, ext: Extension) -> None:
        build_temp = Path(self.build_temp)
        build_temp.mkdir(parents=True, exist_ok=True)
        object_suffix = ".obj" if IS_WINDOWS else ".o"
        object_name = ext.name.replace(".", "_")
        output = build_temp / f"{object_name}_zig{object_suffix}"
        if IS_MACOS:
            self._build_macos_object(output)
        else:
            target = _zig_target(getattr(self, "plat_name", None))
            extra_args = ["-target", target] if target else []
            self._run_zig_build_obj(output, extra_args)
        extra_objects = list(ext.extra_objects or [])
        output_str = str(output)
        if output_str not in extra_objects:
            extra_objects.append(output_str)
        ext.extra_objects = extra_objects

    def _run_zig_build_obj(self, output: Path, extra_args: list[str]) -> None:
        _run_command(_zig_build_args(output, extra_args), tool_name="zig")

    def _build_macos_object(self, output: Path) -> None:
        archflags = os.environ.get("ARCHFLAGS", "")
        import re
        archs = re.findall(r"-arch\s+(\S+)", archflags)
        if not archs:
            machine = platform.machine().lower()
            archs = ["arm64"] if machine in {"arm64", "aarch64"} else ["x86_64"]
        if len(archs) == 1:
            self._run_zig_build_obj(output, ["-target", _zig_macos_target(archs[0])])
            return
        if set(archs) != {"x86_64", "arm64"}:
            error_msg = f"unsupported macOS ARCHFLAGS: {archflags}"
            raise RuntimeError(error_msg)
        thin_outputs = []
        for arch in ("x86_64", "arm64"):
            thin = output.with_name(f"{output.stem}.{arch}{output.suffix}")
            thin_outputs.append(thin)
            self._run_zig_build_obj(thin, ["-target", _zig_macos_target(arch)])
        lipo_cmd = [
            "lipo", "-create", "-output", str(output),
            *(str(p) for p in thin_outputs),
        ]
        _run_command(lipo_cmd, tool_name="lipo")
        for thin in thin_outputs:
            thin.unlink(missing_ok=True)


setup(
    distclass=HPyDistribution,
    cmdclass={"build_ext": ZigHPyBuildExt},
    hpy_ext_modules=[
        Extension(
            "c_uuid_v7._core",
            ["src/hpy.c"],
            libraries=["advapi32", "ntdll"] if IS_WINDOWS else [],
        ),
    ],
)
