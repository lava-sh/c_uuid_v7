from __future__ import annotations

import argparse
import gc
import platform
import statistics
import sys
import time
from collections.abc import Callable

import c_uuid_v7

def bench_case(func: Callable[[], object], iterations: int, repeats: int) -> dict[str, float]:
    timings: list[float] = []

    for _ in range(repeats):
        gc.collect()
        start = time.perf_counter()
        for _ in range(iterations):
            func()
        elapsed = time.perf_counter() - start
        timings.append(elapsed)

    best = min(timings)
    mean = statistics.fmean(timings)
    ops_per_sec = iterations / best if best else float("inf")
    ns_per_op = (best / iterations) * 1_000_000_000 if iterations else 0.0

    return {
        "best": best,
        "mean": mean,
        "ops_per_sec": ops_per_sec,
        "ns_per_op": ns_per_op,
    }


def print_header(iterations: int, repeats: int) -> None:
    print(f"python: {sys.version.split()[0]} [{platform.python_implementation()}]")
    print(f"platform: {platform.platform()}")
    print(f"iterations: {iterations}")
    print(f"repeats: {repeats}")
    print()


def print_result(name: str, result: dict[str, float], baseline_ops: float | None) -> None:
    parts = [
        f"{name:22}",
        f"best={result['best']:.6f}s",
        f"mean={result['mean']:.6f}s",
        f"ops/s={result['ops_per_sec']:,.0f}",
        f"ns/op={result['ns_per_op']:.1f}",
    ]

    if baseline_ops:
        parts.append(f"rel={result['ops_per_sec'] / baseline_ops:.3f}x")

    print(" | ".join(parts))


def main() -> int:
    parser = argparse.ArgumentParser(description="Benchmark the public c_uuid_v7 UUIDv7 API.")
    parser.add_argument("-n", "--iterations", type=int, default=1_000_000)
    parser.add_argument("-r", "--repeats", type=int, default=7)
    args = parser.parse_args()

    cases: list[tuple[str, Callable[[], object]]] = [("uuid7()", c_uuid_v7.uuid7)]

    print_header(args.iterations, args.repeats)

    baseline_ops: float | None = None
    for name, func in cases:
        result = bench_case(func, args.iterations, args.repeats)
        if baseline_ops is None:
            baseline_ops = result["ops_per_sec"]
        print_result(name, result, baseline_ops)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
