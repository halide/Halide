include ../support/Makefile.inc

CXXFLAGS += -O3 -fopenmp -Wall -std=c++11 -march=native -mfma -mavx
HL_TARGET ?= host
LIBHALIDE_BLAS = src/libhalide_blas.a
EMIT_OPTIONS = stmt,assembly

EIGEN_INCLUDES ?= -I$(HOME)/eigen
CBLAS_LIBS ?= -lblas
OPENBLAS_FLAGS ?= -DUSE_OPENBLAS -I/usr/local/OpenBLAS/include
OPENBLAS_LIBS ?= /usr/local/OpenBLAS/lib/libopenblas.a

GEMMLOWP_INCLUDES ?= -I$(HOME)/gemmlowp
GEMMLOWP_SRC ?= $(HOME)/gemmlowp/eight_bit_int_gemm/eight_bit_int_gemm.cc

# ATLAS should be built and installed locally to get the best performance.
# It is designed to automatically tune its performance to your machine during
# the build. Get the source code here: http://math-atlas.sourceforge.net/,
# then set the flags below to point to your local build of ATLAS.
#
# For example:
# ATLAS_FLAGS ?= -DUSE_ATLAS -I/opt/ATLAS/include
# ATLAS_LIBS ?= -L/opt/ATLAS/lib -lptcblas -latlas

# By default use whatever ATLAS is installed, so that this at least builds.
$(warning Warning: Defaulting to system ATLAS, which may be slow. See the Makefile for details.)
ATLAS_FLAGS ?= -DUSE_ATLAS
ATLAS_LIBS ?= -lcblas

# Note that we deliberately build the generators with the no_runtime flag;
# this provides a slight build speed increase (since we don't have to redundantly
# include the runtime code in each generator) with the extra complication that we
# must explicitly build and link the halide runtime separately.
HL_TARGET_NR = $(HL_TARGET)-no_runtime

KERNEL_DIR = src/kernels
KERNELS = \
	igemm_notrans \
	igemm_transA \
	igemm_transB \
	igemm_transAB \
	igemm_transC \
	igemm_transAC \
	igemm_transBC \
	igemm_transABC \

BENCHMARKS = \
	benchmarks/halide_benchmarks \
	benchmarks/gemmlowp_benchmarks

LIBS = $(filter-out -lrt -lz -lpthread -ldl , $(LLVM_STATIC_LIBS)) \
	$(LIB_HALIDE)

.PHONY: clean run_benchmarks
all: $(BENCHMARKS)
	make run_benchmarks

test: tests/test_halide_blas
	tests/test_halide_blas

benchmark: benchmarks/benchmark
	benchmarks/benchmark

clean:
	rm -rf $(KERNEL_DIR)
	rm -rf src/*.o
	rm -rf $(LIBHALIDE_BLAS)
	rm -f $(BENCHMARKS)

KERNEL_HEADERS = $(KERNELS:%=$(KERNEL_DIR)/halide_%.h)
KERNEL_OBJECTS = $(KERNELS:%=$(KERNEL_DIR)/halide_%.o) $(KERNEL_DIR)/halide_runtime.o
$(LIBHALIDE_BLAS): src/halide_blas.o $(KERNEL_OBJECTS)
	$(LD) -r -o src/BLAS.o src/halide_blas.o $(KERNEL_OBJECTS) $(LIBS)
	rm -f $(LIBHALIDE_BLAS)
	ar q $(LIBHALIDE_BLAS) src/BLAS.o
	ranlib $(LIBHALIDE_BLAS)

src/halide_blas.o: src/halide_blas.cpp src/halide_blas.h $(KERNEL_HEADERS)
	$(CXX) $(CXXFLAGS) -c -o $(@) -I ../../include/ -I ../support -I$(KERNEL_DIR) $(<)


tests/test_halide_blas: tests/test_halide_blas.cpp $(LIBHALIDE_BLAS)
	$(CXX) $(CXXFLAGS) -Wno-unused-variable -o $(@) -I../../include/ -I../support -Isrc -I$(KERNEL_DIR) $(GEMMLOWP_INCLUDES) \
	$(EIGEN_INCLUDES) $(<) $(GEMMLOWP_SRC) $(LIBHALIDE_BLAS) $(LIB_HALIDE) $(LLVM_LDFLAGS)

benchmarks/benchmark: benchmarks/benchmark.cpp $(LIBHALIDE_BLAS)
	$(CXX) $(CXXFLAGS) -Wno-unused-variable -o $(@) -I../../include/ -I../support -Isrc -I$(KERNEL_DIR) $(GEMMLOWP_INCLUDES) \
	$(<) $(GEMMLOWP_SRC) $(LIBHALIDE_BLAS) $(LIB_HALIDE) $(LLVM_LDFLAGS)

# Large powers of two are a pathological case for the cache, so avoid
# them for the benchmarks.
L3_BENCHMARK_SIZES = 32 64 128 288 544 1056 2080
L3_BENCHMARKS = igemm_notrans igemm_transA igemm_transB igemm_transAB igemm_transC igemm_transAC igemm_transBC igemm_transABC

gemmlowp_l3_benchmark_%: benchmarks/gemmlowp_benchmarks
	@$(foreach size,$(L3_BENCHMARK_SIZES),benchmarks/gemmlowp_benchmarks $(@:gemmlowp_l3_benchmark_%=%) $(size);)

halide_l3_benchmark_%: benchmarks/halide_benchmarks
	@$(foreach size,$(L3_BENCHMARK_SIZES),benchmarks/halide_benchmarks $(@:halide_l3_benchmark_%=%) $(size);)

l3_benchmarks: \
	$(L3_BENCHMARKS:%=halide_l3_benchmark_%) \
	$(L3_BENCHMARKS:%=gemmlowp_l3_benchmark_%) \

run_benchmarks: $(BENCHMARKS)
	@echo " Package     Subroutine    Size             Runtime     GFLOPS"
#	@echo "======================================================================="
	@make --no-print-directory l3_benchmarks

benchmarks.csv: $(BENCHMARKS)
	make --no-print-directory run_benchmarks > benchmarks.dat
	awk '{printf("%s,%s,%s,%s,%s\n",$$1,$$2,$$3,$$4,$$5)}' benchmarks.dat > benchmarks.csv

benchmarks/gemmlowp_benchmarks: benchmarks/gemmlowp_benchmarks.cpp benchmarks/clock.h benchmarks/macros.h $(LIBHALIDE_BLAS)
	$(CXX) $(CXXFLAGS) -o $(@) -I../../include/ -I../support -I$(KERNEL_DIR) $(GEMMLOWP_INCLUDES) \
	$(<) $(GEMMLOWP_SRC) $(LIBHALIDE_BLAS) $(LIB_HALIDE) $(LLVM_LDFLAGS)

benchmarks/halide_benchmarks: benchmarks/halide_benchmarks.cpp benchmarks/clock.h benchmarks/macros.h $(LIBHALIDE_BLAS)
	$(CXX) $(CXXFLAGS) -o $(@) -I../../include/ -I../support -Isrc -I$(KERNEL_DIR) \
	$(<) $(LIBHALIDE_BLAS) $(LIB_HALIDE) $(LLVM_LDFLAGS)

$(KERNEL_DIR)/%.generator: src/%_generators.cpp $(GENERATOR_DEPS)
	@mkdir -p $(KERNEL_DIR)
	$(CXX) -std=c++11 -fno-rtti -I../../include $(filter-out %.h,$^) $(LLVM_LDFLAGS) -o $@

# This can use any of the generators; pick an arbitrary one
$(KERNEL_DIR)/halide_runtime.o: $(KERNEL_DIR)/blas_l3.generator
	$< -g igemm -o $(KERNEL_DIR) -r $(@F) target=$(HL_TARGET)

$(KERNEL_DIR)/halide_igemm_notrans.o $(KERNEL_DIR)/halide_igemm_notrans.h: $(KERNEL_DIR)/blas_l3.generator
	$(LD_PATH_SETUP) $< -g igemm -f halide_igemm_notrans -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) transpose_A=false transpose_B=false transpose_C=false

$(KERNEL_DIR)/halide_igemm_transA.o $(KERNEL_DIR)/halide_igemm_transA.h: $(KERNEL_DIR)/blas_l3.generator
	$(LD_PATH_SETUP) $< -g igemm -f halide_igemm_transA -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) transpose_A=true transpose_B=false transpose_C=false

$(KERNEL_DIR)/halide_igemm_transB.o $(KERNEL_DIR)/halide_igemm_transB.h: $(KERNEL_DIR)/blas_l3.generator
	$(LD_PATH_SETUP) $< -g igemm -f halide_igemm_transB -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) transpose_A=false transpose_B=true transpose_C=false

$(KERNEL_DIR)/halide_igemm_transAB.o $(KERNEL_DIR)/halide_igemm_transAB.h: $(KERNEL_DIR)/blas_l3.generator
	$(LD_PATH_SETUP) $< -g igemm -f halide_igemm_transAB -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) transpose_A=true transpose_B=true transpose_C=false

$(KERNEL_DIR)/halide_igemm_transC.o $(KERNEL_DIR)/halide_igemm_transC.h: $(KERNEL_DIR)/blas_l3.generator
	$(LD_PATH_SETUP) $< -g igemm -f halide_igemm_transC -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) transpose_A=false transpose_B=false transpose_C=true

$(KERNEL_DIR)/halide_igemm_transAC.o $(KERNEL_DIR)/halide_igemm_transAC.h: $(KERNEL_DIR)/blas_l3.generator
	$(LD_PATH_SETUP) $< -g igemm -f halide_igemm_transAC -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) transpose_A=true transpose_B=false transpose_C=true

$(KERNEL_DIR)/halide_igemm_transBC.o $(KERNEL_DIR)/halide_igemm_transBC.h: $(KERNEL_DIR)/blas_l3.generator
	$(LD_PATH_SETUP) $< -g igemm -f halide_igemm_transBC -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) transpose_A=false transpose_B=true transpose_C=true

$(KERNEL_DIR)/halide_igemm_transABC.o $(KERNEL_DIR)/halide_igemm_transABC.h: $(KERNEL_DIR)/blas_l3.generator
	$(LD_PATH_SETUP) $< -g igemm -f halide_igemm_transABC -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) transpose_A=true transpose_B=true transpose_C=true