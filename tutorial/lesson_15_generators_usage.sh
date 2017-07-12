# Halide tutorial lesson 15: Generators part 2

# This shell script demonstrates how to use a binary containing
# Generators from the command line. Normally you'd call these binaries
# from your build system of choice rather than running them manually
# like we do here.

# This script assumes that you're in the tutorials directory, and the
# generator has been compiled for the current system and is called
# "lesson_15_generate".

# To run this script:
# bash lesson_15_generators_usage.sh

# First we define a helper function that checks that a file exists
check_file_exists()
{
    FILE=$1
    if [ ! -f $FILE ]; then
        echo $FILE not found
        exit -1
    fi
}

# And another helper function to check if a symbol exists in an object file
check_symbol()
{
    FILE=$1
    SYM=$2
    if !(nm $FILE | grep $SYM > /dev/null); then
        echo "$SYM not found in $FILE"
    exit -1
    fi
}

# Bail out on error
#set -e

# Set up LD_LIBRARY_PATH so that we can find libHalide.so
export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:../bin
export DYLD_LIBRARY_PATH=${DYLD_LIBRARY_PATH}:../bin

#########################
# Basic generator usage #
#########################

# First let's compile the first generator for the host system:
./lesson_15_generate -g my_first_generator -o . target=host

# That should create a pair of files in the current directory:
# "my_first_generator.a", and "my_first_generator.h", which define a
# function "my_first_generator" representing the compiled pipeline.

check_file_exists my_first_generator.a
check_file_exists my_first_generator.h
check_symbol my_first_generator.a my_first_generator

#####################
# Cross-compilation #
#####################

# We can also use a generator to compile object files for some other
# target. Let's cross-compile a windows 32-bit object file and header
# for the first generator:

./lesson_15_generate \
    -g my_first_generator \
    -f my_first_generator_win32 \
    -o . \
    target=x86-32-windows

# This generates a file called "my_first_generator_win32.lib" in the
# current directory, along with a matching header. The function
# defined is called "my_first_generator_win32".

check_file_exists my_first_generator_win32.lib
check_file_exists my_first_generator_win32.h

################################
# Generating pipeline variants #
################################

# The full set of command-line arguments to the generator binary are:

# -g generator_name : Selects which generator to run. If you only have
# one generator in your binary you can omit this.

# -o directory : Specifies which directory to create the outputs
# in. Usually a build directory.

# -f name : Specifies the name of the generated function. If you omit
# this, it defaults to the generator name.

# -n file_base_name : Specifies the basename of the generated file(s). If
# you omit this, it defaults to the name of the generated function.

# -e static_library,o,h,assembly,bitcode,stmt,html: A list of
# comma-separated values specifying outputs to create. The default is
# "static_library,h". "assembly" generates assembly equivalent to the
# generated object file. "bitcode" generates llvm bitcode for the pipeline.
# "stmt" generates human-readable pseudocode for the pipeline (similar to
# setting HL_DEBUG_CODEGEN). "html" generates an html version of the
# pseudocode, which can be much nicer to read than the raw .stmt file.

# -r file_base_name : Specifies that the generator should create a
# standalone file for just the runtime. For use when generating multiple
# pipelines from a single generator, to be linked together in one
# executable. See example below.

# -x .old=new,.old2=.new2,... : A comma-separated list of file extension
# pairs to substitute during file naming.

# target=... : The target to compile for.

# my_generator_param=value : The value of your generator params.

# Let's now generate some human-readable pseudocode for the first
# generator:

./lesson_15_generate -g my_first_generator -e stmt -o . target=host

check_file_exists my_first_generator.stmt

# The second generator has generator params, which can be specified on
# the command-line after the target. Let's compile a few different variants:
./lesson_15_generate -g my_second_generator -f my_second_generator_1 -o . \
target=host parallel=false scale=3.0 rotation=ccw output_type=uint16

./lesson_15_generate -g my_second_generator -f my_second_generator_2 -o . \
target=host scale=9.0 rotation=ccw output_type=float32

./lesson_15_generate -g my_second_generator -f my_second_generator_3 -o . \
target=host parallel=false output_type=float64

check_file_exists my_second_generator_1.a
check_file_exists my_second_generator_1.h
check_symbol      my_second_generator_1.a my_second_generator_1
check_file_exists my_second_generator_2.a
check_file_exists my_second_generator_2.h
check_symbol      my_second_generator_2.a my_second_generator_2
check_file_exists my_second_generator_3.a
check_file_exists my_second_generator_3.h
check_symbol      my_second_generator_3.a my_second_generator_3

# Use of these generated object files and headers is exactly the same
# as in lesson 10.

######################
# The Halide runtime #
######################

# Each generated Halide object file contains a simple runtime that
# defines things like how to run a parallel for loop, how to launch a
# cuda program, etc. You can see this runtime in the generated object
# files.

echo "The halide runtime:"
nm my_second_generator_1.a | grep "[SWT] _\?halide_"

# Let's define some functions to check that the runtime exists in a file.
check_runtime()
{
    if !(nm $1 | grep "[TSW] _\?halide_" > /dev/null); then
        echo "Halide runtime not found in $1"
    exit -1
    fi
}

check_no_runtime()
{
    if nm $1 | grep "[TSW] _\?halide_" > /dev/null; then
        echo "Halide runtime found in $1"
    exit -1
    fi
}

# Declarations and documentation for these runtime functions are in
# HalideRuntime.h

# If you're compiling and linking multiple Halide pipelines, then the
# multiple copies of the runtime should combine into a single copy
# (via weak linkage). If you're compiling and linking for multiple
# different targets (e.g. avx and non-avx), then the runtimes might be
# different, and you can't control which copy of the runtime the
# linker selects.

# You can control this behavior explicitly by compiling your pipelines
# with the no_runtime target flag. Let's generate and link several
# different versions of the first pipeline for different x86 variants:

# (Note that we'll ask the generators to just give us object files ("-e o"), 
# instead of static libraries, so that we can easily link them all into a 
# single static library.)

./lesson_15_generate \
    -g my_first_generator \
    -f my_first_generator_basic \
    -e o,h \
    -o . \
    target=host-x86-64-no_runtime

./lesson_15_generate \
    -g my_first_generator \
    -f my_first_generator_sse41 \
    -e o,h \
    -o . \
    target=host-x86-64-sse41-no_runtime

./lesson_15_generate \
    -g my_first_generator \
    -f my_first_generator_avx \
    -e o,h \
    -o . \
    target=host-x86-64-avx-no_runtime

# These files don't contain the runtime
check_no_runtime my_first_generator_basic.o
check_symbol     my_first_generator_basic.o my_first_generator_basic
check_no_runtime my_first_generator_sse41.o
check_symbol     my_first_generator_sse41.o my_first_generator_sse41
check_no_runtime my_first_generator_avx.o
check_symbol     my_first_generator_avx.o my_first_generator_avx

# We can then use the generator to emit just the runtime:
./lesson_15_generate \
    -r halide_runtime_x86 \
    -e o,h \
    -o . \
    target=host-x86-64
check_runtime halide_runtime_x86.o

# Linking the standalone runtime with the three generated object files     
# gives us three versions of the pipeline for varying levels of x86,      
# combined with a single runtime that will work on nearly all x86     
# processors.
ar q my_first_generator_multi.a \
    my_first_generator_basic.o \
    my_first_generator_sse41.o \
    my_first_generator_avx.o \
    halide_runtime_x86.o

check_runtime my_first_generator_multi.a
check_symbol  my_first_generator_multi.a my_first_generator_basic
check_symbol  my_first_generator_multi.a my_first_generator_sse41
check_symbol  my_first_generator_multi.a my_first_generator_avx

echo "Success!"
