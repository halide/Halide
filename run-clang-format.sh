#!/bin/bash

set -eu

ROOT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

if [ -z "$CLANG_FORMAT" ]; then
    echo "CLANG_FORMAT not yet, assuming default tool"
    CLANG_FORMAT = clang-format
fi

echo Using CLANG_FORMAT=${CLANG_FORMAT}...

find "${ROOT_DIR}/apps" \
     "${ROOT_DIR}/src" \
     "${ROOT_DIR}/tools" \
     "${ROOT_DIR}/test" \
     "${ROOT_DIR}/util" \
     "${ROOT_DIR}/python_bindings" \
     -name *.cpp -o -name *.h -o -name *.c | xargs ${CLANG_FORMAT} -i -style=file
