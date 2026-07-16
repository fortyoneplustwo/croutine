#!/usr/bin/env bash
#
# Benchmarks two fizzbuzz binaries (e.g. fiber-based vs. thread-based)
# over several runs, pinned to a single core, and reports median + range.
#
# Usage:
#   ./bench_fizzbuzz.sh <fiber_binary> <threads_binary> [duration_seconds] [num_runs] [cpu_core]
#
# Example:
#   ./bench_fizzbuzz.sh ~/rc/croutine/a ~/rc/high-throughput-fizzbuzz/a.out 10 5 0

set -euo pipefail

FIBER_BIN="${1:?Usage: $0 <fiber_binary> <threads_binary> [duration] [runs] [cpu_core]}"
THREADS_BIN="${2:?Usage: $0 <fiber_binary> <threads_binary> [duration] [runs] [cpu_core]}"
DURATION="${3:-10}"
RUNS="${4:-5}"
CPU_CORE="${5:-0}"

# Run one measurement: pin to a core, run for $DURATION seconds, count bytes,
# compute MiB/s from actual elapsed wall time (not just $DURATION, since
# timeout + process startup/teardown adds a little slack).
run_once() {
  local bin="$1"
  local start end bytes elapsed mibs

  start=$(date +%s.%N)
  bytes=$(timeout "${DURATION}s" taskset -c "$CPU_CORE" "$bin" | wc -c)
  end=$(date +%s.%N)

  elapsed=$(awk -v s="$start" -v e="$end" 'BEGIN { print e - s }')
  mibs=$(awk -v b="$bytes" -v t="$elapsed" 'BEGIN { printf "%.2f", (b / 1048576) / t }')
  echo "$mibs"
}

# Compute median and min/max of a list of numbers passed as args.
summarize() {
  local -a vals=("$@")
  local n=${#vals[@]}
  local sorted
  sorted=$(printf '%s\n' "${vals[@]}" | sort -n)

  local min max median
  min=$(echo "$sorted" | head -n1)
  max=$(echo "$sorted" | tail -n1)

  if (( n % 2 == 1 )); then
    median=$(echo "$sorted" | sed -n "$(( (n+1)/2 ))p")
  else
    local a b
    a=$(echo "$sorted" | sed -n "$(( n/2 ))p")
    b=$(echo "$sorted" | sed -n "$(( n/2 + 1 ))p")
    median=$(awk -v a="$a" -v b="$b" 'BEGIN { printf "%.2f", (a + b) / 2 }')
  fi

  echo "median=${median} min=${min} max=${max}"
}

bench() {
  local label="$1"
  local bin="$2"
  local -a results=()

  echo "== $label ($bin) =="
  for i in $(seq 1 "$RUNS"); do
    local mibs
    mibs=$(run_once "$bin")
    echo "  run $i: ${mibs} MiB/s"
    results+=("$mibs")
    sleep 1  # brief cooldown between runs
  done

  echo "  -> $(summarize "${results[@]}")"
  echo
}

echo "Benchmarking: $RUNS runs x ${DURATION}s each, pinned to core $CPU_CORE"
echo

bench "fiber runtime" "$FIBER_BIN"
bench "threads" "$THREADS_BIN"
