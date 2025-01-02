#!/bin/bash

set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"

##

EXPECTED_VERSION=19

##

usage() { echo -e "Usage: $0 [-c]" 1>&2; exit 1; }

# Fix the formatting in-place
MODE_FLAGS=(-i --sort-includes)

while getopts "c" o; do
    case "${o}" in
    c)
        # Only check the files and print formatting errors
        MODE_FLAGS=(--dry-run -Werror)
        ;;
    *)
        usage
        ;;
    esac
done
shift $((OPTIND - 1))

if [[ "${MODE_FLAGS[*]}" =~ "-i" ]]; then
    if ! git diff-files --quiet --ignore-submodules; then
        echo -e "\033[0;31m" # RED
        echo "WARNING: There are still uncommited changes in your working tree."
        echo "         Reverting this formatting action will be difficult."
        echo -e "\033[0m" # RESET
        git diff-files --ignore-submodules
        echo
        read -p "Do you wish to continue (Y/N)? " -r
        if ! [[ $REPLY =~ ^[Yy]$ ]]; then
            exit
        fi
    fi
fi

# We are currently standardized on using LLVM/Clang19 for this script.
# Note that this is totally independent of the version of LLVM that you
# are using to build Halide itself. If you don't have LLVM19 installed,
# you can usually install what you need easily via:
#
# sudo apt-get install llvm-19 clang-19 libclang-19-dev clang-tidy-19
# export CLANG_FORMAT_LLVM_INSTALL_DIR=/usr/lib/llvm-19
#
# On macOS:
#
# brew install llvm@19
# export CLANG_FORMAT_LLVM_INSTALL_DIR=/opt/homebrew/opt/llvm@19

if [ -z "$CLANG_FORMAT_LLVM_INSTALL_DIR" ]; then
    echo "CLANG_FORMAT_LLVM_INSTALL_DIR must point to an LLVM installation dir for this script."
    exit 1
fi

echo "CLANG_FORMAT_LLVM_INSTALL_DIR=${CLANG_FORMAT_LLVM_INSTALL_DIR}"

CLANG_FORMAT="${CLANG_FORMAT_LLVM_INSTALL_DIR}/bin/clang-format"

VERSION=$("${CLANG_FORMAT}" --version)
if [[ ${VERSION} =~ .*version\ $EXPECTED_VERSION.* ]]; then
    echo "clang-format version $EXPECTED_VERSION found."
else
    echo "CLANG_FORMAT_LLVM_INSTALL_DIR must point to an LLVM $EXPECTED_VERSION install!"
    exit 1
fi

# Note that we specifically exclude files starting with . in order
# to avoid finding emacs backup files
find "${ROOT_DIR}/apps" \
     "${ROOT_DIR}/src" \
     "${ROOT_DIR}/tools" \
     "${ROOT_DIR}/test" \
     "${ROOT_DIR}/util" \
     "${ROOT_DIR}/python_bindings" \
     -not -path "${ROOT_DIR}/src/runtime/hexagon_remote/bin/src/*" \
     \( -name "*.cpp" -o -name "*.h" -o -name "*.c" \) -and -not -wholename "*/.*" \
     -print0 | xargs -0 "${CLANG_FORMAT}" "${MODE_FLAGS[@]}" -style=file

exit "${PIPESTATUS[1]}"
