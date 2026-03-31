import sys

from setuptools import Extension, setup

setup(
    build_zig=True,
    ext_modules=[
        Extension(
            "c_uuid_v7._core",
            ["src/lib.zig"],
            extra_compile_args=["-O", "ReleaseFast"],
            libraries=["bcrypt"] if sys.platform == "win32" else [],
        ),
    ],
)
