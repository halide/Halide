#!/usr/bin/env bash

set -e

CLEANUP_FILES=()
# shellcheck disable=SC2329
cleanup() { rm -rf "${CLEANUP_FILES[@]}"; }
trap cleanup EXIT

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

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
    ([ -x "$(command -v nproc)" ] && nproc) ||
        ([ -x "$(command -v sysctl)" ] && sysctl -n hw.physicalcpu)
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
    CLEANUP_FILES+=("${CLANG_TIDY_BUILD_DIR}")
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
    RCDIR="$(xcrun clang -print-resource-dir)"
    cat >"${CLANG_TIDY_BUILD_DIR}/toolchain.cmake" <<EOF
set(CMAKE_SYSROOT "${SDKROOT}")
set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES
    "${RCDIR}/include"
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
cmake -S "${ROOT_DIR}" -B "${CLANG_TIDY_BUILD_DIR}" -Wno-dev -DWITH_TESTS=OFF

[ -e "${CLANG_TIDY_BUILD_DIR}/compile_commands.json" ]

echo Building Halide...
cmake --build "${CLANG_TIDY_BUILD_DIR}" -j "${J}"

echo Merging runtime compilation database...
jq -s 'add' "${CLANG_TIDY_BUILD_DIR}/compile_commands.json" \
    <(sed 's/,$//' "${CLANG_TIDY_BUILD_DIR}"/src/runtime/*.json | jq -s '.') \
    >"${CLANG_TIDY_BUILD_DIR}/compile_commands_merged.json"
mv "${CLANG_TIDY_BUILD_DIR}/compile_commands_merged.json" "${CLANG_TIDY_BUILD_DIR}/compile_commands.json"

# Wrapper filters noisy "N warnings generated." from each clang-tidy invocation.
CLANG_TIDY_FILTER="${CLANG_TIDY_BUILD_DIR}/clang-tidy-filter.sh"
cat >"${CLANG_TIDY_FILTER}" <<WRAPPER
#!/usr/bin/env bash
"${CLANG_TIDY_LLVM_INSTALL_DIR}/bin/clang-tidy" "\$@" 2>&1 | grep -v '^[[:digit:]]\+ warnings\? generated\.\$'
exit "\${PIPESTATUS[0]}"
WRAPPER
chmod +x "${CLANG_TIDY_FILTER}"

echo Running clang-tidy...
export PYTHONUNBUFFERED=1
"${CLANG_TIDY_LLVM_INSTALL_DIR}/bin/run-clang-tidy" \
    ${FIX} \
    -j "${J}" \
    -quiet \
    -p "${CLANG_TIDY_BUILD_DIR}" \
    -clang-tidy-binary "${CLANG_TIDY_FILTER}" \
    -clang-apply-replacements-binary "${CLANG_TIDY_LLVM_INSTALL_DIR}/bin/clang-apply-replacements" \
    "$@"

CLANG_TIDY_EXIT_CODE=$?

if [ "$CLANG_TIDY_EXIT_CODE" -eq 0 ]; then
    echo "Success!"
else
    echo "clang-tidy failed with exit code $CLANG_TIDY_EXIT_CODE"
fi

exit "$CLANG_TIDY_EXIT_CODE"
