#!/bin/bash

# This script is an ugly workaround to the fact that there is no way to force
# Bazel to build a fully-static library (ie, packaging all static dependencies).
# This is a more-expanded version of the technique used in Makefile,
# with a little extra tweaking for Bazel.
#
# TODO: Replace this with cc_export() if/when the tracking issue
# (https://github.com/bazelbuild/bazel/issues/1920) is

set -eu

# $1 = scratch directory
# $2 = .a to use for linkage baseline (e.g. libHalide.a)
# $3 = list of .a's to decompose and intelligently strip
# $4 = final output archive
# $5 = CXX + CFlags to use
# $6 = linkopts to use
# (Note that all paths are assumed to be relative to curdir on entry)

# $2 can be a list in -c=opt mode; if there are multiple entries,
# try to choose ".pic.a", then ".a"
BEST_ARCHIVE=
for ARCHIVE in $2; do
    if [[ "${ARCHIVE}" =~ ^.*\.pic\.a$ ]]; then
        BEST_ARCHIVE=${ARCHIVE}
    fi
done
if [[ -z "${BEST_ARCHIVE}" ]]; then
    for ARCHIVE in $2; do
        if [[ "${ARCHIVE}" =~ ^.*\.a$ ]]; then
            BEST_ARCHIVE=${ARCHIVE}
        fi
    done
fi
if [[ -z "${BEST_ARCHIVE}" ]]; then
    exit
fi

TMP=$1/hfs_tmp
rm -rf $TMP
mkdir -p $TMP

# Remember WD since we have to change dir to use ar x
WD=`pwd`

# Extract the baseline .o files
cd $TMP
ar x $WD/$BEST_ARCHIVE
cd $WD

# Determine the relevant object files from llvm with a dummy
# compilation. Passing -t to the linker gets it to list which
# object files in which archives it uses to resolve
# symbols. We only care about the libLLVM ones.
LIST=`$5 -o /dev/null -shared $TMP/*.o -Wl,-t $3 -lc++ $6 | egrep libLLVM`

# Extract the necessary object files from the llvm archives.
# Make each name unique as we go along, in case there are dupes.
UNIQUE=0
cd $TMP
for LINE in $LIST; do
    # expect each line to be /path/to/some.a(some.o)
    ARCHIVE=`echo ${LINE} | sed -e 's/(\(.*\))\(.*\)/\1/'`
    OBJ=`echo ${LINE} | sed -e 's/(\(.*\))\(.*\)/\2/'`
    ar x $WD/$ARCHIVE $OBJ; mv $OBJ llvm_${UNIQUE}_${OBJ}
    UNIQUE=$((UNIQUE + 1))
done
cd $WD

# Archive together all the halide and llvm object files. Start by
# just copying over the previous 
cp $BEST_ARCHIVE $4
chmod +w $4

# (Use xargs because ar breaks on MinGW with all objects at the same time.)
echo $TMP/llvm_*.o* | xargs -n200 ar q $4
ranlib $4

rm -rf $TMP

