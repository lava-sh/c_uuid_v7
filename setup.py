from setuptools import Extension, setup

setup(
    build_zig=True,
    ext_modules=[
        Extension("c_uuid_v7._core", ["src/sum.zig"]),
    ],
)
