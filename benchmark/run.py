import platform
import time
from collections.abc import Callable
from importlib.metadata import version as get_lib_version
from pathlib import Path

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


def benchmark(func: Callable, count: int) -> float:
    start = time.perf_counter()
    for _ in range(count):
        func()
    end = time.perf_counter()
    return end - start


def benchmark_cases(cases: dict[str, Callable], count: int) -> dict[str, float]:
    return {
        name: benchmark(func, count)
        for name, func in cases.items()
    }  # fmt: skip


def plot_benchmark(
    results: dict[str, float],
    save_path: Path,
    scenario: str,
    package_names: dict[str, str],
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
            [
                f"{name}\nv{get_lib_version(package_names[name])}"
                for name in df["implementation"]
            ],
        ),
    )

    chart = (
        alt
        .Chart(df)
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
        chart
        .mark_text(
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


def run(run_count: int) -> None:
    default_generator = uuid7gen.UUIDv7()

    default_cases = {
        "c_uuid_v7": c_uuid_v7.uuid7,
        "uuid_utils": uuid_utils.uuid7,
        "fastuuid": fastuuid.uuid7,
        "uuid_v7": uuid_v7_uuid7,
        "uuid6": uuid6.uuid7,
        "lastuuid": lastuuid.uuid7,
        "UUIDv7gen": default_generator.generate,
    }
    default_package_names = {
        "c_uuid_v7": "c_uuid_v7",
        "uuid_utils": "uuid-utils",
        "uuid_v7": "uuid-v7",
        "uuid6": "uuid6",
        "fastuuid": "fastuuid",
        "lastuuid": "lastuuid",
        "UUIDv7gen": "UUIDv7gen",
    }

    compact_cases = {
        "c_uuid_v7.compat": c_uuid_v7_compat.uuid7,
        "uuid_utils.compat": uuid_utils_compat.uuid7,
        "uuid_v7": uuid_v7_uuid7,
        "uuid6": uuid6.uuid7,
        "lastuuid": lastuuid.uuid7,
    }
    compact_package_names = {
        "c_uuid_v7.compat": "c_uuid_v7",
        "uuid_utils.compat": "uuid-utils",
        "uuid_v7": "uuid-v7",
        "uuid6": "uuid6",
        "lastuuid": "lastuuid",
    }

    plot_benchmark(
        benchmark_cases(default_cases, run_count),
        FILE_PATH / "uuid7-default.svg",
        "uuid7() default APIs",
        default_package_names,
    )
    plot_benchmark(
        benchmark_cases(compact_cases, run_count),
        FILE_PATH / "uuid7-compact.svg",
        "uuid7() compact / stdlib-compatible APIs",
        compact_package_names,
    )


if __name__ == "__main__":
    run(N)
