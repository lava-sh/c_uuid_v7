import os
import platform
import re
import subprocess
import sys
from importlib import import_module
from pathlib import Path
from sysconfig import get_config_var

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext
from setuptools.dist import Distribution

ROOT = Path(__file__).resolve().parent
LOCAL_HPY_ROOT = ROOT / ".hpy-source" / "hpy-0.9.0"


def _load_hpy_devel():
    if LOCAL_HPY_ROOT.exists():
        sys.path.insert(0, str(LOCAL_HPY_ROOT))
        module = import_module("hpy.devel")
        return module, LOCAL_HPY_ROOT / "hpy" / "devel"

    module = import_module("hpy.devel")
    return module, None


HPY_DEVEL, HPY_DEVEL_BASE = _load_hpy_devel()
HPY_DEVEL._HPY_UNIVERSAL_MODULE_STUB_TEMPLATE = """\
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

    def get_extra_sources(self) -> list[str]:
        return [self._relative_path(Path(path)).as_posix() for path in super().get_extra_sources()]

    def get_ctx_sources(self) -> list[str]:
        return [self._relative_path(Path(path)).as_posix() for path in super().get_ctx_sources()]


class HPyDistribution(Distribution):
    global_options = Distribution.global_options + [
        ("hpy-abi=", None, f"Specify the HPy ABI mode (default: {HPY_DEVEL.DEFAULT_HPY_ABI})"),
        ("hpy-use-static-libs", None, "Use static HPy libraries when available"),
    ]
    hpy_abi = HPY_DEVEL.DEFAULT_HPY_ABI
    hpy_use_static_libs = False

    def __init__(self, attrs=None):
        self.hpy_ext_modules = []
        super().__init__(attrs)
        self.hpydevel = RelativeHPyDevel(base_dir=HPY_DEVEL_BASE)
        if not _hpy_build_is_patched(self):
            self.hpydevel.fix_distribution(self)


def _has_mro_name(cls, name: str) -> bool:
    return any(base.__name__ == name for base in cls.__mro__)


def _hpy_build_is_patched(dist: Distribution) -> bool:
    build_cmd = dist.cmdclass.get("build")
    if build_cmd is None and getattr(HPY_DEVEL.cmd, "build", None) is not None:
        build_cmd = getattr(HPY_DEVEL.cmd.build, "build", None)

    build_ext_cmd = dist.cmdclass.get("build_ext")
    if build_ext_cmd is None:
        build_ext_cmd = getattr(HPY_DEVEL.setuptools.command.build_ext, "build_ext", None)

    return (
        build_cmd is not None
        and _has_mro_name(build_cmd, "build_hpy_mixin")
        and build_ext_cmd is not None
        and _has_mro_name(build_ext_cmd, "build_ext_hpy_mixin")
    )

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


def _windows_arch(plat_name: str | None = None) -> str:
    normalized = (plat_name or "").lower().replace("_", "-")
    if normalized == "win32":
        return "x86"
    if normalized == "win-amd64":
        return "x86_64"
    if normalized == "win-arm64":
        return "arm64"

    if sys.maxsize <= 2**32:
        return "x86"

    machine = platform.machine().lower()
    if "arm" in machine or machine == "aarch64":
        return "arm64"
    return "x86_64"


def _zig_windows_target(plat_name: str | None = None) -> str:
    zig_arch = {
        "x86": "x86",
        "x86_64": "x86_64",
        "arm64": "aarch64",
    }[_windows_arch(plat_name)]
    return f"{zig_arch}-windows-msvc"


def _linux_arch() -> str:
    machine = platform.machine().lower()
    aliases = {
        "amd64": "x86_64",
        "x64": "x86_64",
        "x86_64": "x86_64",
        "i386": "x86",
        "i686": "x86",
        "x86": "x86",
        "aarch64": "arm64",
        "arm64": "arm64",
        "ppc64le": "ppc64le",
        "powerpc64le": "ppc64le",
        "s390x": "s390x",
        "armv7l": "armv7l",
    }
    return aliases.get(machine, machine)


def _zig_linux_target() -> str:
    zig_arch = {
        "x86": "x86",
        "x86_64": "x86_64",
        "arm64": "aarch64",
        "ppc64le": "powerpc64le",
        "s390x": "s390x",
        "armv7l": "arm",
    }[_linux_arch()]

    libc = "gnu"
    auditwheel_plat = os.environ.get("AUDITWHEEL_PLAT", "")
    if "musllinux" in auditwheel_plat:
        libc = "musl"

    abi_suffix = ""
    if _linux_arch() == "armv7l" and libc == "gnu":
        abi_suffix = "eabihf"

    return f"{zig_arch}-linux-{libc}{abi_suffix}"


def _zig_target(plat_name: str | None = None) -> str | None:
    if platform.system() == "Darwin":
        return None
    if platform.system() == "Windows":
        return _zig_windows_target(plat_name)
    if platform.system() == "Linux":
        return _zig_linux_target()
    return None


class ZigHPyBuildExt(build_ext):
    def build_extension(self, ext: Extension) -> None:
        self._ensure_zig_object(ext)
        super().build_extension(ext)

    def _ensure_zig_object(self, ext: Extension) -> None:
        build_temp = Path(self.build_temp)
        build_temp.mkdir(parents=True, exist_ok=True)

        object_suffix = ".obj" if platform.system() == "Windows" else ".o"
        object_name = ext.name.replace(".", "_")
        output = build_temp / f"{object_name}_zig{object_suffix}"

        if platform.system() == "Darwin":
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
        cmd = [
            _find_zig(),
            "build-obj",
            f"-femit-bin={output}",
            "-O",
            "ReleaseFast",
            *extra_args,
            "src/sum.zig",
        ]

        print("cmd", " ".join(f'"{arg}"' if " " in arg else arg for arg in cmd))
        print()
        sys.stdout.flush()

        result = subprocess.run(cmd, capture_output=True, encoding="utf-8")
        if result.returncode != 0:
            print("\nrun return:\n", result)
            print("\n")
            raise RuntimeError(result.stderr or f"zig failed with exit code {result.returncode}")
        if result.stderr:
            print(result.stderr)

    def _build_macos_object(self, output: Path) -> None:
        archflags = os.environ.get("ARCHFLAGS", "")
        archs = _parse_archflags(archflags)
        if not archs:
            machine = platform.machine().lower()
            archs = ["arm64"] if machine in {"arm64", "aarch64"} else ["x86_64"]

        if len(archs) == 1:
            self._run_zig_build_obj(output, ["-target", _zig_macos_target(archs[0])])
            return

        if set(archs) != {"x86_64", "arm64"}:
            raise RuntimeError(f"unsupported macOS ARCHFLAGS: {archflags}")

        thin_outputs: list[Path] = []
        for arch in ("x86_64", "arm64"):
            thin = output.with_name(f"{output.stem}.{arch}{output.suffix}")
            thin_outputs.append(thin)
            self._run_zig_build_obj(thin, ["-target", _zig_macos_target(arch)])

        cmd = ["lipo", "-create", "-output", str(output), *(str(path) for path in thin_outputs)]
        print("cmd", " ".join(f'"{arg}"' if " " in arg else arg for arg in cmd))
        print()
        sys.stdout.flush()

        result = subprocess.run(cmd, capture_output=True, encoding="utf-8")
        if result.returncode != 0:
            print("\nrun return:\n", result)
            print("\n")
            raise RuntimeError(result.stderr or f"lipo failed with exit code {result.returncode}")
        if result.stderr:
            print(result.stderr)

        for thin in thin_outputs:
            thin.unlink(missing_ok=True)


setup(
    distclass=HPyDistribution,
    cmdclass={"build_ext": ZigHPyBuildExt},
    hpy_ext_modules=[
        Extension("c_uuid_v7._core", ["src/hpy_sum.c"]),
    ],
)
