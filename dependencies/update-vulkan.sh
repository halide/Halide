#!/bin/bash

set -eo pipefail

cd -- "$(dirname -- "$0")" || exit 1

GIT_BRANCH="$1"
if [ -z "$GIT_BRANCH" ]; then
  echo "error: usage: $0 <git-branch>"
  echo "remark: the current <git-branch> is vulkan-sdk-1.3.296"
  exit 1
fi

mkdir -p vulkan

cleanup () {
  rm -rf Vulkan-Headers
}

trap cleanup SIGINT SIGTERM EXIT

git clone https://github.com/KhronosGroup/Vulkan-Headers.git --branch "$GIT_BRANCH"
cmake -S Vulkan-Headers -B Vulkan-Headers/build -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$PWD/Vulkan-Headers/_local"
cmake --build Vulkan-Headers/build --target install

# Copy license information
cp Vulkan-Headers/LICENSE.md vulkan/

# Copy headers we care about (only the ANSI-C core interfaces!)
mkdir -p vulkan/include/vulkan
cp Vulkan-Headers/_local/include/vulkan/*.h vulkan/include/vulkan/
mkdir -p vulkan/include/vk_video
cp Vulkan-Headers/_local/include/vk_video/*.h vulkan/include/vk_video/

# Copy CMake config
mkdir -p vulkan/share/
cp -R Vulkan-Headers/_local/share/cmake vulkan/share/

git add -f vulkan/

echo "Updated Vulkan-Headers to branch $GIT_BRANCH!"
