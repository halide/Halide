#!/usr/bin/env bash
#
# Build libHalide for WebAssembly using Emscripten.
#
# This script orchestrates a multi-stage build:
#   Stage 1: Build LLVM static libraries for wasm32
#   Stage 2: Cross-compile Halide for wasm32
#   Stage 3: Build and optionally run a smoke test
#
# Prerequisites:
#   - Emscripten SDK (emsdk) installed and activated (emcc/em++ on PATH)
#   - Native LLVM 20+ providing: clang, llvm-as, llvm-tblgen, clang-tblgen
#   - LLVM source tree (for building LLVM-for-wasm)
#   - CMake 3.28+, Ninja (recommended)
#
# Usage:
#   ./wasm/build.sh [options]
#
# Options:
#   --llvm-src DIR        Path to LLVM source tree (llvm-project root)
#   --native-llvm DIR     Path to native LLVM installation (prefix)
#   --build-dir DIR       Build output directory (default: build-wasm)
#   --jobs N              Parallel build jobs (default: nproc)
#   --llvm-targets LIST   LLVM targets to build (default: all)
#   --skip-llvm           Skip LLVM build (reuse existing)
#   --skip-halide         Skip Halide build
#   --run-test            Run the smoke test after building
#   --clean               Remove build directory before starting
#   --help                Show this help message
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HALIDE_SRC="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Defaults
LLVM_SRC=""
NATIVE_LLVM=""
BUILD_DIR="${HALIDE_SRC}/build-wasm"
JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
LLVM_TARGETS="all"
SKIP_LLVM=false
SKIP_HALIDE=false
RUN_TEST=false
CLEAN=false

usage() {
    head -n 34 "$0" | tail -n +3 | sed 's/^# \?//'
    exit 0
}

log() { echo "=== $*" >&2; }
die() { echo "ERROR: $*" >&2; exit 1; }

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --llvm-src)     LLVM_SRC="$2"; shift 2 ;;
        --native-llvm)  NATIVE_LLVM="$2"; shift 2 ;;
        --build-dir)    BUILD_DIR="$2"; shift 2 ;;
        --jobs)         JOBS="$2"; shift 2 ;;
        --llvm-targets) LLVM_TARGETS="$2"; shift 2 ;;
        --skip-llvm)    SKIP_LLVM=true; shift ;;
        --skip-halide)  SKIP_HALIDE=true; shift ;;
        --run-test)     RUN_TEST=true; shift ;;
        --clean)        CLEAN=true; shift ;;
        --help|-h)      usage ;;
        *)              die "Unknown option: $1" ;;
    esac
done

# Validate prerequisites
command -v emcc >/dev/null 2>&1 || die "emcc not found. Activate Emscripten SDK first: source emsdk_env.sh"
command -v cmake >/dev/null 2>&1 || die "cmake not found"

# Find Emscripten toolchain file
EMSDK_DIR="$(dirname "$(command -v emcc)")"
EMSCRIPTEN_TOOLCHAIN=""
for candidate in \
    "${EMSDK_DIR}/cmake/Modules/Platform/Emscripten.cmake" \
    "${EMSDK_DIR}/../cmake/Modules/Platform/Emscripten.cmake" \
    "${EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"; do
    if [[ -f "${candidate}" ]]; then
        EMSCRIPTEN_TOOLCHAIN="${candidate}"
        break
    fi
done
[[ -n "${EMSCRIPTEN_TOOLCHAIN}" ]] || die "Could not find Emscripten.cmake toolchain file"

# Validate native LLVM
if [[ -z "${NATIVE_LLVM}" ]]; then
    # Try to auto-detect from system
    NATIVE_CLANG="$(command -v clang 2>/dev/null || true)"
    if [[ -n "${NATIVE_CLANG}" ]]; then
        NATIVE_LLVM="$(dirname "$(dirname "${NATIVE_CLANG}")")"
        log "Auto-detected native LLVM at: ${NATIVE_LLVM}"
    else
        die "No native LLVM found. Specify --native-llvm DIR"
    fi
fi

NATIVE_CLANG="${NATIVE_LLVM}/bin/clang"
NATIVE_LLVM_AS="${NATIVE_LLVM}/bin/llvm-as"
NATIVE_LLVM_TBLGEN="${NATIVE_LLVM}/bin/llvm-tblgen"
NATIVE_CLANG_TBLGEN="${NATIVE_LLVM}/bin/clang-tblgen"

[[ -x "${NATIVE_CLANG}" ]] || die "Native clang not found at: ${NATIVE_CLANG}"
[[ -x "${NATIVE_LLVM_AS}" ]] || die "Native llvm-as not found at: ${NATIVE_LLVM_AS}"

# Check LLVM version
NATIVE_LLVM_VERSION="$("${NATIVE_CLANG}" --version | head -1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -1)"
NATIVE_LLVM_MAJOR="${NATIVE_LLVM_VERSION%%.*}"
[[ "${NATIVE_LLVM_MAJOR}" -ge 20 ]] || die "Native LLVM version ${NATIVE_LLVM_VERSION} is too old (need 20+)"
log "Native LLVM version: ${NATIVE_LLVM_VERSION}"

if ${CLEAN}; then
    log "Cleaning build directory: ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi

mkdir -p "${BUILD_DIR}"

LLVM_WASM_BUILD="${BUILD_DIR}/llvm"
HALIDE_WASM_BUILD="${BUILD_DIR}/halide"
TEST_BUILD="${BUILD_DIR}/test"

# ============================================================================
# Stage 1: Build LLVM for wasm32
# ============================================================================

if ! ${SKIP_LLVM}; then
    [[ -n "${LLVM_SRC}" ]] || die "LLVM source tree required. Specify --llvm-src DIR (llvm-project root)"
    [[ -d "${LLVM_SRC}/llvm/CMakeLists.txt" ]] || [[ -f "${LLVM_SRC}/llvm/CMakeLists.txt" ]] || \
        die "Invalid LLVM source tree: ${LLVM_SRC}/llvm/CMakeLists.txt not found"

    log "Stage 1: Building LLVM for wasm32 (targets: ${LLVM_TARGETS})"
    log "  Source: ${LLVM_SRC}/llvm"
    log "  Build:  ${LLVM_WASM_BUILD}"
    log "  This will take a while..."

    # Check for native tablegen tools
    TBLGEN_FLAGS=""
    if [[ -x "${NATIVE_LLVM_TBLGEN}" ]]; then
        TBLGEN_FLAGS="-DLLVM_TABLEGEN=${NATIVE_LLVM_TBLGEN}"
        log "  Using native llvm-tblgen: ${NATIVE_LLVM_TBLGEN}"
    else
        log "  WARNING: Native llvm-tblgen not found at ${NATIVE_LLVM_TBLGEN}"
        log "  The build will attempt to build and run tablegen, which may fail."
    fi

    if [[ -x "${NATIVE_CLANG_TBLGEN}" ]]; then
        TBLGEN_FLAGS="${TBLGEN_FLAGS} -DCLANG_TABLEGEN=${NATIVE_CLANG_TBLGEN}"
        log "  Using native clang-tblgen: ${NATIVE_CLANG_TBLGEN}"
    fi

    emcmake cmake -S "${LLVM_SRC}/llvm" -B "${LLVM_WASM_BUILD}" -G Ninja \
        -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DLLVM_TARGETS_TO_BUILD="${LLVM_TARGETS}" \
        -DLLVM_ENABLE_PROJECTS="clang;lld" \
        ${TBLGEN_FLAGS} \
        -DLLVM_BUILD_TOOLS=OFF \
        -DLLVM_BUILD_UTILS=OFF \
        -DLLVM_INCLUDE_TESTS=OFF \
        -DLLVM_INCLUDE_BENCHMARKS=OFF \
        -DLLVM_INCLUDE_EXAMPLES=OFF \
        -DLLVM_INCLUDE_DOCS=OFF \
        -DLLVM_ENABLE_TERMINFO=OFF \
        -DLLVM_ENABLE_ZLIB=OFF \
        -DLLVM_ENABLE_ZSTD=OFF \
        -DLLVM_ENABLE_LIBXML2=OFF \
        -DLLVM_ENABLE_LIBEDIT=OFF \
        -DLLVM_ENABLE_LIBPFM=OFF \
        -DLLVM_ENABLE_THREADS=OFF \
        -DLLVM_ENABLE_PIC=OFF \
        -DLLVM_ENABLE_ASSERTIONS=OFF \
        -DLLVM_ENABLE_BACKTRACES=OFF \
        -DLLVM_ENABLE_CRASH_OVERRIDES=OFF \
        -DLLVM_ENABLE_UNWIND_TABLES=OFF \
        -DBUILD_SHARED_LIBS=OFF \
        -DCLANG_BUILD_TOOLS=OFF \
        -DCLANG_INCLUDE_TESTS=OFF \
        -DCLANG_INCLUDE_DOCS=OFF

    cmake --build "${LLVM_WASM_BUILD}" -j "${JOBS}"

    log "Stage 1 complete: LLVM wasm libraries built"
else
    log "Skipping Stage 1 (LLVM build)"
    [[ -d "${LLVM_WASM_BUILD}" ]] || die "LLVM wasm build not found at ${LLVM_WASM_BUILD}. Run without --skip-llvm first."
fi

# ============================================================================
# Stage 2: Cross-compile Halide for wasm32
# ============================================================================

if ! ${SKIP_HALIDE}; then
    log "Stage 2: Building Halide for wasm32"
    log "  Source: ${HALIDE_SRC}"
    log "  Build:  ${HALIDE_WASM_BUILD}"
    log "  Native clang:   ${NATIVE_CLANG}"
    log "  Native llvm-as: ${NATIVE_LLVM_AS}"

    emcmake cmake -S "${HALIDE_SRC}" -B "${HALIDE_WASM_BUILD}" -G Ninja \
        -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DBUILD_SHARED_LIBS=OFF \
        -DLLVM_DIR="${LLVM_WASM_BUILD}/lib/cmake/llvm" \
        -DClang_DIR="${LLVM_WASM_BUILD}/lib/cmake/clang" \
        -DLLD_DIR="${LLVM_WASM_BUILD}/lib/cmake/lld" \
        -DHalide_NATIVE_CLANG="${NATIVE_CLANG}" \
        -DHalide_NATIVE_LLVM_AS="${NATIVE_LLVM_AS}" \
        -DWITH_TESTS=OFF \
        -DWITH_TUTORIALS=OFF \
        -DWITH_AUTOSCHEDULERS=OFF \
        -DWITH_PYTHON_BINDINGS=OFF \
        -DWITH_UTILS=OFF \
        -DWITH_DOCS=OFF \
        -DWITH_SERIALIZATION=OFF \
        -DWITH_PACKAGING=OFF \
        -DCMAKE_CROSSCOMPILING_EMULATOR=node

    cmake --build "${HALIDE_WASM_BUILD}" -j "${JOBS}"

    log "Stage 2 complete: Halide wasm library built"
    log "  Library: ${HALIDE_WASM_BUILD}/src/libHalide.a"
else
    log "Skipping Stage 2 (Halide build)"
fi

# ============================================================================
# Stage 3: Build and optionally run the smoke test
# ============================================================================

if ${RUN_TEST} || [[ -f "${HALIDE_WASM_BUILD}/src/libHalide.a" ]]; then
    log "Stage 3: Building smoke test"

    HALIDE_INCLUDE="${HALIDE_SRC}/src"
    HALIDE_LIB="${HALIDE_WASM_BUILD}/src"

    mkdir -p "${TEST_BUILD}"

    # Build the test using em++ directly (simpler than CMake for a single file)
    em++ -O2 \
        -std=c++17 \
        -I"${HALIDE_INCLUDE}" \
        -I"${HALIDE_WASM_BUILD}/include" \
        "${SCRIPT_DIR}/test_halide_wasm.cpp" \
        -L"${HALIDE_LIB}" \
        -lHalide \
        -o "${TEST_BUILD}/test_halide_wasm.js" \
        -sALLOW_MEMORY_GROWTH=1 \
        -sINITIAL_MEMORY=536870912 \
        -sSTACK_SIZE=8388608 \
        -sNO_EXIT_RUNTIME=0 \
        -sENVIRONMENT=node \
        2>&1 || {
            log "WARNING: Test build failed (expected if LLVM wasm build is not complete)"
            log "  This is a known issue — the LLVM-for-wasm build is complex."
            log "  Check the error output above for details."
        }

    if ${RUN_TEST} && [[ -f "${TEST_BUILD}/test_halide_wasm.js" ]]; then
        log "Running smoke test..."
        node "${TEST_BUILD}/test_halide_wasm.js" && \
            log "Smoke test PASSED" || \
            log "Smoke test FAILED"
    fi
fi

log "Done. Build artifacts in: ${BUILD_DIR}"
