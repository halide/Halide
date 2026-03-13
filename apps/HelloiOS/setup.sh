#!/usr/bin/env bash

set -euo pipefail

: "${Halide_ROOT:?Set Halide_ROOT in the environment to the Halide install prefix.}"

cd "$(dirname "${BASH_SOURCE[0]}")"

cmake -G Xcode -S Generators -B Generators/build

# Bootstrap: build kernels and package the xcframework so Xcode can load
# the project. Subsequent builds are driven by the Xcode run-script phase.
cmake --build Generators/build --config Release

echo ""
echo "Setup complete. Open HelloiOS.xcworkspace in Xcode to build and run."
echo ""
echo "  open $(pwd)/HelloiOS.xcworkspace"
echo ""
echo "To run on a real device, you will need to set the development team "
echo "in the Xcode project settings. Check 'Automatically manage signing'"
echo "under the Signing & Capabilities tab of the HelloiOS project settings."
echo ""
echo "If you encounter build problems, check the troubleshooting section of"
echo "the README. If that section doesn't help and you solve your build"
echo "problems regardless, we would welcome a PR that updates the"
echo "troubleshooting section of the README."
