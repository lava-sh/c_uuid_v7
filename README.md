<div align="center">

# c_uuid_v7

*Fast UUID v7 generator implemented as a CPython C extension*

| 🐍 PyPI                                                                                            | 🐙 GitHub                                                                                               |
|----------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------|
| ![Version](https://img.shields.io/pypi/v/c_uuid_v7?style=flat-square&color=007ec6)                 | ![Stars](https://img.shields.io/github/stars/lava-sh/c_uuid_v7?style=flat-square&color=007ec6)          |
| ![License](https://img.shields.io/pypi/l/c_uuid_v7?style=flat-square&color=007ec6)                 | ![CI](https://img.shields.io/github/actions/workflow/status/lava-sh/c_uuid_v7/ci.yml?style=flat-square) |
| ![Downloads](https://img.shields.io/pypi/dm/c_uuid_v7?style=flat-square&color=007ec6)              | ![Repo size](https://img.shields.io/github/repo-size/lava-sh/c_uuid_v7?style=flat-square&color=007ec6)  |
| ![Python Version](https://img.shields.io/pypi/pyversions/c_uuid_v7?style=flat-square&color=007ec6) | ![Last Commit](https://img.shields.io/github/last-commit/lava-sh/c_uuid_v7?style=flat-square)           |

</div>

## Features

* The fastest UUID v7 generator in Python (see [benchmarks](https://github.com/lava-sh/c_uuid_v7/tree/main/benchmark))

## Installation

Using pip

```bash
pip install c_uuid_v7
```

Using uv

```bash
uv pip install c_uuid_v7
```

## Example

```python
import c_uuid_v7

print(c_uuid_v7.uuid7())  # 019d1ab2-cfea-71f3-ab07-0bf844ff9149
print(c_uuid_v7.compat.uuid7())  # 019d1ab2-cfea-71f3-ab07-0bf98a94016c
```

## Compatibility with Python [uuid.UUID](https://docs.python.org/3/library/uuid.html)

In some cases, for example if you are using `Django`, you might
need [uuid.UUID](https://docs.python.org/3/library/uuid.html) instances to be returned
from the standard-library `uuid`, not a custom `UUID` class.

In that case you can use the `c_uuid_v7.compat` which comes with a performance penalty
in comparison with the `c_uuid_v7` default behaviour, but is still faster than the standard-library.

```py
import c_uuid_v7.compat as uuid

# make a random UUID
print(repr(uuid.uuid7()))
# UUID('019d1ab3-f95a-79df-b868-56fe41c33af3')
```

## Supported wheels

| Platform     | Architectures                   | manylinux        | musllinux       | CPython   | PyPy |
|--------------|---------------------------------|------------------|-----------------|-----------|------|
| Linux x86_64 | `x86_64`, `i686`                | `manylinux_2_28` | `musllinux_1_2` | 3.10–3.14 | 3.11 |
| Linux ARM64  | `aarch64`                       | `manylinux_2_28` | `musllinux_1_2` | 3.10–3.14 | 3.11 |
| Linux ARMv7  | `armv7l`                        | `manylinux_2_31` | `musllinux_1_2` | 3.10–3.14 | 3.11 |
| Linux POWER  | `ppc64le`                       | `manylinux_2_28` | `musllinux_1_2` | 3.10–3.14 | 3.11 |
| Linux s390x  | `s390x`                         | `manylinux_2_28` | `musllinux_1_2` | 3.10–3.14 | 3.11 |
| Linux RISC-V | `riscv64`                       | `manylinux_2_39` | `musllinux_1_2` | 3.10–3.14 | 3.11 |
| Windows      | `AMD64`, `x86`, `ARM64`         | —                | —               | 3.10–3.14 | 3.11 |
| macOS        | `x86_64`, `arm64`, `universal2` | —                | —               | 3.10–3.14 | 3.11 |
