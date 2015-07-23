include ../support/Makefile.inc

CXXFLAGS += -O3 -fopenmp -Wall -std=c++11 -march=native
HL_TARGET ?= host
LIBHALIDE_BLAS = src/libhalide_blas.a
EMIT_OPTIONS = stmt,assembly

EIGEN_INCLUDES ?= -I/usr/include/eigen3
CBLAS_LIBS ?= -lblas
OPENBLAS_FLAGS ?= -DUSE_OPENBLAS
OPENBLAS_LIBS ?= -lopenblas

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
	scopy_impl \
	dcopy_impl \
	sscal_impl \
	dscal_impl \
	saxpy_impl \
	daxpy_impl \
	sdot \
	ddot \
	sasum \
	dasum \
	sgemv_notrans \
	dgemv_notrans \
	sgemv_trans \
	dgemv_trans \
	sger_impl \
	dger_impl \
	sgemm_notrans \
	dgemm_notrans \
	sgemm_transA \
	dgemm_transA \
	sgemm_transB \
	dgemm_transB \
	sgemm_transAB \
	dgemm_transAB \

BENCHMARKS = \
	benchmarks/cblas_benchmarks \
	benchmarks/atlas_benchmarks \
	benchmarks/openblas_benchmarks \
	benchmarks/eigen_benchmarks \
	benchmarks/halide_benchmarks

LIBS = $(filter-out -lrt -lz -lpthread -ldl , $(LLVM_STATIC_LIBS)) \
	../../$(LIB_HALIDE)

.PHONY: clean run_benchmarks
all: $(BENCHMARKS)
	make run_benchmarks

test: tests/test_halide_blas
	tests/test_halide_blas

clean:
	rm -rf $(KERNEL_DIR)
	rm -rf src/*.o
	rm -rf $(LIBHALIDE_BLAS)
	rm -f $(BENCHMARKS)

KERNEL_HEADERS = $(KERNELS:%=$(KERNEL_DIR)/halide_%.h)
KERNEL_OBJECTS = $(KERNELS:%=$(KERNEL_DIR)/halide_%.o) $(KERNEL_DIR)/halide_runtime.o 
$(LIBHALIDE_BLAS): src/halide_blas.o $(KERNEL_OBJECTS)
	$(MAKE) -C ../../ $(LIB_HALIDE)
	$(LD) -r -o src/BLAS.o src/halide_blas.o $(KERNEL_OBJECTS) $(LIBS)
	rm -f $(LIBHALIDE_BLAS)
	ar q $(LIBHALIDE_BLAS) src/BLAS.o
	ranlib $(LIBHALIDE_BLAS)

src/halide_blas.o: src/halide_blas.cpp src/halide_blas.h $(KERNEL_HEADERS)
	$(CXX) $(CXXFLAGS) -c -o $(@) -I ../../include/ -I ../support -I$(KERNEL_DIR) $(<)


tests/test_halide_blas: tests/test_halide_blas.cpp $(LIBHALIDE_BLAS)
	$(CXX) $(CXXFLAGS) -Wno-unused-variable -o $(@) -I../../include/ -I../support -Isrc -I$(KERNEL_DIR) \
	$(CBLAS_FLAGS) $(<) $(LIBHALIDE_BLAS) ../../$(LIB_HALIDE) $(CBLAS_LIBS) $(LLVM_LDFLAGS)

# Large powers of two are a pathological case for the cache, so avoid
# them for the benchmarks.
L1_BENCHMARK_SIZES = 16 64 288 1056 2080
L2_BENCHMARK_SIZES = 8 16 32 64 128 288 544 1056 2080
L3_BENCHMARK_SIZES = 8 16 32 64 128 288 544 1056 2080
L1_BENCHMARKS = scopy dcopy sscal dscal saxpy daxpy sdot ddot sasum dasum
L2_BENCHMARKS = sgemv_notrans dgemv_notrans sgemv_trans dgemv_trans sger dger
L3_BENCHMARKS = sgemm_notrans dgemm_notrans sgemm_transA dgemm_transA sgemm_transB dgemm_transB sgemm_transAB dgemm_transAB

cblas_l1_benchmark_%: benchmarks/cblas_benchmarks
	@$(foreach size,$(L1_BENCHMARK_SIZES),benchmarks/cblas_benchmarks $(@:cblas_l1_benchmark_%=%) $(size);)

atlas_l1_benchmark_%: benchmarks/atlas_benchmarks
	@$(foreach size,$(L1_BENCHMARK_SIZES),benchmarks/atlas_benchmarks $(@:atlas_l1_benchmark_%=%) $(size);)

openblas_l1_benchmark_%: benchmarks/openblas_benchmarks
	@$(foreach size,$(L1_BENCHMARK_SIZES),benchmarks/openblas_benchmarks $(@:openblas_l1_benchmark_%=%) $(size);)

eigen_l1_benchmark_%: benchmarks/eigen_benchmarks
	@$(foreach size,$(L1_BENCHMARK_SIZES),benchmarks/eigen_benchmarks $(@:eigen_l1_benchmark_%=%) $(size);)

halide_l1_benchmark_%: benchmarks/halide_benchmarks
	@$(foreach size,$(L1_BENCHMARK_SIZES),benchmarks/halide_benchmarks $(@:halide_l1_benchmark_%=%) $(size);)

l1_benchmarks: \
	$(L1_BENCHMARKS:%=cblas_l1_benchmark_%) \
	$(L1_BENCHMARKS:%=atlas_l1_benchmark_%) \
	$(L1_BENCHMARKS:%=openblas_l1_benchmark_%) \
	$(L1_BENCHMARKS:%=eigen_l1_benchmark_%) \
	$(L1_BENCHMARKS:%=halide_l1_benchmark_%)

cblas_l2_benchmark_%: benchmarks/cblas_benchmarks
	@$(foreach size,$(L2_BENCHMARK_SIZES),benchmarks/cblas_benchmarks $(@:cblas_l2_benchmark_%=%) $(size);)

atlas_l2_benchmark_%: benchmarks/atlas_benchmarks
	@$(foreach size,$(L2_BENCHMARK_SIZES),benchmarks/atlas_benchmarks $(@:atlas_l2_benchmark_%=%) $(size);)

openblas_l2_benchmark_%: benchmarks/openblas_benchmarks
	@$(foreach size,$(L2_BENCHMARK_SIZES),benchmarks/openblas_benchmarks $(@:openblas_l2_benchmark_%=%) $(size);)

eigen_l2_benchmark_%: benchmarks/eigen_benchmarks
	@$(foreach size,$(L2_BENCHMARK_SIZES),benchmarks/eigen_benchmarks $(@:eigen_l2_benchmark_%=%) $(size);)

halide_l2_benchmark_%: benchmarks/halide_benchmarks
	@$(foreach size,$(L2_BENCHMARK_SIZES),benchmarks/halide_benchmarks $(@:halide_l2_benchmark_%=%) $(size);)

l2_benchmarks: \
	$(L2_BENCHMARKS:%=cblas_l2_benchmark_%) \
	$(L2_BENCHMARKS:%=atlas_l2_benchmark_%) \
	$(L2_BENCHMARKS:%=openblas_l2_benchmark_%) \
	$(L2_BENCHMARKS:%=eigen_l2_benchmark_%) \
	$(L2_BENCHMARKS:%=halide_l2_benchmark_%)

cblas_l3_benchmark_%: benchmarks/cblas_benchmarks
	@$(foreach size,$(L3_BENCHMARK_SIZES),benchmarks/cblas_benchmarks $(@:cblas_l3_benchmark_%=%) $(size);)

atlas_l3_benchmark_%: benchmarks/atlas_benchmarks
	@$(foreach size,$(L3_BENCHMARK_SIZES),benchmarks/atlas_benchmarks $(@:atlas_l3_benchmark_%=%) $(size);)

openblas_l3_benchmark_%: benchmarks/openblas_benchmarks
	@$(foreach size,$(L3_BENCHMARK_SIZES),benchmarks/openblas_benchmarks $(@:openblas_l3_benchmark_%=%) $(size);)

eigen_l3_benchmark_%: benchmarks/eigen_benchmarks
	@$(foreach size,$(L3_BENCHMARK_SIZES),benchmarks/eigen_benchmarks $(@:eigen_l3_benchmark_%=%) $(size);)

halide_l3_benchmark_%: benchmarks/halide_benchmarks
	@$(foreach size,$(L3_BENCHMARK_SIZES),benchmarks/halide_benchmarks $(@:halide_l3_benchmark_%=%) $(size);)

l3_benchmarks: \
	$(L3_BENCHMARKS:%=cblas_l3_benchmark_%) \
	$(L3_BENCHMARKS:%=atlas_l3_benchmark_%) \
	$(L3_BENCHMARKS:%=openblas_l3_benchmark_%) \
	$(L3_BENCHMARKS:%=eigen_l3_benchmark_%) \
	$(L3_BENCHMARKS:%=halide_l3_benchmark_%)

run_benchmarks: $(BENCHMARKS)
	@echo " Package     Subroutine    Size             Runtime     GFLOPS"
#	@echo "======================================================================="
	@make --no-print-directory l1_benchmarks
	@make --no-print-directory l2_benchmarks
	@make --no-print-directory l3_benchmarks

benchmarks.csv: $(BENCHMARKS)
	make --no-print-directory run_benchmarks > benchmarks.dat
	awk '{printf("%s,%s,%s,%s,%s\n",$$1,$$2,$$3,$$4,$$5)}' benchmarks.dat > benchmarks.csv

benchmarks/cblas_benchmarks: benchmarks/cblas_benchmarks.cpp benchmarks/clock.h benchmarks/macros.h $(LIBHALIDE_BLAS)
	$(CXX) $(CXXFLAGS) -o $(@) -I../../include/ -I../support -I$(KERNEL_DIR) $(CBLAS_FLAGS) \
	$(<) $(LIBHALIDE_BLAS) ../../$(LIB_HALIDE) $(CBLAS_LIBS) $(LLVM_LDFLAGS)

benchmarks/atlas_benchmarks: benchmarks/cblas_benchmarks.cpp benchmarks/clock.h benchmarks/macros.h $(LIBHALIDE_BLAS)
	$(CXX) $(CXXFLAGS) -o $(@) -I../../include/ -I../support -I$(KERNEL_DIR) $(ATLAS_FLAGS) \
	$(<) $(LIBHALIDE_BLAS) ../../$(LIB_HALIDE) $(ATLAS_LIBS) $(LLVM_LDFLAGS)

benchmarks/openblas_benchmarks: benchmarks/cblas_benchmarks.cpp benchmarks/clock.h benchmarks/macros.h $(LIBHALIDE_BLAS)
	$(CXX) $(CXXFLAGS) -o $(@) -I../../include/ -I../support -I$(KERNEL_DIR) $(OPENBLAS_FLAGS) \
	$(<) $(LIBHALIDE_BLAS) ../../$(LIB_HALIDE) $(OPENBLAS_LIBS) $(LLVM_LDFLAGS)

benchmarks/eigen_benchmarks: benchmarks/eigen_benchmarks.cpp benchmarks/clock.h benchmarks/macros.h $(LIBHALIDE_BLAS)
	$(CXX) $(CXXFLAGS) -o $(@) -I../../include/ -I../support -I$(KERNEL_DIR) $(EIGEN_INCLUDES) \
	$(<) $(LIBHALIDE_BLAS) ../../$(LIB_HALIDE) $(LLVM_LDFLAGS)

benchmarks/halide_benchmarks: benchmarks/halide_benchmarks.cpp benchmarks/clock.h benchmarks/macros.h $(LIBHALIDE_BLAS)
	$(CXX) $(CXXFLAGS) -o $(@) -I../../include/ -I../support -Isrc -I$(KERNEL_DIR) \
	$(<) $(LIBHALIDE_BLAS) ../../$(LIB_HALIDE) $(LLVM_LDFLAGS)

$(KERNEL_DIR)/%.generator: src/%_generators.cpp $(GENERATOR_DEPS)
	@mkdir -p $(KERNEL_DIR)
	$(CXX) -std=c++11 -fno-rtti -I../../include $(filter-out %.h,$^) $(LLVM_LDFLAGS) -o $@

# This can use any of the generators; pick an arbitrary one
$(KERNEL_DIR)/halide_runtime.o: $(KERNEL_DIR)/blas_l1.generator
	$< -g saxpy -o $(KERNEL_DIR) -r $(@F) target=$(HL_TARGET) 

$(KERNEL_DIR)/halide_scopy_impl.o $(KERNEL_DIR)/halide_scopy_impl.h: $(KERNEL_DIR)/blas_l1.generator
	$(LD_PATH_SETUP) $< -g saxpy -f halide_scopy_impl -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) vectorize=true scale_x=false add_to_y=false

$(KERNEL_DIR)/halide_dcopy_impl.o $(KERNEL_DIR)/halide_dcopy_impl.h: $(KERNEL_DIR)/blas_l1.generator
	$(LD_PATH_SETUP) $< -g daxpy -f halide_dcopy_impl -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) vectorize=true scale_x=false add_to_y=false

$(KERNEL_DIR)/halide_sscal_impl.o $(KERNEL_DIR)/halide_sscal_impl.h: $(KERNEL_DIR)/blas_l1.generator
	$(LD_PATH_SETUP) $< -g saxpy -f halide_sscal_impl -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) vectorize=true scale_x=true add_to_y=false

$(KERNEL_DIR)/halide_dscal_impl.o $(KERNEL_DIR)/halide_dscal_impl.h: $(KERNEL_DIR)/blas_l1.generator
	$(LD_PATH_SETUP) $< -g daxpy -f halide_dscal_impl -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) vectorize=true scale_x=true add_to_y=false

$(KERNEL_DIR)/halide_saxpy_impl.o $(KERNEL_DIR)/halide_saxpy_impl.h: $(KERNEL_DIR)/blas_l1.generator
	$(LD_PATH_SETUP) $< -g saxpy -f halide_saxpy_impl -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) vectorize=true scale_x=true add_to_y=true

$(KERNEL_DIR)/halide_daxpy_impl.o $(KERNEL_DIR)/halide_daxpy_impl.h: $(KERNEL_DIR)/blas_l1.generator
	$(LD_PATH_SETUP) $< -g daxpy -f halide_daxpy_impl -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) vectorize=true scale_x=true add_to_y=true

$(KERNEL_DIR)/halide_sdot.o $(KERNEL_DIR)/halide_sdot.h: $(KERNEL_DIR)/blas_l1.generator
	$(LD_PATH_SETUP) $< -g sdot -f halide_sdot -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) vectorize=true

$(KERNEL_DIR)/halide_ddot.o $(KERNEL_DIR)/halide_ddot.h: $(KERNEL_DIR)/blas_l1.generator
	$(LD_PATH_SETUP) $< -g ddot -f halide_ddot -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) vectorize=true

$(KERNEL_DIR)/halide_sasum.o $(KERNEL_DIR)/halide_sasum.h: $(KERNEL_DIR)/blas_l1.generator
	$(LD_PATH_SETUP) $< -g sasum -f halide_sasum -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) vectorize=true

$(KERNEL_DIR)/halide_dasum.o $(KERNEL_DIR)/halide_dasum.h: $(KERNEL_DIR)/blas_l1.generator
	$(LD_PATH_SETUP) $< -g dasum -f halide_dasum -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) vectorize=true

$(KERNEL_DIR)/halide_sgemv_notrans.o $(KERNEL_DIR)/halide_sgemv_notrans.h: $(KERNEL_DIR)/blas_l2.generator
	$(LD_PATH_SETUP) $< -g sgemv -f halide_sgemv_notrans -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) parallel=false vectorize=true transpose=false

$(KERNEL_DIR)/halide_dgemv_notrans.o $(KERNEL_DIR)/halide_dgemv_notrans.h: $(KERNEL_DIR)/blas_l2.generator
	$(LD_PATH_SETUP) $< -g dgemv -f halide_dgemv_notrans -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) parallel=false vectorize=true transpose=false

$(KERNEL_DIR)/halide_sgemv_trans.o $(KERNEL_DIR)/halide_sgemv_trans.h: $(KERNEL_DIR)/blas_l2.generator
	$(LD_PATH_SETUP) $< -g sgemv -f halide_sgemv_trans -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) parallel=false vectorize=true transpose=true

$(KERNEL_DIR)/halide_dgemv_trans.o $(KERNEL_DIR)/halide_dgemv_trans.h: $(KERNEL_DIR)/blas_l2.generator
	$(LD_PATH_SETUP) $< -g dgemv -f halide_dgemv_trans -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) parallel=false vectorize=true transpose=true

$(KERNEL_DIR)/halide_sger_impl.o $(KERNEL_DIR)/halide_sger_impl.h: $(KERNEL_DIR)/blas_l2.generator
	$(LD_PATH_SETUP) $< -g sger -f halide_sger_impl -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) parallel=false vectorize=true

$(KERNEL_DIR)/halide_dger_impl.o $(KERNEL_DIR)/halide_dger_impl.h: $(KERNEL_DIR)/blas_l2.generator
	$(LD_PATH_SETUP) $< -g dger -f halide_dger_impl -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) parallel=false vectorize=true

$(KERNEL_DIR)/halide_sgemm_notrans.o $(KERNEL_DIR)/halide_sgemm_notrans.h: $(KERNEL_DIR)/blas_l3.generator
	$(LD_PATH_SETUP) $< -g sgemm -f halide_sgemm_notrans -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) parallel=false vectorize=true transpose_A=false transpose_B=false

$(KERNEL_DIR)/halide_dgemm_notrans.o $(KERNEL_DIR)/halide_dgemm_notrans.h: $(KERNEL_DIR)/blas_l3.generator
	$(LD_PATH_SETUP) $< -g dgemm -f halide_dgemm_notrans -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) parallel=false vectorize=true transpose_A=false transpose_B=false

$(KERNEL_DIR)/halide_sgemm_transA.o $(KERNEL_DIR)/halide_sgemm_transA.h: $(KERNEL_DIR)/blas_l3.generator
	$(LD_PATH_SETUP) $< -g sgemm -f halide_sgemm_transA -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) parallel=false vectorize=true transpose_A=true transpose_B=false

$(KERNEL_DIR)/halide_dgemm_transA.o $(KERNEL_DIR)/halide_dgemm_transA.h: $(KERNEL_DIR)/blas_l3.generator
	$(LD_PATH_SETUP) $< -g dgemm -f halide_dgemm_transA -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) parallel=false vectorize=true transpose_A=true transpose_B=false

$(KERNEL_DIR)/halide_sgemm_transB.o $(KERNEL_DIR)/halide_sgemm_transB.h: $(KERNEL_DIR)/blas_l3.generator
	$(LD_PATH_SETUP) $< -g sgemm -f halide_sgemm_transB -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) parallel=false vectorize=true transpose_A=false transpose_B=true

$(KERNEL_DIR)/halide_dgemm_transB.o $(KERNEL_DIR)/halide_dgemm_transB.h: $(KERNEL_DIR)/blas_l3.generator
	$(LD_PATH_SETUP) $< -g dgemm -f halide_dgemm_transB -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) parallel=false vectorize=true transpose_A=false transpose_B=true

$(KERNEL_DIR)/halide_sgemm_transAB.o $(KERNEL_DIR)/halide_sgemm_transAB.h: $(KERNEL_DIR)/blas_l3.generator
	$(LD_PATH_SETUP) $< -g sgemm -f halide_sgemm_transAB -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) parallel=false vectorize=true transpose_A=true transpose_B=true

$(KERNEL_DIR)/halide_dgemm_transAB.o $(KERNEL_DIR)/halide_dgemm_transAB.h: $(KERNEL_DIR)/blas_l3.generator
	$(LD_PATH_SETUP) $< -g dgemm -f halide_dgemm_transAB -o $(KERNEL_DIR) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) parallel=false vectorize=true transpose_A=true transpose_B=true
