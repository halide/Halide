# This Makefile is for Ubuntu 20.04

# First download a release version of Halide from
# https://github.com/halide/Halide/releases and untar it into this
# folder, or set the HALIDE_PATH variable as appropriate.

HALIDE_PATH ?= $(shell ls -d Halide* | grep -v tar.gz | head -n1)
CXX ?= c++
CXXFLAGS ?= -I $(HALIDE_PATH)/include -I $(HALIDE_PATH)/share/Halide/tools -Wall -Werror -O3 -std=c++17
LIBHALIDE_LDFLAGS ?= -L $(HALIDE_PATH)/lib -lHalide -Wl,-rpath,$(HALIDE_PATH)/lib
LDFLAGS ?= -ldl -lpthread -ljpeg -lpng
GENERATOR_OUTPUTS ?= stmt,assembly,static_library,h

.SUFFIXES:

all: figure1 figure2 table1

# Generate the benchmarking data and output images for figure 1
figure1: bin/teaser
	mkdir -p teaser
	bin/teaser
	mv circle*.png teaser

# Generate the averaging trees illustrated in figure 2
figure2: bin/find_kernel bin/find_14641
	bin/find_kernel 0 4 4 # 1 1 kernel
	bin/find_kernel 0 1 2 1
	bin/find_kernel 0 1 1 1 1
	bin/find_kernel 0 1 3 3 1
	bin/find_kernel 0 1 3 3 9
	bin/find_kernel 0 4 12 # 1 3 kernel
	bin/find_14641

# Generate the raw assembly that was used for the instruction counts in table 1
table1: bin/table1
	mkdir -p table1
	bin/table1 3000
	mv *.s table1

# Generate a list of all unbiased kernels up to some size limits
# below. This is slow. Run it overnight. Broken into separate rules so
# you can use make -j.
bin/all_kernels_2_7.txt: bin/enumerate
	bin/enumerate 2 7 > bin/all_kernels_2_7.txt 

bin/all_kernels_3_7.txt: bin/enumerate
	bin/enumerate 3 7 > bin/all_kernels_3_7.txt

bin/all_kernels_4_7.txt: bin/enumerate
	bin/enumerate 4 7 > bin/all_kernels_4_7.txt

bin/all_kernels_5_6.txt: bin/enumerate
	bin/enumerate 5 6 > bin/all_kernels_5_6.txt

bin/all_kernels_6_6.txt: bin/enumerate
	bin/enumerate 6 6 > bin/all_kernels_6_6.txt 

all_kernels.txt: bin/all_kernels_2_7.txt bin/all_kernels_3_7.txt bin/all_kernels_4_7.txt bin/all_kernels_5_6.txt bin/all_kernels_6_6.txt
	echo "Enumerating all kernels. This will take a very long time to run."
	cat $^ > all_kernels.txt

bin/%: %.cpp dag.h
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -march=native $< -o $@ $(LIBHALIDE_LDFLAGS)

bin/bilinear_upsample_generator: bilinear_upsample_generator.cpp $(HALIDE_PATH)/share/Halide/tools/GenGen.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

bin/bilinear_upsample_%.a: bin/bilinear_upsample_generator
	@mkdir -p $(@D)
	$^ -g bilinear_upsample -e $(GENERATOR_OUTPUTS) -o $(@D) -f bilinear_upsample_$* method=$* target=host-disable_llvm_loop_opt input.type=uint8 output.type=uint8

bin/bilinear_upsample_float.a: bin/bilinear_upsample_generator
	@mkdir -p $(@D)
	$^ -g bilinear_upsample -e $(GENERATOR_OUTPUTS) -o $(@D) -f bilinear_upsample_float method=float target=host-disable_llvm_loop_opt input.type=float32 output.type=float32

bin/bilinear_upsample_float_dither.a: bin/bilinear_upsample_generator
	@mkdir -p $(@D)
	$^ -g bilinear_upsample -e $(GENERATOR_OUTPUTS) -o $(@D) -f bilinear_upsample_float_dither method=dither target=host-disable_llvm_loop_opt input.type=float32 output.type=uint8

bin/teaser: teaser.cpp bin/bilinear_upsample_averaging.a bin/bilinear_upsample_round_up.a bin/bilinear_upsample_round_to_even.a bin/bilinear_upsample_dither.a bin/bilinear_upsample_float.a bin/bilinear_upsample_float_dither.a
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -march=native $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS)

clean:
	rm -rf bin
