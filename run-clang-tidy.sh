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

# We are currently standardized on using LLVM/Clang 17 for this script.
# Note that this is totally independent of the version of LLVM that you
# are using to build Halide itself. If you don't have LLVM17 installed,
# you can usually install what you need easily via:
#
# sudo apt-get install llvm-17 clang-17 libclang-17-dev clang-tidy-17
# export CLANG_TIDY_LLVM_INSTALL_DIR=/usr/lib/llvm-17
#
# On macOS:
#
# brew install llvm@17
# export CLANG_TIDY_LLVM_INSTALL_DIR=/opt/homebrew/opt/llvm@17

if [ -z "$CLANG_TIDY_LLVM_INSTALL_DIR" ]; then
  echo "CLANG_TIDY_LLVM_INSTALL_DIR must point to an LLVM installation dir for this script."
  exit
fi

echo "CLANG_TIDY_LLVM_INSTALL_DIR = ${CLANG_TIDY_LLVM_INSTALL_DIR}"

VERSION=$("${CLANG_TIDY_LLVM_INSTALL_DIR}/bin/clang-tidy" --version)
if [[ ${VERSION} =~ .*version\ 17.* ]]
then
    echo "clang-tidy version 17 found."
else
    echo "CLANG_TIDY_LLVM_INSTALL_DIR must point to an LLVM 17 install!"
    exit 1
fi


# Use a temp folder for the CMake stuff here, so it's fresh & correct every time
CLANG_TIDY_BUILD_DIR=$(mktemp -d)
echo "CLANG_TIDY_BUILD_DIR = ${CLANG_TIDY_BUILD_DIR}"

# Specify Halide_LLVM_SHARED_LIBS=ON because some installers may provide only that.
echo Building compile_commands.json...
cmake -G Ninja -S "${ROOT_DIR}" -B "${CLANG_TIDY_BUILD_DIR}" -Wno-dev \
      -DCMAKE_C_COMPILER="${CLANG_TIDY_LLVM_INSTALL_DIR}/bin/clang" \
      -DCMAKE_CXX_COMPILER="${CLANG_TIDY_LLVM_INSTALL_DIR}/bin/clang++" \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      -DHalide_CLANG_TIDY_BUILD=ON \
      -DHalide_LLVM_ROOT="${CLANG_TIDY_LLVM_INSTALL_DIR}" \
      > /dev/null

[ -a "${CLANG_TIDY_BUILD_DIR}/compile_commands.json" ]

# We need to remove -arch flags where -target flags also exist. These break our fake runtime compilation steps on macOS
echo Patching compile_commands.json...
patch_file '/-target/ s/-arch *[^ ]+//' "${CLANG_TIDY_BUILD_DIR}/compile_commands.json"

# We must populate the includes directory to check things outside of src/
echo Building HalideIncludes...
cmake --build "${CLANG_TIDY_BUILD_DIR}" -j "${J}" --target HalideIncludes

echo Building flatbuffer stuff...
cmake --build "${CLANG_TIDY_BUILD_DIR}" -j "${J}" --target generate_fb_header

RUN_CLANG_TIDY=${CLANG_TIDY_LLVM_INSTALL_DIR}/bin/run-clang-tidy

# We deliberately skip apps/ and test/ for now, as the compile commands won't include
# generated headers files from Generators.
#
# Skip DefaultCostModel.cpp as it relies on cost_model.h.
# Skip GenGen.cpp and RunGenMain.cpp as they bring clang-tidy to its knees,
# for reasons that aren't entirely clear yet.
echo Finding targets...
CLANG_TIDY_TARGETS=$(find \
     "${ROOT_DIR}/src" \
     "${ROOT_DIR}/python_bindings" \
     "${ROOT_DIR}/tools" \
     "${ROOT_DIR}/util" \
     \( -name "*.cpp" -o -name "*.h" -o -name "*.c" \) -and -not -wholename "*/.*" \
     ! -name DefaultCostModel.cpp \
     ! -name GenGen.cpp \
     ! -name RunGenMain.cpp)

# clang-tidy doesn't have a sane way to exclude third-party headers (e.g. pybind11),
# so we will instead build an include filter
CLANG_TIDY_HEADER_FILTER=".*/src/.*|.*/python_bindings/.*|.*/tools/.*|.*/util/.*"

echo Running clang-tidy...
${RUN_CLANG_TIDY} \
    ${FIX} \
    -j "${J}" \
    -header-filter="${CLANG_TIDY_HEADER_FILTER}" \
    -quiet \
    -p "${CLANG_TIDY_BUILD_DIR}" \
    -clang-tidy-binary "${CLANG_TIDY_LLVM_INSTALL_DIR}/bin/clang-tidy" \
    -clang-apply-replacements-binary "${CLANG_TIDY_LLVM_INSTALL_DIR}/bin/clang-apply-replacements" \
    ${CLANG_TIDY_TARGETS} \
    2>&1 | grep -v "warnings generated" | sed "s|.*/||"

RESULT=${PIPESTATUS[0]}

echo "run-clang-tidy finished with status ${RESULT}"

rm -rf "${CLANG_TIDY_BUILD_DIR}"

exit "${RESULT}"