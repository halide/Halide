#!/bin/bash

set -e

ROOT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

# We are currently standardized on using LLVM/Clang17 for this script.
# Note that this is totally independent of the version of LLVM that you
# are using to build Halide itself. If you don't have LLVM17 installed,
# you can usually install what you need easily via:
#
# sudo apt-get install llvm-17 clang-17 libclang-17-dev clang-tidy-17
# export CLANG_FORMAT_LLVM_INSTALL_DIR=/usr/lib/llvm-17
#
# On macOS:
#
# brew install llvm@17
# export CLANG_FORMAT_LLVM_INSTALL_DIR=/opt/homebrew/opt/llvm@17

if [ -z "$CLANG_FORMAT_LLVM_INSTALL_DIR" ]; then
  echo "CLANG_FORMAT_LLVM_INSTALL_DIR must point to an LLVM installation dir for this script."
  exit 1
fi

echo "CLANG_FORMAT_LLVM_INSTALL_DIR = ${CLANG_FORMAT_LLVM_INSTALL_DIR}"

CLANG_FORMAT="${CLANG_FORMAT_LLVM_INSTALL_DIR}/bin/clang-format"

VERSION=$("${CLANG_FORMAT}" --version)
if [[ ${VERSION} =~ .*version\ 17.* ]]; then
    echo "clang-format version 17 found."
else
    echo "CLANG_FORMAT_LLVM_INSTALL_DIR must point to an LLVM 17 install!"
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
     -print0 | \
     xargs -0 "${CLANG_FORMAT}" -i -style=file