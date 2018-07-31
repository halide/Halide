#!/bin/bash


##############################################################
#### Optional Configuration###################################
##############################################################

# Set LLVM prefix. The prefix is where LLVM is installed and
# it should contain bin/clang and bin/llvm-config. Example:
# LLVM_PREFIX=/data/scratch/baghdadi/llvm/llvm_60_prefix/
LLVM_PREFIX=

##############################################################
#### Optional Configuration###################################
##############################################################

# Set clang and llvm-config paths
# Examples
# export CLANG=/data/scratch/baghdadi/llvm/llvm_50_prefix/bin/clang
# export LLVM_CONFIG=/data/scratch/baghdadi/llvm/llvm_50_prefix/bin/llvm-config
export CLANG=${LLVM_PREFIX}/bin/clang
export LLVM_CONFIG=${LLVM_PREFIX}/bin/llvm-config


# Install TVM, TC, PENCIL
INSTALL_TVM=1
INSTALL_TC=1
INSTALL_PENCIL=1

# Number of cores passed for make -j
# Example
# CORES=48
CORES=48

# Set path to the cmake binary if it not instaled system-wide
# CMAKE=/data/scratch/baghdadi/cmake-3.9.0_prefix/bin/cmake
CMAKE=cmake

##############################################################
##############################################################
##############################################################

PROJECT_ROOT=$PWD

set -e
echo_and_run_cmd () { # takes one argument, the command
    echo $1
    # Run the command
    $@
}

if [ ! -d "software" ]; then
	echo_and_run_cmd "mkdir software"
fi

echo_and_run_cmd "cd ${PROJECT_ROOT}/software"

if [ "${INSTALL_TVM}" -eq "1" ]; then
	# Getting TVM
	echo "### Installing TVM ###"
	if [ ! -d "tvm" ]; then
		echo_and_run_cmd "git clone --recursive https://github.com/dmlc/tvm"
		echo_and_run_cmd "cd tvm"
	else
		echo_and_run_cmd "cd tvm"
		echo_and_run_cmd "git pull"
	fi
	echo_and_run_cmd "cp ${PROJECT_ROOT}/scripts/tvm_config.cmake ${PROJECT_ROOT}/software/tvm/config.cmake"
	if [ ! -d "build" ]; then
		echo_and_run_cmd "mkdir build"
	fi
	echo_and_run_cmd "cd build"
	#echo_and_run_cmd "make clean"
	echo_and_run_cmd "$CMAKE .. -DLLVMCONFIG=${LLVM_CONFIG}"
	echo_and_run_cmd "make -j $CORES"
	echo_and_run_cmd "cd ${PROJECT_ROOT}/software"
fi


# Getting TC
if [ "${INSTALL_TC}" -eq "1" ]; then
	# The following script follows the steps described in https://facebookresearch.github.io/TensorComprehensions/installation.html
	INSTALLATION_PATH="${PROJECT_ROOT}/software/"
	wget https://repo.anaconda.com/archive/Anaconda3-5.1.0-Linux-x86_64.sh -O anaconda.sh && \
	chmod +x anaconda.sh && \
	./anaconda.sh -u -b -p ${INSTALLATION_PATH} && \
	rm anaconda.sh

	. ${INSTALLATION_PATH}/bin/activate
	conda update -y -n base conda

	echo_and_run_cmd "cd ${PROJECT_ROOT}/software"
	conda install -y -c pytorch -c tensorcomp tensor_comprehensions
	echo_and_run_cmd "cd ${PROJECT_ROOT}/software"
fi


# Getting PENCIL
if [ "${INSTALL_PENCIL}" -eq "1" ]; then
	BRANCH="master"

	# PPCG directory
	DIRECTORY="ppcg_${BRANCH}"

	git clone git://repo.or.cz/ppcg.git $DIRECTORY
	cd $DIRECTORY

	./get_submodules.sh
	./autogen.sh
	./configure --with-clang-prefix=${LLVM_PREFIX}
	make -j $CORES
	echo_and_run_cmd "cd ${PROJECT_ROOT}/software"
fi
