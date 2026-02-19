#!/bin/bash

set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" > /dev/null 2>&1 && pwd)"

##

# We standardize a common LLVM/Clang version for this script.
# Note that this is totally independent of the version of LLVM that you
# are using to build Halide itself. If you don't have the right version
# installed, you can usually install what you need easily via:
#
#   sudo apt-get install llvm-X clang-X libclang-X-dev clang-tidy-X
#   export CLANG_TIDY_LLVM_INSTALL_DIR=/usr/lib/llvm-X
#
# On macOS:
#
#   brew install llvm@X
#   export CLANG_TIDY_LLVM_INSTALL_DIR=/opt/homebrew/opt/llvm@X
#
# Where X matches the EXPECTED_VERSION below.

EXPECTED_VERSION=21

##

usage() {
    echo "Usage: $0 [-j MAX_PROCESS_COUNT] [-f]" 1>&2
    exit 1
}

get_thread_count() {
    ([ -x "$(command -v nproc)" ] && nproc) \
        || ([ -x "$(command -v sysctl)" ] && sysctl -n hw.physicalcpu)
}

if [ "$(uname)" == "Darwin" ]; then
    _DEFAULT_LLVM_LOCATION="/opt/homebrew/opt/llvm@$EXPECTED_VERSION"
else
    _DEFAULT_LLVM_LOCATION="/usr/lib/llvm-$EXPECTED_VERSION"
fi

J=$(get_thread_count)
FIX=

while getopts ":j:f" o; do
    case "${o}" in
    j)
        J="${OPTARG}"
        [[ ${J} =~ ^[0-9]+$ ]] || (
            echo "-j requires an integer argument"
            usage
        )
        ;;
    f)
        FIX="-fix"
        ;;
    *)
        usage
        ;;
    esac
done
shift $((OPTIND - 1))

echo "Using ${J} processes."
if [ -n "${FIX}" ]; then
    echo "Operating in -fix mode!"
fi

if [ -z "$CLANG_TIDY_LLVM_INSTALL_DIR" ]; then
    if [ -d "${_DEFAULT_LLVM_LOCATION}" ]; then
        CLANG_TIDY_LLVM_INSTALL_DIR="${_DEFAULT_LLVM_LOCATION}"
    else
        echo "CLANG_TIDY_LLVM_INSTALL_DIR must point to an LLVM installation dir for this script."
        exit
    fi
fi

echo "CLANG_TIDY_LLVM_INSTALL_DIR = ${CLANG_TIDY_LLVM_INSTALL_DIR}"

VERSION=$("${CLANG_TIDY_LLVM_INSTALL_DIR}/bin/clang-tidy" --version)
if [[ ${VERSION} =~ .*version\ $EXPECTED_VERSION.* ]]; then
    echo "clang-tidy version $EXPECTED_VERSION found."
else
    echo "CLANG_TIDY_LLVM_INSTALL_DIR must point to an LLVM $EXPECTED_VERSION install!"
    exit 1
fi

# Use a temp folder for the CMake stuff here, so it's fresh & correct every time
if [[ -z ${CLANG_TIDY_BUILD_DIR} ]]; then
    CLANG_TIDY_BUILD_DIR=$(mktemp -d)
    trap 'rm -rf ${CLANG_TIDY_BUILD_DIR}' EXIT
else
    mkdir -p "${CLANG_TIDY_BUILD_DIR}"
fi

echo "CLANG_TIDY_BUILD_DIR = ${CLANG_TIDY_BUILD_DIR}"

export CC="${CLANG_TIDY_LLVM_INSTALL_DIR}/bin/clang"
export CXX="${CLANG_TIDY_LLVM_INSTALL_DIR}/bin/clang++"

export CMAKE_GENERATOR=Ninja
export CMAKE_BUILD_TYPE=Debug
export CMAKE_EXPORT_COMPILE_COMMANDS=ON
export Halide_LLVM_ROOT="${CLANG_TIDY_LLVM_INSTALL_DIR}"

if [[ $(${CC} --version) =~ .*Homebrew.* ]]; then
    # Homebrew clang is badly misconfigured and needs help finding the
    # system headers, even though it uses system libc++ by default.
    ARCH="$(uname -m)"
    if [[ ${ARCH} == "arm64" ]]; then
        HOMEBREW_TRIPLET="arm64-osx-homebrew"
    else
        HOMEBREW_TRIPLET="x64-osx-homebrew"
    fi

    if [[ -d ${VCPKG_ROOT:-} ]]; then
        # vcpkg is active: delegate to the custom triple, which chains to the
        # toolchain file that fixes the include paths.
        export CMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        export VCPKG_DEFAULT_TRIPLET="${HOMEBREW_TRIPLET}"
        export VCPKG_OVERLAY_TRIPLETS="${ROOT_DIR}/cmake/triplets"
        export VCPKG_OVERLAY_PORTS="${ROOT_DIR}/cmake/vcpkg"
        export VCPKG_MANIFEST_FEATURES=-developer
    else
        # vcpkg is not active: use the toolchain file directly.
        export CMAKE_TOOLCHAIN_FILE="${ROOT_DIR}/cmake/toolchain.macos-homebrew-clang.cmake"
    fi
fi

echo Configuring Halide...
cmake -S "${ROOT_DIR}" -B "${CLANG_TIDY_BUILD_DIR}" -Wno-dev -DWITH_TESTS=OFF

[ -e "${CLANG_TIDY_BUILD_DIR}/compile_commands.json" ]

echo Building Halide...
cmake --build "${CLANG_TIDY_BUILD_DIR}" -j "${J}"

echo Building runtime compilation database...
temp_file=$(mktemp)
trap 'rm -f $temp_file' EXIT
rm -f "${CLANG_TIDY_BUILD_DIR}/src/runtime/compile_commands.json"
cat "${CLANG_TIDY_BUILD_DIR}"/src/runtime/*.json > "$temp_file"
{
    echo '['
    cat "$temp_file" | sed '$ s/,$//'
    echo ']'
} > "${CLANG_TIDY_BUILD_DIR}/src/runtime/compile_commands.json"

echo Merging compilation databases...
jq -s 'add' "${CLANG_TIDY_BUILD_DIR}/compile_commands.json" \
    "${CLANG_TIDY_BUILD_DIR}/src/runtime/compile_commands.json" \
    > "${CLANG_TIDY_BUILD_DIR}/compile_commands_merged.json"
mv "${CLANG_TIDY_BUILD_DIR}/compile_commands_merged.json" "${CLANG_TIDY_BUILD_DIR}/compile_commands.json"

echo Running clang-tidy...
PYTHONUNBUFFERED=1 "${CLANG_TIDY_LLVM_INSTALL_DIR}/bin/run-clang-tidy" \
    ${FIX} \
    -j "${J}" \
    -quiet \
    -p "${CLANG_TIDY_BUILD_DIR}" \
    -clang-tidy-binary "${CLANG_TIDY_LLVM_INSTALL_DIR}/bin/clang-tidy" \
    -clang-apply-replacements-binary "${CLANG_TIDY_LLVM_INSTALL_DIR}/bin/clang-apply-replacements" \
    "$@" 2>&1 | sed -Eu '/^[[:digit:]]+ warnings? generated\.$/{N;/\n$/d;}'

CLANG_TIDY_EXIT_CODE=${PIPESTATUS[0]}

if [ "$CLANG_TIDY_EXIT_CODE" -eq 0 ]; then
    echo "Success!"
else
    echo "clang-tidy failed with exit code $CLANG_TIDY_EXIT_CODE"
fi

exit "$CLANG_TIDY_EXIT_CODE"
