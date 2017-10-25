#!/bin/bash

# makelib.sh
#
# This is a script to allow combining a list of .a or .o
# files into a single .a file; input .a are decomposed
# into their component .o files. Ordering is maintained.
# It is assumed there are no duplicates. (It's basically the subset
# of libtool that we'd use, if libtool was reliably available everwhere.)
#
# $1 = Output file (always .a)
# $2...$N = Input files (any mixture of .a or .o files)
#
# It is OK to have the same file as an input and output
# (it will of course be overwritten).

if [[ $# -lt 2 ]]; then
    echo At least two arguments are required > /dev/stderr
    exit 1
fi

if [[ $# -eq 2 && ${1} == ${2} ]]; then
    # Output equals input; just return
    exit 0
fi

set -euo pipefail
ORIG_WD=${PWD}

# Output to temp file in case it's also in inputs
OUTPUT_DIR=`mktemp -d`
OUTPUT="${OUTPUT_DIR}/tmp.a"

# $1 == destination .a
# $2 == source .o or .a
append_objects() {
    # Since we are recursive, we need to do this at each level
    set -euo pipefail
    local EXT="${2##*.}"
    if [[ ${EXT} == "o" ]]; then
        ar qS ${1} ${2}
    elif [[ ${EXT} == "a" ]]; then
        local AR_TEMP=`mktemp -d`
        cd ${AR_TEMP}
        ar x ${2}
        cd ${ORIG_WD}
        # Iterate via 'ar t' rather than globbing what we extracted,
        # so that we add in the same order.
        local OBJ
        for OBJ in `ar t ${2}`; do
            if [[ "$OBJ" == */* ]]; then
                echo "This tool does not support Archive files with partial or absolute paths" > /dev/stderr
                exit 1
            fi
            if [[ ${OBJ} != "__.SYMDEF" && ${OBJ} != "SORTED" ]]; then
                $(append_objects ${1} ${AR_TEMP}/${OBJ})
            fi
        done
        rm -rf ${AR_TEMP}
    else
        echo ${2} is neither .o nor .a > /dev/stderr
        exit 1
    fi
}

for INPUT_ in ${@:2}; do
    # Inputs might be relative; convert to an absolute path
    # for ease of use with ar. Since OSX doesn't have a realpath
    # available, we'll have to fake it: if the path doesn't begin
    # with '/', assume it's relative and prepend PWD.
    INPUT=`[[ $INPUT_ = /* ]] && echo "${INPUT_}" || echo "${PWD}/${INPUT_#./}"`
    $(append_objects ${OUTPUT} ${INPUT})
done

ranlib ${OUTPUT}
mv -f ${OUTPUT} $1

rm -r ${OUTPUT_DIR}
