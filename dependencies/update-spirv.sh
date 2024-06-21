#!/bin/bash

set -eo pipefail

cd -- "$(dirname -- "$0")" || exit 1

GIT_BRANCH="$1"
if [ -z "$GIT_BRANCH" ]; then
  echo "error: usage: $0 <git-branch>"
  echo "remark: the current <git-branch> is sdk-1.3.231"
  exit 1
fi

mkdir -p spirv

cleanup () {
  rm -rf SPIRV-Headers
}

trap cleanup SIGINT SIGTERM EXIT

git clone https://github.com/KhronosGroup/SPIRV-Headers.git --branch "$GIT_BRANCH"
cmake -S SPIRV-Headers -B SPIRV-Headers/build -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$PWD/SPIRV-Headers/_local"
cmake --build SPIRV-Headers/build --target install

# Copy license information
cp SPIRV-Headers/LICENSE spirv/

# Copy headers we care about
mkdir -p spirv/include/spirv/unified1/
cp SPIRV-Headers/_local/include/spirv/unified1/GLSL.std.450.h spirv/include/spirv/unified1/
cp SPIRV-Headers/_local/include/spirv/unified1/spirv.h spirv/include/spirv/unified1/

# Copy CMake config
mkdir -p spirv/share/
cp -R SPIRV-Headers/_local/share/cmake spirv/share/

git add -f spirv/

echo "Updated SPIRV-Headers to branch $GIT_BRANCH!"
