#!/bin/bash

set -e

ROOT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

[[ "$1" != "" && "$1" != "-fix" ]] && echo "The only supported argument is -fix" && exit

FIX=$1

if [ -z ${IWYU_LLVM_INSTALL_DIR} ]; then
IWYU_LLVM_INSTALL_DIR=${HOME}/llvm-14-install
fi
echo Using IWYU_LLVM_INSTALL_DIR=${IWYU_LLVM_INSTALL_DIR}

# Find the tools we need
if [ -z ${IWYU} ]; then
# Linux usually installs as `iwyu` as well but Homebrew only includes the full name
IWYU=$(which include-what-you-use)
fi
[ -z "$IWYU" ] && echo "install-what-you-use is not installed." && exit
echo Using IWYU=${IWYU}

if [ -z ${IWYU_J} ]; then
# Linux usually installs as `iwyu` as well but Homebrew only includes the full name
IWYU_J=32
fi
echo Using IWYU_J=${IWYU_J}

if [ -z ${IWYU_TOOL} ]; then
IWYU_TOOL=$(which iwyu_tool.py)
fi
[ -z "$IWYU_TOOL" ] && echo "iwyu_tool.py is not installed." && exit
echo Using IWYU_TOOL=${IWYU_TOOL}

if [ -z ${IWYU_FIX_INCLUDES} ]; then
IWYU_FIX_INCLUDES=$(which fix_includes.py)
fi
[ -z "$IWYU_FIX_INCLUDES" ] && echo "fix_includes.py is not installed." && exit
echo Using IWYU_FIX_INCLUDES=${IWYU_FIX_INCLUDES}

if [ -z ${IWYU_CLANG} ]; then
IWYU_CLANG=$(which clang)
fi
[ -z "$IWYU_CLANG" ] && echo "clang is not installed." && exit
echo Using IWYU_CLANG=${IWYU_CLANG}

if [ -z ${IWYU_CLANGPP} ]; then
IWYU_CLANGPP=$(which clang++)
fi
[ -z "$IWYU_CLANGPP" ] && echo "clang++ is not installed." && exit
echo Using IWYU_CLANGPP=${IWYU_CLANGPP}

# Check the versions
# IWYU_VERSION=$(${IWYU} --version)
# if [[ ! ${IWYU_VERSION} =~ .*include-what-you-use\ 0.16.* ]]
# then
#     echo "IWYU must point to an IWYU 0.16 install!"
#     exit 1
# fi

# IWYU_CLANG_VERSION=$(${IWYU_CLANG} --version)
# if [[ ! ${IWYU_CLANG_VERSION} =~ .*version\ 12.* ]]
# then
#     echo "IWYU_CLANG must point to a Clang 12 install!"
#     exit 1
# fi

# IWYU_CLANGPP_VERSION=$(${IWYU_CLANGPP} --version)
# if [[ ! ${IWYU_CLANGPP_VERSION} =~ .*version\ 12.* ]]
# then
#     echo "IWYU_CLANGPP must point to a Clang 12 install!"
#     exit 1
# fi

# Use a temp folder for the CMake stuff here, so it's fresh & correct every time
IWYU_BUILD_DIR=`mktemp -d`
echo Using IWYU_BUILD_DIR = ${IWYU_BUILD_DIR}

# Specify Halide_SHARED_LLVM=ON because some installers may provide only that.
# Must specify Halide_CLANG_TIDY_BUILD=ON to ensure all targets are processed, even though we aren't using clang-tidy here.
echo Building compile_commands.json...
CC="${IWYU_CLANG}" \
CXX="${IWYU_CLANGPP}" \
cmake -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      -DHalide_CLANG_TIDY_BUILD=ON \
      -DHalide_SHARED_LLVM=ON \
      -DLLVM_DIR=${IWYU_LLVM_INSTALL_DIR}/lib/cmake/llvm \
      -S ${ROOT_DIR} \
      -B ${IWYU_BUILD_DIR} \
      > /dev/null

[ -a ${IWYU_BUILD_DIR}/compile_commands.json ]

# We must populate the includes directory to check things outside of src/
cmake --build ${IWYU_BUILD_DIR} --target HalideIncludes

# We deliberately skip apps/ and test/ for now, as the compile commands won't include
# generated headers files from Generators.
#
# Skip everything in hexagon_remote because reasons.
# Skip find_inverse.cpp as we don't usually build it.
# Skip DefaultCostModel.cpp as it relies on cost_model.h.
IWYU_TARGETS=$(find \
     "${ROOT_DIR}/src" \
     "${ROOT_DIR}/python_bindings" \
     "${ROOT_DIR}/tools" \
     "${ROOT_DIR}/util" \
     \( -name *.cpp -o -name *.c \) -and -not -wholename "*/.*" \
     ! -path "${ROOT_DIR}/src/runtime/hexagon_remote/*" \
     ! -name find_inverse.cpp \
     ! -name DefaultCostModel.cpp)

IWYU_LOG="${IWYU_BUILD_DIR}/iwyu.log"
echo Running iwyu_tool.py...

# iwyu_tool.py returns nonzero if anything is found to fix, so disable exiting on error for this command
set +e
${IWYU_TOOL} ${IWYU_TARGETS} -j ${IWYU_J} -p ${IWYU_BUILD_DIR}/compile_commands.json > ${IWYU_LOG}
IWYU_TOOL_RESULT=${PIPESTATUS[0]}
set -e

echo iwyu_tool.py finished with status ${IWYU_TOOL_RESULT}

if [ ! -z ${FIX} ]
then
    echo "YES (${FIX})"
    echo Running fix_includes.py...
    echo ${IWYU_FIX_INCLUDES} \
        --nocomments \
        --basedir=${ROOT_DIR} \
        < ${IWYU_LOG}

    ${IWYU_FIX_INCLUDES} \
        --nocomments \
        --basedir=${ROOT_DIR} \
        < ${IWYU_LOG}
fi


echo Done! (You probably want to run clang-format now.)

# rm -rf ${IWYU_BUILD_DIR}

