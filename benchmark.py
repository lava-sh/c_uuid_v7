import sys
import time
import uuid
from collections.abc import Callable
from typing import TypeAlias

import fastuuid
import lastuuid
import uuid6
import uuid7 as uuid7gen
import uuid_utils
import uuid_utils.compat as uuid_utils_compat
from uuid_v7.base import uuid7 as uuid_v7_uuid7

import c_uuid_v7
import c_uuid_v7.compat as c_uuid_v7_compat

DEFAULT_COUNT = 500
BenchmarkCase: TypeAlias = tuple[str, Callable[[], object]]


def _benchmark(func: Callable[[], object], count: int) -> float:
    start = time.perf_counter()
    for _ in range(count):
        func()
    end = time.perf_counter()
    return end - start


def _format_results(title: str, cases: list[BenchmarkCase], count: int) -> str:
    results = [(name, _benchmark(func, count)) for name, func in cases]
    results.sort(key=lambda item: item[1])

    fastest = results[0][1]
    lines = [title, f"iterations: {count}", ""]

    for index, (name, elapsed) in enumerate(results, start=1):
        slowdown = elapsed / fastest
        lines.append(
            f"{index:>2}. {name:<20} {elapsed:.6f}s  x{slowdown:.2f}",
        )

    return "\n".join(lines)


def main() -> None:
    count = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_COUNT

    default_cases: list[BenchmarkCase] = [
        ("uuid", uuid.uuid7),
        ("c_uuid_v7", c_uuid_v7.uuid7),
        ("uuid_utils", uuid_utils.uuid7),
        ("fastuuid", fastuuid.uuid7),
        ("uuid_v7", uuid_v7_uuid7),
        ("uuid6", uuid6.uuid7),
        ("lastuuid", lastuuid.uuid7),
        ("UUIDv7gen", uuid7gen.UUIDv7().generate),
    ]

    compat_cases: list[BenchmarkCase] = [
        ("c_uuid_v7.compat", c_uuid_v7_compat.uuid7),
        ("uuid_utils.compat", uuid_utils_compat.uuid7),
        ("uuid_v7", uuid_v7_uuid7),
        ("uuid6", uuid6.uuid7),
        ("lastuuid", lastuuid.uuid7),
        ("uuid", uuid.uuid7),
    ]

    print(_format_results("uuid7() default APIs", default_cases, count))
    print()
    print(_format_results("uuid7() stdlib-compatible APIs", compat_cases, count))


if __name__ == "__main__":
    main()
