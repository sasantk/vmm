#!/usr/bin/env bash

set -euo pipefail

WARMUPS=5
RUNS=30
CPU="${CPU:-2}"

BIN="./bin/kvm-sample"
RAW_DIR="data/raw"

mkdir -p "$RAW_DIR"

if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
    echo "Building release binary..."
    make release >/dev/null
fi

git_commit="$(git rev-parse --short HEAD)"

if [[ -n "$(git status --porcelain)" ]]; then
    git_commit="${git_commit}-dirty"
fi

timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
output_file="${RAW_DIR}/pio-cpu${CPU}-${timestamp}.csv"

echo "Pinned CPU: ${CPU}"
echo "Running ${WARMUPS} warm-up runs..."

for ((i = 1; i <= WARMUPS; i++)); do
    if ! taskset -c "$CPU" "$BIN" >/dev/null; then
        echo "warm-up run $i failed" >&2
        exit 1
    fi
done

echo "run_id,workload,iterations,runtime_ns,userspace_exits,io_exits,hlt_exits,host_cpu,git_commit" \
    > "$output_file"

echo "Running ${RUNS} measured runs..."

for ((run_id = 1; run_id <= RUNS; run_id++)); do
    if ! result="$(taskset -c "$CPU" "$BIN")"; then
        echo "measured run $run_id failed" >&2
        exit 1
    fi

    workload=""
    iterations=""
    runtime_ns=""
    userspace_exits=""
    io_exits=""
    hlt_exits=""

    for field in $result; do
        case "$field" in
            workload=*)
                workload="${field#*=}"
                ;;
            iterations=*)
                iterations="${field#*=}"
                ;;
            runtime_ns=*)
                runtime_ns="${field#*=}"
                ;;
            userspace_exits=*)
                userspace_exits="${field#*=}"
                ;;
            io_exits=*)
                io_exits="${field#*=}"
                ;;
            hlt_exits=*)
                hlt_exits="${field#*=}"
                ;;
        esac
    done

    if [[ -z "$workload" ||
          -z "$iterations" ||
          -z "$runtime_ns" ||
          -z "$userspace_exits" ||
          -z "$io_exits" ||
          -z "$hlt_exits" ]]; then
        echo "invalid output from run $run_id:" >&2
        echo "$result" >&2
        exit 1
    fi

    echo \
        "${run_id},${workload},${iterations},${runtime_ns},${userspace_exits},${io_exits},${hlt_exits},${CPU},${git_commit}" \
        >> "$output_file"

    echo "run ${run_id}/${RUNS}: ${runtime_ns} ns"
done

echo
echo "Benchmark complete:"
echo "$output_file"

echo
echo "Generating summary..."

python3 ./bench/summarize.py "$output_file"
