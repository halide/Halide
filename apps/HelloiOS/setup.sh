#!/usr/bin/env bash

set -euo pipefail

: "${Halide_ROOT:?Set Halide_ROOT in the environment to the Halide install prefix.}"

cd "$(dirname "${BASH_SOURCE[0]}")"

cmake -G Xcode -S Generators -B Generators/build

echo ""
echo "Setup complete. Open HelloiOS.xcworkspace in Xcode to build and run."
echo ""
echo "  open $(pwd)/HelloiOS.xcworkspace"
echo ""
