#!/bin/bash

set -e

ROOT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

usage() { echo "Usage: $0 [-j MAX_PROCESS_COUNT] [-f]" 1>&2; exit 1; }

get_thread_count () {
  ([ -x "$(command -v nproc)" ] && nproc) ||
  ([ -x "$(command -v sysctl)" ] && sysctl -n hw.physicalcpu)
}

if [ "$(uname)" == "Darwin" ]; then
  patch_file () { sed -i '' -E "$@"; }
else
  patch_file () { sed -i -E "$@"; }
fi

J=$(get_thread_count)
FIX=

while getopts ":j:f" o; do
    case "${o}" in
        j)
            J="${OPTARG}"
            [[ "${J}" =~ ^[0-9]+$ ]] || ( echo "-j requires an integer argument"; usage )
            ;;
        f)
            FIX="-fix"
            ;;
        *)
            usage
            ;;
    esac
done
shift $((OPTIND-1))

echo "Using ${J} processes."
if [ -n "${FIX}" ]; then
    echo "Operating in -fix mode!"
fi

# We are currently standardized on using LLVM/Clang 21 for this script.
# Note that this is totally independent of the version of LLVM that you
# are using to build Halide itself. If you don't have LLVM21 installed,
# you can usually install what you need easily via:
#
# sudo apt-get install llvm-21 clang-21 libclang-21-dev clang-tidy-21
# export CLANG_TIDY_LLVM_INSTALL_DIR=/usr/lib/llvm-21
#
# On macOS:
#
# brew install llvm@21
# export CLANG_TIDY_LLVM_INSTALL_DIR=/opt/homebrew/opt/llvm@21

if [ -z "$CLANG_TIDY_LLVM_INSTALL_DIR" ]; then
  echo "CLANG_TIDY_LLVM_INSTALL_DIR must point to an LLVM installation dir for this script."
  exit
fi

echo "CLANG_TIDY_LLVM_INSTALL_DIR = ${CLANG_TIDY_LLVM_INSTALL_DIR}"

VERSION=$("${CLANG_TIDY_LLVM_INSTALL_DIR}/bin/clang-tidy" --version)
if [[ ${VERSION} =~ .*version\ 21.* ]]
then
    echo "clang-tidy version 21 found."
else
    echo "CLANG_TIDY_LLVM_INSTALL_DIR must point to an LLVM 21 install!"
    exit 1
fi

# Use a temp folder for the CMake stuff here, so it's fresh & correct every time
if [[ -z "${CLANG_TIDY_BUILD_DIR}" ]]; then
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
  # Homebrew clang 21 is badly misconfigured and needs help finding the
  # system headers, even though it uses system libc++ by default.
  SDKROOT="$(xcrun --show-sdk-path)"
  # TOOLCHAINROOT="$(xcrun --show-toolchain-path)"
  TOOLCHAINROOT="$(cd "$(dirname "$(xcrun --find clang)")"/../.. && pwd)"
  cat > "${CLANG_TIDY_BUILD_DIR}/toolchain.cmake" << EOF
set(CMAKE_SYSROOT "${SDKROOT}")
set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES
    "${TOOLCHAINROOT}/usr/lib/clang/17/include"
    "${SDKROOT}/usr/include"
    "${TOOLCHAINROOT}/usr/include"
    "${SDKROOT}/System/Library/Frameworks"
    "${SDKROOT}/System/Library/SubFrameworks"
)
set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES
    "${SDKROOT}/usr/include/c++/v1"
    \${CMAKE_C_STANDARD_INCLUDE_DIRECTORIES}
)
EOF
  export CMAKE_TOOLCHAIN_FILE="${CLANG_TIDY_BUILD_DIR}/toolchain.cmake"
fi

echo Configuring Halide...
cmake -S "${ROOT_DIR}" -B "${CLANG_TIDY_BUILD_DIR}" -Wno-dev -DWITH_TESTS=OFF -DWITH_TUTORIALS=OFF

[ -a "${CLANG_TIDY_BUILD_DIR}/compile_commands.json" ]

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
