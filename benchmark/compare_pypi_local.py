from __future__ import annotations

import argparse
import json
import os
import statistics
import subprocess  # noqa: S404
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TMP_BENCH = ROOT / ".tmp_bench"
PYPI_PYTHON = TMP_BENCH / "pypi" / "Scripts" / "python.exe"
LOCAL_PYTHON = TMP_BENCH / "local" / "Scripts" / "python.exe"
CASES = (
    ("uuid7", "c_uuid_v7.uuid7"),
    ("compat.uuid7", "c_uuid_v7.compat.uuid7"),
)

SCRIPT = """
from __future__ import annotations

import gc
import json
import statistics
import time
from importlib.metadata import version

import c_uuid_v7
import c_uuid_v7.compat as compat
from c_uuid_v7._core import _uuid7

ITERATIONS = {iterations}
REPEATS = {repeats}
WARMUP = {warmup}
CASES = (
    ("uuid7", c_uuid_v7.uuid7),
    ("compat.uuid7", compat.uuid7),
)

results = {{}}
for name, func in CASES:
    for _ in range(WARMUP):
        func()
    timings = []
    for _ in range(REPEATS):
        gc.collect()
        start = time.perf_counter()
        for _ in range(ITERATIONS):
            func()
        timings.append(time.perf_counter() - start)
    best = min(timings)
    median = statistics.median(timings)
    mean = statistics.fmean(timings)
    results[name] = {{
        "best": best,
        "median": median,
        "mean": mean,
        "ops": ITERATIONS / best,
        "median_ops": ITERATIONS / median,
        "ns": best / ITERATIONS * 1_000_000_000,
        "median_ns": median / ITERATIONS * 1_000_000_000,
    }}

print(json.dumps({{"version": version("c_uuid_v7"), "results": results}}))
"""


def run_bench(
    python: Path,
    iterations: int,
    repeats: int,
    warmup: int,
) -> dict[str, object]:
    with tempfile.NamedTemporaryFile(
        mode="w+",
        encoding="utf-8",
        suffix=".json",
        delete=False,
    ) as output_file:
        output_path = Path(output_file.name)

    env = os.environ.copy()
    env["C_UUID_V7_BENCH_OUT"] = str(output_path)
    argv = [
        str(python),
        "-c",
        SCRIPT.format(iterations=iterations, repeats=repeats, warmup=warmup)
        + "\nfrom pathlib import Path\n"
        + "Path(__import__('os').environ['C_UUID_V7_BENCH_OUT']).write_text("
        + "json.dumps({'version': version('c_uuid_v7'), 'results': results}),"
        + " encoding='utf-8')\n",
    ]
    try:
        completed = subprocess.run(  # noqa: S603
            argv,
            check=False,
            capture_output=True,
            text=True,
            cwd=ROOT,
            env=env,
        )
        if completed.returncode != 0:
            msg = (
                f"Benchmark failed for {python} with exit code {completed.returncode}\n"
                f"{completed.stderr.strip()}"
            )
            raise SystemExit(msg)
        return json.loads(output_path.read_text(encoding="utf-8"))
    finally:
        output_path.unlink(missing_ok=True)


def format_line(
    case: str,
    baseline: dict[str, float],
    candidate: dict[str, float],
    baseline_name: str,
    candidate_name: str,
) -> tuple[str, str, str, str]:
    ratio = baseline["median_ops"] / candidate["median_ops"]
    faster = baseline_name if ratio > 1 else candidate_name
    speedup = ratio if ratio > 1 else 1 / ratio
    baseline_cell = (
        f"{baseline['median_ops']:>12,.0f} ops/s"
        f" | {baseline['median_ns']:>7.1f} ns"
        f" | best {baseline['ops']:>12,.0f}"
    )
    candidate_cell = (
        f"{candidate['median_ops']:>12,.0f} ops/s"
        f" | {candidate['median_ns']:>7.1f} ns"
        f" | best {candidate['ops']:>12,.0f}"
    )
    faster_cell = f"{faster} x{speedup:.3f}"
    return case, baseline_cell, candidate_cell, faster_cell


def format_table(
    rows: list[tuple[str, str, str, str]],
    baseline_name: str,
    candidate_name: str,
) -> str:
    headers = ("case", baseline_name, candidate_name, "faster")
    widths = [
        max(len(headers[0]), *(len(row[0]) for row in rows)),
        max(len(headers[1]), *(len(row[1]) for row in rows)),
        max(len(headers[2]), *(len(row[2]) for row in rows)),
        max(len(headers[3]), *(len(row[3]) for row in rows)),
    ]

    def make_row(columns: tuple[str, str, str, str]) -> str:
        return (
            f"| {columns[0]:<{widths[0]}} "
            f"| {columns[1]:<{widths[1]}} "
            f"| {columns[2]:<{widths[2]}} "
            f"| {columns[3]:<{widths[3]}} |"
        )

    separator = (
        f"+-{'-' * widths[0]}-+"
        f"-{'-' * widths[1]}-+"
        f"-{'-' * widths[2]}-+"
        f"-{'-' * widths[3]}-+"
    )
    table_lines = [separator, make_row(headers), separator]
    table_lines.extend(make_row(row) for row in rows)
    table_lines.append(separator)
    return "\n".join(table_lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("-n", "--iterations", type=int, default=1_000_000)
    parser.add_argument("-r", "--repeats", type=int, default=7)
    parser.add_argument("-w", "--warmup", type=int, default=50_000)
    parser.add_argument("--baseline-python", type=Path, default=PYPI_PYTHON)
    parser.add_argument("--candidate-python", type=Path, default=LOCAL_PYTHON)
    parser.add_argument("--baseline-name", default="pypi")
    parser.add_argument("--candidate-name", default="local")
    args = parser.parse_args()

    missing = [
        str(path)
        for path in (args.baseline_python, args.candidate_python)
        if not path.exists()
    ]
    if missing:
        msg = f"Missing benchmark environments: {', '.join(missing)}"
        raise SystemExit(msg)

    baseline = run_bench(
        args.baseline_python,
        args.iterations,
        args.repeats,
        args.warmup,
    )
    candidate = run_bench(
        args.candidate_python,
        args.iterations,
        args.repeats,
        args.warmup,
    )

    lines = [
        f"{args.baseline_name}:  {baseline['version']}",
        f"{args.candidate_name}: {candidate['version']}",
        f"iterations={args.iterations:,}  repeats={args.repeats}  warmup={args.warmup:,}",
    ]
    rows: list[tuple[str, str, str, str]] = []

    for case, _ in CASES:
        rows.append(
            format_line(
                case,
                baseline["results"][case],
                candidate["results"][case],
                args.baseline_name,
                args.candidate_name,
            ),
        )

    lines.extend(("", format_table(rows, args.baseline_name, args.candidate_name)))

    baseline_ops = [baseline["results"][case]["median_ops"] for case, _ in CASES]
    candidate_ops = [candidate["results"][case]["median_ops"] for case, _ in CASES]
    geomean_line = (
        "geomean median ops/s: "
        f"{args.baseline_name}={statistics.geometric_mean(baseline_ops):,.0f}, "
        f"{args.candidate_name}={statistics.geometric_mean(candidate_ops):,.0f}"
    )
    lines.extend(("", geomean_line))
    os.write(1, "\n".join(lines).encode())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
