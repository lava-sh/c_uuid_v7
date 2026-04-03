import os
import sys
from collections.abc import Iterable

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext

EXTENSION = [
    "src/lib.c",
    "src/hexpairs.c",
    "src/posix.c",
    "src/random.c",
    "src/windows.c",
]

FALSE_VALUES = {"", "0", "false", "no", "off"}


def env_enabled(name: str, default: str = "1") -> bool:
    return os.getenv(name, default).strip().lower() not in FALSE_VALUES


def append_unique(target: list[str], values: Iterable[str]) -> None:
    for value in values:
        if value not in target:
            target.append(value)


def append_macro(
        target: list[tuple[str, str | None]],
        macro: tuple[str, str | None],
) -> None:
    if macro not in target:
        target.append(macro)


class BuildExtWithMaxOpt(build_ext):
    def build_extensions(self) -> None:
        for extension in self.extensions:
            self.apply_optimization_flags(extension)
        super().build_extensions()

    def apply_optimization_flags(self, extension: Extension) -> None:
        compile_args = list(extension.extra_compile_args or [])
        link_args = list(extension.extra_link_args or [])
        macros = list(extension.define_macros or [])

        append_macro(macros, ("NDEBUG", "1"))

        if self.compiler.compiler_type == "msvc":
            append_unique(compile_args, ["/O2", "/Gw", "/Gy", "/Oi", "/Ot", "/GF"])
            append_unique(link_args, ["/OPT:REF", "/OPT:ICF"])

            if env_enabled("BUILD_LTO"):
                append_unique(compile_args, ["/GL"])
                append_unique(link_args, ["/LTCG"])
        else:
            append_unique(compile_args, ["-O3", "-fstrict-aliasing"])

            if env_enabled("BUILD_MAX_OPT"):
                append_unique(
                    compile_args,
                    [
                        "-fno-semantic-interposition",
                        "-fomit-frame-pointer",
                    ],
                )

            if env_enabled("BUILD_LTO"):
                append_unique(compile_args, ["-flto"])
                append_unique(link_args, ["-flto"])

            if sys.platform == "darwin":
                append_unique(link_args, ["-Wl,-dead_strip"])
            else:
                append_unique(compile_args, ["-ffunction-sections", "-fdata-sections"])
                append_unique(link_args, ["-Wl,--gc-sections", "-Wl,-O2"])

        extension.extra_compile_args = compile_args
        extension.extra_link_args = link_args
        extension.define_macros = macros


setup(
    ext_modules=[
        Extension(
            name="c_uuid_v7._core",
            sources=EXTENSION,
        ),
    ],
    cmdclass={"build_ext": BuildExtWithMaxOpt},
)
