import platform
import sys
import time
import uuid
from collections.abc import Callable
from importlib.metadata import (
    PackageNotFoundError,
    version as get_lib_version,
)
from operator import itemgetter
from pathlib import Path
from typing import TypeAlias

import altair as alt
import cpuinfo
import fastuuid
import lastuuid
import polars as pl
import uuid6
import uuid7 as uuid7gen
import uuid_utils
import uuid_utils.compat as uuid_utils_compat
from uuid_v7.base import uuid7 as uuid_v7_uuid7

import c_uuid_v7
import c_uuid_v7.compat as c_uuid_v7_compat

N = 500_000

FILE_PATH = Path(__file__).resolve().parent
CPU = cpuinfo.get_cpu_info()["brand_raw"]
PY_VERSION = f"{platform.python_version()} ({platform.system()} {platform.release()})"
BenchmarkCase: TypeAlias = tuple[Callable[[], object], str]


def _benchmark(func: Callable[[], object], count: int) -> float:
    start = time.perf_counter()
    for _ in range(count):
        func()
    end = time.perf_counter()
    return end - start


def benchmark(
    cases: dict[str, BenchmarkCase],
    count: int,
) -> dict[str, float]:
    return {
        name: _benchmark(func, count)
        for name, (func, _) in cases.items()
    }


def get_label(name: str, package_name: str) -> str:
    try:
        version = get_lib_version(package_name)
    except PackageNotFoundError:
        version = "stdlib"
    return f"{name}\n{version}"


def plot_benchmark(
    cases: dict[str, BenchmarkCase],
    results: dict[str, float],
    save_path: Path,
    scenario: str,
) -> None:
    df = pl.DataFrame({
        "implementation": list(results.keys()),
        "exec_time": list(results.values()),
    }).sort("exec_time")

    df = df.with_columns(
        (pl.col("exec_time") / pl.col("exec_time").min()).alias("slowdown"),
    )

    df = df.with_columns(
        pl.Series(
            "implementation_label",
            [get_label(name, cases[name][1]) for name in df["implementation"]],
        ),
    )

    chart = (
        alt.Chart(df)
        .mark_bar(cornerRadiusTopLeft=6, cornerRadiusTopRight=6)
        .encode(
            x=alt.X(
                "implementation_label:N",
                sort=None,
                title="Implementation",
                axis=alt.Axis(
                    labelAngle=0,
                    labelExpr="split(datum.label, '\\n')",
                    labelLineHeight=14,
                ),
            ),
            y=alt.Y(
                "exec_time:Q",
                title="Execution Time (seconds, lower=better)",
                scale=alt.Scale(domain=(0, df["exec_time"].max() * 1.04)),
                axis=alt.Axis(grid=False),
            ),
            color=alt.Color(
                "implementation:N",
                legend=None,
                scale=alt.Scale(scheme="dark2"),
            ),
            tooltip=[
                alt.Tooltip("implementation:N", title=""),
                alt.Tooltip("exec_time:Q", title="Execution Time (s)", format=".4f"),
                alt.Tooltip("slowdown:Q", title="Slowdown", format=".2f"),
            ],
        )
    )

    text = (
        chart.mark_text(
            align="center",
            baseline="bottom",
            dy=-2,
            fontSize=9,
            fontWeight="bold",
        )
        .transform_calculate(
            label='format(datum.exec_time, ".4f") + '
            '"s (x" + format(datum.slowdown, ".2f") + ")"',
        )
        .encode(text="label:N")
    )

    (chart + text).properties(
        width=800,
        height=600,
        title={
            "text": f"UUID v7 benchmark ({scenario})",
            "subtitle": f"Python: {PY_VERSION} | CPU: {CPU}",
        },
    ).save(save_path)


def _format_table(results: dict[str, float]) -> str:
    ordered_results = sorted(results.items(), key=itemgetter(1))
    fastest_time = ordered_results[0][1]

    rows = [
        (
            name,
            f"{exec_time:.4f}",
            f"{exec_time / fastest_time:.1f}x",
        )
        for name, exec_time in ordered_results
    ]

    headers = ("uuid", "seconds", "relative")
    uuid_width = max(len(headers[0]), *(len(name) for name, _, _ in rows))
    seconds_width = max(len(headers[1]), *(len(seconds) for _, seconds, _ in rows))
    relative_width = max(len(headers[2]), *(len(relative) for _, _, relative in rows))

    separator = (
        f"+-{'-' * uuid_width}-+-{'-' * seconds_width}-+-{'-' * relative_width}-+"
    )
    header = (
        f"| {headers[0]:^{uuid_width}} | "
        f"{headers[1]:^{seconds_width}} | "
        f"{headers[2]:^{relative_width}} |"
    )
    lines = [separator, header, separator]

    for name, seconds, relative in rows:
        lines.append(
            f"| {name:^{uuid_width}} | "
            f"{seconds:^{seconds_width}} | "
            f"{relative:^{relative_width}} |",
        )

    lines.append(separator)
    return "\n".join(lines)


def _write_line(text: str = "") -> None:
    sys.stdout.write(f"{text}\n")


def print_benchmark(title: str, results: dict[str, float]) -> None:
    _write_line(title)
    _write_line(_format_table(results))
    _write_line()


def _print_saved_chart(path: Path) -> None:
    _write_line(f"Saved chart: {path.name}")


def run(run_count: int) -> None:
    cases = {
        "uuid": (uuid.uuid7, "uuid"),
        "c_uuid_v7": (c_uuid_v7.uuid7, "c_uuid_v7"),
        "uuid_utils": (uuid_utils.uuid7, "uuid-utils"),
        "fastuuid": (fastuuid.uuid7, "fastuuid"),
        "uuid_v7": (uuid_v7_uuid7, "uuid-v7"),
        "uuid6": (uuid6.uuid7, "uuid6"),
        "lastuuid": (lastuuid.uuid7, "lastuuid"),
        "UUIDv7gen": (uuid7gen.UUIDv7().generate, "UUIDv7gen"),
    }

    compact_cases = {
        "c_uuid_v7.compat": (c_uuid_v7_compat.uuid7, "c_uuid_v7"),
        "uuid_utils.compat": (uuid_utils_compat.uuid7, "uuid-utils"),
        "uuid_v7": (uuid_v7_uuid7, "uuid-v7"),
        "uuid6": (uuid6.uuid7, "uuid6"),
        "lastuuid": (lastuuid.uuid7, "lastuuid"),
        "uuid": (uuid.uuid7, "uuid"),
    }

    _write_line(f"Python: {PY_VERSION}")
    _write_line(f"CPU: {CPU}")
    _write_line(f"Iterations: {run_count}")
    _write_line()

    results = benchmark(cases, run_count)
    compact_results = benchmark(compact_cases, run_count)

    print_benchmark(
        "UUID v7 benchmark (uuid7() default APIs)",
        results,
    )
    print_benchmark(
        "UUID v7 benchmark (uuid7() stdlib-compatible APIs)",
        compact_results,
    )

    default_chart_path = FILE_PATH / "uuid7.svg"
    compact_chart_path = FILE_PATH / "uuid7-compact.svg"

    plot_benchmark(cases, results, default_chart_path, "uuid7() default APIs")
    plot_benchmark(
        compact_cases,
        compact_results,
        compact_chart_path,
        "uuid7() stdlib-compatible APIs",
    )

    _print_saved_chart(default_chart_path)
    _print_saved_chart(compact_chart_path)


if __name__ == "__main__":
    run(N)
