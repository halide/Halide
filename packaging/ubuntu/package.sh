#!/bin/bash
set -e -o pipefail

halide_source=$(realpath "$1")
halide_build_root=$(realpath "$2")

[ -z "$halide_source" ] && echo "Usage: $0 <source-dir> <build-dir>" && exit
[ -z "$halide_build_root" ] && echo "Usage: $0 <source-dir> <build-dir>" && exit
[ -z "$LLVM_ROOT" ] && echo "Must set LLVM_ROOT to /usr/lib/llvm-VERSION" && exit

function group() {
  [[ -n "${GITHUB_ACTIONS}" && -n "${SEEN_GROUP}" ]] && echo "::endgroup::"
  [[ -n "${GITHUB_ACTIONS}" ]] && echo "::group::$*"
  export SEEN_GROUP=1
}

group "Configure shared Halide build"
cmake --preset=package-ubuntu-shared -S "$halide_source" -B "$halide_build_root/shared-Release"

group "Configure static Halide build"
cmake --preset=package-ubuntu-static -S "$halide_source" -B "$halide_build_root/static-Release"

group "Build shared Halide"
cmake --build "$halide_build_root/shared-Release" -- -v

group "Build static Halide"
cmake --build "$halide_build_root/static-Release" -- -v

group "Create Ubuntu packages"
cd "$halide_build_root"
rm -rf ./_CPack_Packages ./*.deb lintian.log
umask 0022
export LD_LIBRARY_PATH="$halide_build_root/shared-Release/src"

cpack -G DEB -C Release --config "$halide_source/packaging/ubuntu/config.cmake"

# Lintian: https://lintian.debian.org/tags

group "Run strict Lintian checks"
lintian --no-tag-display-limit -i ./*.deb

group "Run extra Lintian checks"
lintian --no-tag-display-limit -L "=info" -i ./*.deb

echo "Success!"
