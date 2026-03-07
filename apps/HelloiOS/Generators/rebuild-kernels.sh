#!/usr/bin/env bash
# Called by the Xcode "Build Halide Generators" run-script phase.
# GENERATORS_BUILD_DIR comes from GeneratorPaths.xcconfig, written by build.sh.

set -euo pipefail

if [ -z "${GENERATORS_BUILD_DIR:-}" ]; then
    echo "error: GENERATORS_BUILD_DIR is not set. Run build.sh first to configure."
    exit 1
fi

xcodebuild -project "$GENERATORS_BUILD_DIR/HelloiOS-Generators.xcodeproj" \
    -target halide_kernels -configuration Release