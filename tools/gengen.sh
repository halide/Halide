#!/bin/bash
#
# gengen.sh is a simple utility script that will take .cpp file(s) containing
# one (or more) Generators, compile them, link them with libHalide, and run
# the resulting executable to produce a .o/.h expressing the Generator's
# function.
#
# Flags:
# -c : C++ compiler to use [default: $CXX]
# -l : path to libHalide [default: bin/libHalide.a]
# -o : directory path for output files [REQUIRED]
# -s : path to .cpp file(s) containing Generaors [REQUIRED]
# -g : name of the Generator to produce. [Can be omitted if there's only one in the .cpp]
# -f : C function name for the result. [Defaults to the Generator name if omitted]
#
# Flags are followed by GeneratorParam values, in the form name=value name=value etc.
# You must always set the 'target' params common to all Generators;
# all others are optional.

set -e

usage()
{
  echo `basename $0` -c CXX -l LIBHALIDE -o OUTPUT_DIR -s GENERATOR_SRC [-g GENERATOR_NAME] [-f FUNCTION_NAME] target=TARGET [generator_param=value ...]
  exit 85
}

TOOLS_DIR=$(cd $(dirname $0) && pwd)
HALIDE_DIR=$(cd ${TOOLS_DIR}/.. && pwd)

LIBHALIDE=${HALIDE_DIR}/bin/libHalide.a

while getopts "c:f:g:l:o:s:" opt; do
  case $opt in
    c)
      CXX=${OPTARG}
      ;;
    f)
      FUNCTION_NAME=${OPTARG}
      ;;
    g)
      GENERATOR_NAME=${OPTARG}
      ;;
    l)
      LIBHALIDE=${OPTARG}
      ;;
    o)
      OUTPUT_DIR=${OPTARG}
      ;;
    s)
      GENERATOR_SRCS=${OPTARG}
      ;;
    *)
      usage
      ;;
  esac
done
shift $(($OPTIND - 1))

# It's OK for GENERATOR_NAME and FUNCTION_NAME to be empty:
# if the source we're compiling has only one generator registered,
# we just use that one (and assume that FUNCTION_NAME=GENERATOR_NAME)
if [ -z "${CXX}" ] || [ -z "${OUTPUT_DIR}" ] || [ -z "${GENERATOR_SRCS}" ]; then
  usage
fi

if [ -n "${GENERATOR_NAME}" ]; then
  GENERATOR_FLAG="-g ${GENERATOR_NAME}"
fi

if [ -n "${FUNCTION_NAME}" ]; then
  FUNCTION_FLAG="-f ${FUNCTION_NAME}"
fi

mkdir -p ${OUTPUT_DIR}
GENGEN=`mktemp ${OUTPUT_DIR}/gengen.XXXX`
${CXX} -std=c++11 -fno-rtti -I${HALIDE_DIR}/include ${GENERATOR_SRCS} ${TOOLS_DIR}/GenGen.cpp ${LIBHALIDE} -lz -o ${GENGEN}
${GENGEN} ${GENERATOR_FLAG} ${FUNCTION_FLAG} -o ${OUTPUT_DIR} $@
