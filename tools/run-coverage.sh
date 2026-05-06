#!/usr/bin/env bash
# Collect source-based LLVM coverage for src/ by running the test suite twice:
#   1. host        — all correctness, generator, error, and autoscheduler tests
#   2. host-metal  — only tests whose name contains "gpu"
#
# Prerequisites: Homebrew LLVM 21 (for llvm-profdata and llvm-cov).
#
# Usage: tools/run-coverage.sh [--report-only]
#   --report-only  Skip build and test steps; re-merge existing profraw files
#                  and regenerate all reports. Useful after re-running a single
#                  test manually.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build/macOS-coverage"
LLVM_DIR="/opt/homebrew/opt/llvm@21/bin"
NPROC=$(sysctl -n hw.logicalcpu)

REPORT_ONLY=0
for arg in "$@"; do
    case "$arg" in
        --report-only) REPORT_ONLY=1 ;;
        *)
            echo "Unknown argument: $arg" >&2
            exit 1
            ;;
    esac
done

cd "$REPO_ROOT"

if [[ $REPORT_ONLY -eq 0 ]]; then

    # ── Configure + Build ─────────────────────────────────────────────────────────
    cmake --preset macOS-coverage
    cmake --build "$BUILD_DIR" -j"$NPROC"

    # ── Run 1: host — all tests ──────────────────────────────────────────────────
    mkdir -p "$BUILD_DIR/profiles"
    ctest --preset macOS-coverage -j"$NPROC" || echo "Warning: some tests failed (see above)"

    # ── Reconfigure + rebuild for host-metal ─────────────────────────────────────
    cmake --preset macOS-coverage -DHalide_TARGET=host-metal
    cmake --build "$BUILD_DIR" -j"$NPROC"

    # ── Run 2: host-metal — GPU tests only ───────────────────────────────────────
    ctest --preset macOS-coverage -j"$NPROC" -R gpu || echo "Warning: some tests failed (see above)"

fi # REPORT_ONLY

# ── Merge profiles ────────────────────────────────────────────────────────────
"$LLVM_DIR/llvm-profdata" merge \
    "$BUILD_DIR/profiles"/*.profraw \
    -o "$BUILD_DIR/merged.profdata"

# ── Generate coverage reports ─────────────────────────────────────────────────
HALIDE_LIB=$(find "$BUILD_DIR/src" -maxdepth 1 -name "libHalide.*.*.*.dylib" | head -1)
OBJECTS=(-object "$HALIDE_LIB")

while IFS= read -r bin; do
    OBJECTS+=(-object "$bin")
done < <(
    # Exclude generator_aot_* and generator_aotcpp_*: they run pre-compiled
    # Halide pipelines at test time and add no unique src/ coverage.
    find "$BUILD_DIR/test" -maxdepth 2 -type f -perm +0111 |
        grep -v '/generator_aot_\|/generator_aotcpp_' | sort
)

COV_ARGS=(
    --instr-profile="$BUILD_DIR/merged.profdata"
    "${OBJECTS[@]}"
    --ignore-filename-regex='build/|test/|tutorial/|python_bindings/|apps/|packaging/|include/'
    --sources "$REPO_ROOT/src"
)

"$LLVM_DIR/llvm-cov" report "${COV_ARGS[@]}" 2>/dev/null >"$BUILD_DIR/coverage-report.txt"
"$LLVM_DIR/llvm-cov" show "${COV_ARGS[@]}" --format=html --output-dir="$BUILD_DIR/coverage-html" 2>/dev/null
"$LLVM_DIR/llvm-cov" export "${COV_ARGS[@]}" --format=lcov 2>/dev/null >"$BUILD_DIR/coverage.info"

echo "Coverage report: $BUILD_DIR/coverage-report.txt"
echo "LCOV data:       $BUILD_DIR/coverage.info"
echo "HTML report:     $BUILD_DIR/coverage-html/index.html"
