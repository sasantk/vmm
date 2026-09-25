#!/usr/bin/env python3

import csv
import math
import statistics
import sys
from pathlib import Path

REQUIRED_COLUMNS = {
    "run_id",
    "workload",
    "iterations",
    "runtime_ns",
    "userspace_exits",
    "io_exits",
    "hlt_exits",
    "host_cpu",
    "git_commit",
}


def percentile(values, p):
    """
    Linear interpolation percentile.

    p must be between 0.0 and 1.0.
    """
    values = sorted(values)

    if not values:
        raise ValueError("cannot calculate percentile of empty data")

    pos = (len(values) - 1) * p
    lower = math.floor(pos)
    upper = math.ceil(pos)

    if lower == upper:
        return float(values[lower])

    fraction = pos - lower

    return values[lower] + (values[upper] - values[lower]) * fraction


def main():
    if len(sys.argv) != 2:
        print(
            f"usage: {sys.argv[0]} <raw.csv>",
            file=sys.stderr,
        )
        return 1

    input_path = Path(sys.argv[1])

    if not input_path.is_file():
        print(
            f"input file does not exist: {input_path}",
            file=sys.stderr,
        )
        return 1

    runtimes = []

    with input_path.open(newline="") as f:
        reader = csv.DictReader(f)

        if reader.fieldnames is None:
            print("CSV has no header", file=sys.stderr)
            return 1

        missing = REQUIRED_COLUMNS - set(reader.fieldnames)

        if missing:
            print(
                f"missing required columns: {', '.join(sorted(missing))}",
                file=sys.stderr,
            )
            return 1

        rows = list(reader)

    if len(rows) < 30:
        print(
            f"expected at least 30 measured runs, got {len(rows)}",
            file=sys.stderr,
        )
        return 1

    workloads = {row["workload"] for row in rows}
    iterations_set = {row["iterations"] for row in rows}
    host_cpus = {row["host_cpu"] for row in rows}
    commits = {row["git_commit"] for row in rows}

    if len(workloads) != 1:
        print("multiple workloads found in one raw file", file=sys.stderr)
        return 1

    if len(iterations_set) != 1:
        print(
            "multiple iteration counts found in one raw file", file=sys.stderr
        )
        return 1

    if len(host_cpus) != 1:
        print("multiple host CPUs found in one raw file", file=sys.stderr)
        return 1

    if len(commits) != 1:
        print("multiple git commits found in one raw file", file=sys.stderr)
        return 1

    expected_iterations = int(next(iter(iterations_set)))

    for row in rows:
        runtime_ns = int(row["runtime_ns"])
        userspace_exits = int(row["userspace_exits"])
        io_exits = int(row["io_exits"])
        hlt_exits = int(row["hlt_exits"])

        if io_exits != expected_iterations:
            print(
                f"run {row['run_id']}: "
                f"expected {expected_iterations} IO exits, got {io_exits}",
                file=sys.stderr,
            )
            return 1

        if hlt_exits != 1:
            print(
                f"run {row['run_id']}: "
                f"expected 1 HLT exit, got {hlt_exits}",
                file=sys.stderr,
            )
            return 1

        if userspace_exits != expected_iterations + 1:
            print(
                f"run {row['run_id']}: "
                f"expected {expected_iterations + 1} userspace exits, "
                f"got {userspace_exits}",
                file=sys.stderr,
            )
            return 1

        runtimes.append(runtime_ns)

    q1 = percentile(runtimes, 0.25)
    median = statistics.median(runtimes)
    q3 = percentile(runtimes, 0.75)
    p95 = percentile(runtimes, 0.95)

    summary = {
        "source_file": input_path.name,
        "workload": next(iter(workloads)),
        "iterations": expected_iterations,
        "runs": len(rows),
        "host_cpu": next(iter(host_cpus)),
        "git_commit": next(iter(commits)),
        "min_ns": min(runtimes),
        "median_ns": median,
        "p95_ns": p95,
        "q1_ns": q1,
        "q3_ns": q3,
        "iqr_ns": q3 - q1,
        "max_ns": max(runtimes),
    }

    output_dir = Path("data/processed")
    output_dir.mkdir(parents=True, exist_ok=True)

    output_path = output_dir / f"{input_path.stem}-summary.csv"

    with output_path.open("w", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=summary.keys(),
        )
        writer.writeheader()
        writer.writerow(summary)

    print(f"source:   {input_path}")
    print(f"runs:     {summary['runs']}")
    print(f"cpu:      {summary['host_cpu']}")
    print(f"commit:   {summary['git_commit']}")
    print()
    print(f"min:      {summary['min_ns'] / 1_000_000:.3f} ms")
    print(f"median:   {summary['median_ns'] / 1_000_000:.3f} ms")
    print(f"p95:      {summary['p95_ns'] / 1_000_000:.3f} ms")
    print(f"q1:       {summary['q1_ns'] / 1_000_000:.3f} ms")
    print(f"q3:       {summary['q3_ns'] / 1_000_000:.3f} ms")
    print(f"IQR:      {summary['iqr_ns'] / 1_000_000:.3f} ms")
    print(f"max:      {summary['max_ns'] / 1_000_000:.3f} ms")
    print()
    print(f"written:  {output_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
