#!/usr/bin/env bash
# Called by the Xcode "Build Halide Generators" run-script phase.
# Variables (CMAKE_COMMAND, GENERATORS_BUILD_DIR) come from
# GeneratorPaths.xcconfig, written by build.sh.

set -euo pipefail

if [ -z "${GENERATORS_BUILD_DIR:-}" ]; then
    echo "error: GENERATORS_BUILD_DIR is not set. Run build.sh first to configure."
    exit 1
fi

"$CMAKE_COMMAND" --build "$GENERATORS_BUILD_DIR"