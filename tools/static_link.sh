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
# $3 = final output archive
# $4 = CXX + CFlags to use
# $5 = linkopts to use
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

# fake-link line must include -lc++ on Darwin, but not Linux
LINKOPTS=
if [[ $(uname) == "Darwin" ]]; then
    LINKOPTS=-lc++
fi

# Determine the relevant object files from llvm with a dummy
# compilation. Passing -t to the linker gets it to list which
# object files in which archives it uses to resolve
# symbols. We only care about the libLLVM ones.
# (Send errors to /dev/null since it may otherwise list
# various missing symbols which are irrelevant here.)
LIST=`$4 -o /dev/null -shared $TMP/*.o -Wl,-t ${LINKOPTS} $5 2> /dev/null | egrep libLLVM`

# Extract the necessary object files from the llvm archives.
# Make each name unique as we go along, in case there are dupes.
UNIQUE=0
cd $TMP
for LINE in $LIST; do
    # expect each line to be  /path/to/some.a(some.o)
    # or                     (/path/to/some.a)some.o
    ARCHIVE=`echo "${LINE}" | sed -E -e 's/[(]*(.+\.a)[()].*/\1/'`
    OBJ=`echo "${LINE}" | sed -E -e 's/[(]*.+\.a[()](.*\.o).*/\1/'`
    ar x $ARCHIVE $OBJ; mv $OBJ llvm_${UNIQUE}_${OBJ}
    UNIQUE=$((UNIQUE + 1))
done
cd $WD

# Archive together all the halide and llvm object files. Start by
# just copying over the previous 
cp $BEST_ARCHIVE $3
chmod +w $3

# (Use xargs because ar breaks on MinGW with all objects at the same time.)
echo $TMP/llvm_*.o* | xargs -n200 ar q $3
ranlib $3

rm -rf $TMP

