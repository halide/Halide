#!/bin/bash
#
# gengen.sh is a simple utility script that will take .cpp file(s) containing
# one (or more) Generators, compile them, link them with libHalide, and run
# the resulting executable to produce a .o/.h expressing the Generator's
# function.
#
# Flags:
# -c : C++ compiler to use [default: $CXX]
# -l : path to libHalide [default: bin/libHalide.so]
# -o : directory path for output files [REQUIRED]
# -s : path to .cpp file(s) containing Generators [REQUIRED]
# -g : name of the Generator to produce. [Can be omitted if there's only one in the .cpp]
# -f : C function name for the result. [Defaults to the Generator name if omitted]
# -e : Emit options. [Defaults to static_library, h]
# -n : Output filename base. [Defaults to same as -f]
# -x : Extension options. [Defaults to none.]
#
# Flags are followed by GeneratorParam values, in the form name=value name=value etc.
# You must always set the 'target' params common to all Generators;
# all others are optional.

set -e

usage()
{
  echo `basename $0` -c CXX -l LIBHALIDE -o OUTPUT_DIR -s GENERATOR_SRC [-g GENERATOR_NAME] [-f FUNCTION_NAME] [-e EMIT_OPTIONS] [-x EXTENSION_OPTIONS] [-n FILE_BASE_NAME] target=TARGET [generator_param=value ...]
  exit 85
}

TOOLS_DIR=$(cd $(dirname $0) && pwd)
HALIDE_DIR=$(cd ${TOOLS_DIR}/.. && pwd)

LIBHALIDE=${HALIDE_DIR}/bin/libHalide.so

# Initialize sources array.
GENERATOR_SRCS=()

while getopts "c:e:f:g:l:n:o:s:x:" opt; do
  case $opt in
    c)
      CXX="${OPTARG}"
      ;;
    e)
      EMIT_OPTIONS="${OPTARG}"
      ;;
    f)
      FUNCTION_NAME="${OPTARG}"
      ;;
    g)
      GENERATOR_NAME="${OPTARG}"
      ;;
    l)
      LIBHALIDE="${OPTARG}"
      ;;
    n)
      FILE_BASE_NAME="${OPTARG}"
      ;;
    o)
      OUTPUT_DIR="${OPTARG}"
      ;;
    s)
      GENERATOR_SRCS+=("${OPTARG}")
      ;;
    x)
      EXTENSIONS_OPTIONS="${OPTARG}"
      ;;
    *)
      echo "Unknown option: ${opt}"
      usage
      ;;
  esac
done
shift $(($OPTIND - 1))

# It's OK for GENERATOR_NAME and FUNCTION_NAME to be empty:
# if the source we're compiling has only one generator registered,
# we just use that one (and assume that FUNCTION_NAME=GENERATOR_NAME)
if [ -z "${CXX}" ]; then 
  echo "C++ (-c) compiler must be specified."
  usage
fi
if [ -z "${OUTPUT_DIR}" ]; then
  echo "Output directory (-o) must be specified."
  usage
fi
if [ -z "${GENERATOR_SRCS[*]}" ]; then 
  echo "Generator sources (-s) must be specified."
  usage
fi

if [ -n "${GENERATOR_NAME}" ]; then
  GENERATOR_FLAG="-g ${GENERATOR_NAME}"
fi

if [ -n "${FUNCTION_NAME}" ]; then
  FUNCTION_FLAG="-f ${FUNCTION_NAME}"
fi

if [ -n "${EXTENSIONS_OPTIONS}" ]; then
  EXTENSIONS_FLAG="-x ${EXTENSIONS_OPTIONS}"
fi

if [ -n "${FILE_BASE_NAME}" ]; then
  FILE_BASE_NAME_FLAG="-n ${FILE_BASE_NAME}"
fi

if [ -n "${EMIT_OPTIONS}" ]; then
  EMIT_OPTIONS_FLAG="-e ${EMIT_OPTIONS}"
fi

mkdir -p ${OUTPUT_DIR}

GENGEN=`mktemp ${OUTPUT_DIR}/gengen.XXXX`

${CXX} -g -std=c++11 -fno-rtti -I${HALIDE_DIR}/include ${GENERATOR_SRCS[@]} ${TOOLS_DIR}/GenGen.cpp "${LIBHALIDE}" -lz -lpthread -ldl -o ${GENGEN}
${GENGEN} ${GENERATOR_FLAG} ${FUNCTION_FLAG} ${EXTENSIONS_FLAG} ${FILE_BASE_NAME_FLAG} ${EMIT_OPTIONS_FLAG} -o ${OUTPUT_DIR} $@
rm ${GENGEN}
