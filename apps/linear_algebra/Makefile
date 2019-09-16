include ../support/Makefile.inc

BIN ?= bin
BUILD = $(BIN)/build
CXXFLAGS += -Wall -march=native -I /opt/local/include
HL_TARGET ?= host
LIBHALIDE_BLAS = $(BIN)/libhalide_blas.a
HALIDEBLAS_FLAGS ?= -DUSE_HALIDE
EMIT_OPTIONS = stmt,assembly,object,h

EIGEN_INCLUDES ?= -I/usr/include/eigen3
CBLAS_LIBS ?= -lblas
CBLAS_FLAGS ?= -DUSE_CBLAS
OPENBLAS_FLAGS ?= -DUSE_OPENBLAS
OPENBLAS_LIBS ?= -L/opt/local/lib -lopenblas

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
HL_TARGET_NR = $(HL_TARGET)-no_runtime-no_asserts-no_bounds_query

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
	$(BIN)/cblas_benchmarks \
	$(BIN)/atlas_benchmarks \
	$(BIN)/openblas_benchmarks \
	$(BIN)/eigen_benchmarks \
	$(BIN)/halide_benchmarks

.PHONY: clean run_benchmarks
all: $(BENCHMARKS)
	make run_benchmarks

ifneq ("$(wildcard /usr/include/cblas.h)","")
test: $(BIN)/test_halide_blas
	$(BIN)/test_halide_blas
else
test:
	@echo /usr/include/cblas.h not found: skipping linear_algebra tests...
endif

clean:
	rm -rf $(BIN)

KERNEL_HEADERS = $(KERNELS:%=$(BUILD)/halide_%.h)
KERNEL_OBJECTS = $(KERNELS:%=$(BUILD)/halide_%.o) $(BUILD)/halide_runtime.o

$(BUILD)/halide_blas.o: src/halide_blas.cpp src/halide_blas.h $(KERNEL_HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c -o $(@) -I ../../include/ -I ../support -I$(BUILD) $(<)

$(LIBHALIDE_BLAS): $(KERNEL_OBJECTS) $(BUILD)/halide_blas.o
	$(AR) q $@ $(filter-out %.a,$^)

$(BIN)/test_halide_blas: tests/test_halide_blas.cpp $(LIBHALIDE_BLAS)
	$(CXX) $(CXXFLAGS) -Wno-unused-variable -o $(@) -I../../include/ -I../support -Isrc -I$(BUILD) \
	$(CBLAS_FLAGS) $^ $(CBLAS_LIBS) $(LDFLAGS)

# Large powers of two are a pathological case for the cache, so avoid
# them for the benchmarks.
L1_BENCHMARK_SIZES = 16 64 288 1056 2080
L2_BENCHMARK_SIZES = 32 64 128 288 544 1056 2080
L3_BENCHMARK_SIZES = 32 64 128 288 544 1056 2080
L1_BENCHMARKS = scopy dcopy sscal dscal saxpy daxpy sdot ddot sasum dasum
L2_BENCHMARKS = sgemv_notrans dgemv_notrans sgemv_trans dgemv_trans sger dger
L3_BENCHMARKS = sgemm_notrans dgemm_notrans sgemm_transA dgemm_transA sgemm_transB dgemm_transB sgemm_transAB dgemm_transAB

cblas_l1_benchmark_%: $(BIN)/cblas_benchmarks
	@$(foreach size,$(L1_BENCHMARK_SIZES),$(BIN)/cblas_benchmarks $(@:cblas_l1_benchmark_%=%) $(size);)

atlas_l1_benchmark_%: $(BIN)/atlas_benchmarks
	@$(foreach size,$(L1_BENCHMARK_SIZES),$(BIN)/atlas_benchmarks $(@:atlas_l1_benchmark_%=%) $(size);)

openblas_l1_benchmark_%: $(BIN)/openblas_benchmarks
	@$(foreach size,$(L1_BENCHMARK_SIZES),$(BIN)/openblas_benchmarks $(@:openblas_l1_benchmark_%=%) $(size);)

eigen_l1_benchmark_%: $(BIN)/eigen_benchmarks
	@$(foreach size,$(L1_BENCHMARK_SIZES),$(BIN)/eigen_benchmarks $(@:eigen_l1_benchmark_%=%) $(size);)

halide_l1_benchmark_%: $(BIN)/halide_benchmarks
	@$(foreach size,$(L1_BENCHMARK_SIZES),$(BIN)/halide_benchmarks $(@:halide_l1_benchmark_%=%) $(size);)

l1_benchmarks: \
	$(L1_BENCHMARKS:%=cblas_l1_benchmark_%) \
	$(L1_BENCHMARKS:%=atlas_l1_benchmark_%) \
	$(L1_BENCHMARKS:%=openblas_l1_benchmark_%) \
	$(L1_BENCHMARKS:%=eigen_l1_benchmark_%) \
	$(L1_BENCHMARKS:%=halide_l1_benchmark_%)

cblas_l2_benchmark_%: $(BIN)/cblas_benchmarks
	@$(foreach size,$(L2_BENCHMARK_SIZES),$(BIN)/cblas_benchmarks $(@:cblas_l2_benchmark_%=%) $(size);)

atlas_l2_benchmark_%: $(BIN)/atlas_benchmarks
	@$(foreach size,$(L2_BENCHMARK_SIZES),$(BIN)/atlas_benchmarks $(@:atlas_l2_benchmark_%=%) $(size);)

openblas_l2_benchmark_%: $(BIN)/openblas_benchmarks
	@$(foreach size,$(L2_BENCHMARK_SIZES),$(BIN)/openblas_benchmarks $(@:openblas_l2_benchmark_%=%) $(size);)

eigen_l2_benchmark_%: $(BIN)/eigen_benchmarks
	@$(foreach size,$(L2_BENCHMARK_SIZES),$(BIN)/eigen_benchmarks $(@:eigen_l2_benchmark_%=%) $(size);)

halide_l2_benchmark_%: $(BIN)/halide_benchmarks
	@$(foreach size,$(L2_BENCHMARK_SIZES),$(BIN)/halide_benchmarks $(@:halide_l2_benchmark_%=%) $(size);)

l2_benchmarks: \
	$(L2_BENCHMARKS:%=cblas_l2_benchmark_%) \
	$(L2_BENCHMARKS:%=atlas_l2_benchmark_%) \
	$(L2_BENCHMARKS:%=openblas_l2_benchmark_%) \
	$(L2_BENCHMARKS:%=eigen_l2_benchmark_%) \
	$(L2_BENCHMARKS:%=halide_l2_benchmark_%)

cblas_l3_benchmark_%: $(BIN)/cblas_benchmarks
	@$(foreach size,$(L3_BENCHMARK_SIZES),$(BIN)/cblas_benchmarks $(@:cblas_l3_benchmark_%=%) $(size);)

atlas_l3_benchmark_%: $(BIN)/atlas_benchmarks
	@$(foreach size,$(L3_BENCHMARK_SIZES),$(BIN)/atlas_benchmarks $(@:atlas_l3_benchmark_%=%) $(size);)

openblas_l3_benchmark_%: $(BIN)/openblas_benchmarks
	@$(foreach size,$(L3_BENCHMARK_SIZES),$(BIN)/openblas_benchmarks $(@:openblas_l3_benchmark_%=%) $(size);)

eigen_l3_benchmark_%: $(BIN)/eigen_benchmarks
	@$(foreach size,$(L3_BENCHMARK_SIZES),$(BIN)/eigen_benchmarks $(@:eigen_l3_benchmark_%=%) $(size);)

halide_l3_benchmark_%: $(BIN)/halide_benchmarks
	@$(foreach size,$(L3_BENCHMARK_SIZES),$(BIN)/halide_benchmarks $(@:halide_l3_benchmark_%=%) $(size);)

l3_benchmarks: \
	$(L3_BENCHMARKS:%=cblas_l3_benchmark_%) \
	$(L3_BENCHMARKS:%=atlas_l3_benchmark_%) \
	$(L3_BENCHMARKS:%=openblas_l3_benchmark_%) \
	$(L3_BENCHMARKS:%=eigen_l3_benchmark_%) \
	$(L3_BENCHMARKS:%=halide_l3_benchmark_%)

run_benchmarks: $(BENCHMARKS)
	@echo " Package     Subroutine    Size             Runtime     GFLOPS"
	@make --no-print-directory l1_benchmarks
	@make --no-print-directory l2_benchmarks
	@make --no-print-directory l3_benchmarks

benchmarks.csv: $(BENCHMARKS)
	make --no-print-directory run_benchmarks > benchmarks.dat
	awk '{printf("%s,%s,%s,%s,%s\n",$$1,$$2,$$3,$$4,$$5)}' benchmarks.dat > benchmarks.csv

$(BIN)/cblas_benchmarks: benchmarks/cblas_benchmarks.cpp benchmarks/clock.h benchmarks/macros.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $(@) -I$(BUILD) $(CBLAS_FLAGS) $(<) $(CBLAS_LIBS)

$(BIN)/atlas_benchmarks: benchmarks/cblas_benchmarks.cpp benchmarks/clock.h benchmarks/macros.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $(@) -I$(BUILD) $(ATLAS_FLAGS) $(<) $(ATLAS_LIBS)

$(BIN)/openblas_benchmarks: benchmarks/cblas_benchmarks.cpp benchmarks/clock.h benchmarks/macros.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $(@) -I$(BUILD) $(OPENBLAS_FLAGS) $(<) $(OPENBLAS_LIBS)

$(BIN)/eigen_benchmarks: benchmarks/eigen_benchmarks.cpp benchmarks/clock.h benchmarks/macros.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $(@) -I$(BUILD) $(EIGEN_INCLUDES) $(<)

$(BIN)/halide_benchmarks: benchmarks/halide_benchmarks.cpp benchmarks/clock.h benchmarks/macros.h $(LIBHALIDE_BLAS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -o $(@) -Isrc -I$(BUILD) $(HALIDEBLAS_FLAGS) $(<) $(LIBHALIDE_BLAS) $(LDFLAGS)

$(BUILD)/%.generator: src/%_generators.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) $(LDFLAGS) -o $@

# This can use any of the generators; pick an arbitrary one
$(BUILD)/halide_runtime.o: $(BUILD)/blas_l1.generator
	$< -o $(BUILD) -e object -r halide_runtime target=$(HL_TARGET)

$(BUILD)/halide_scopy_impl.o $(BUILD)/halide_scopy_impl.h: $(BUILD)/blas_l1.generator
	$< -g saxpy -f halide_scopy_impl -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) vectorize=true scale_x=false add_to_y=false

$(BUILD)/halide_dcopy_impl.o $(BUILD)/halide_dcopy_impl.h: $(BUILD)/blas_l1.generator
	$< -g daxpy -f halide_dcopy_impl -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) vectorize=true scale_x=false add_to_y=false

$(BUILD)/halide_sscal_impl.o $(BUILD)/halide_sscal_impl.h: $(BUILD)/blas_l1.generator
	$< -g saxpy -f halide_sscal_impl -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) vectorize=true scale_x=true add_to_y=false

$(BUILD)/halide_dscal_impl.o $(BUILD)/halide_dscal_impl.h: $(BUILD)/blas_l1.generator
	$< -g daxpy -f halide_dscal_impl -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) vectorize=true scale_x=true add_to_y=false

$(BUILD)/halide_saxpy_impl.o $(BUILD)/halide_saxpy_impl.h: $(BUILD)/blas_l1.generator
	$< -g saxpy -f halide_saxpy_impl -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) vectorize=true scale_x=true add_to_y=true

$(BUILD)/halide_daxpy_impl.o $(BUILD)/halide_daxpy_impl.h: $(BUILD)/blas_l1.generator
	$< -g daxpy -f halide_daxpy_impl -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) vectorize=true scale_x=true add_to_y=true

$(BUILD)/halide_sdot.o $(BUILD)/halide_sdot.h: $(BUILD)/blas_l1.generator
	$< -g sdot -f halide_sdot -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) vectorize=true

$(BUILD)/halide_ddot.o $(BUILD)/halide_ddot.h: $(BUILD)/blas_l1.generator
	$< -g ddot -f halide_ddot -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) vectorize=true

$(BUILD)/halide_sasum.o $(BUILD)/halide_sasum.h: $(BUILD)/blas_l1.generator
	$< -g sasum -f halide_sasum -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) vectorize=true

$(BUILD)/halide_dasum.o $(BUILD)/halide_dasum.h: $(BUILD)/blas_l1.generator
	$< -g dasum -f halide_dasum -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) vectorize=true

$(BUILD)/halide_sgemv_notrans.o $(BUILD)/halide_sgemv_notrans.h: $(BUILD)/blas_l2.generator
	$< -g sgemv -f halide_sgemv_notrans -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) parallel=true vectorize=true transpose=false

$(BUILD)/halide_dgemv_notrans.o $(BUILD)/halide_dgemv_notrans.h: $(BUILD)/blas_l2.generator
	$< -g dgemv -f halide_dgemv_notrans -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) parallel=true vectorize=true transpose=false

$(BUILD)/halide_sgemv_trans.o $(BUILD)/halide_sgemv_trans.h: $(BUILD)/blas_l2.generator
	$< -g sgemv -f halide_sgemv_trans -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) parallel=true vectorize=true transpose=true

$(BUILD)/halide_dgemv_trans.o $(BUILD)/halide_dgemv_trans.h: $(BUILD)/blas_l2.generator
	$< -g dgemv -f halide_dgemv_trans -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) parallel=true vectorize=true transpose=true

$(BUILD)/halide_sger_impl.o $(BUILD)/halide_sger_impl.h: $(BUILD)/blas_l2.generator
	$< -g sger -f halide_sger_impl -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) parallel=true vectorize=true

$(BUILD)/halide_dger_impl.o $(BUILD)/halide_dger_impl.h: $(BUILD)/blas_l2.generator
	$< -g dger -f halide_dger_impl -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) parallel=true vectorize=true

$(BUILD)/halide_sgemm_notrans.o $(BUILD)/halide_sgemm_notrans.h: $(BUILD)/blas_l3.generator
	$< -g sgemm -f halide_sgemm_notrans -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) transpose_A=false transpose_B=false

$(BUILD)/halide_dgemm_notrans.o $(BUILD)/halide_dgemm_notrans.h: $(BUILD)/blas_l3.generator
	$< -g dgemm -f halide_dgemm_notrans -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) transpose_A=false transpose_B=false

$(BUILD)/halide_sgemm_transA.o $(BUILD)/halide_sgemm_transA.h: $(BUILD)/blas_l3.generator
	$< -g sgemm -f halide_sgemm_transA -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) transpose_A=true transpose_B=false

$(BUILD)/halide_dgemm_transA.o $(BUILD)/halide_dgemm_transA.h: $(BUILD)/blas_l3.generator
	$< -g dgemm -f halide_dgemm_transA -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) transpose_A=true transpose_B=false

$(BUILD)/halide_sgemm_transB.o $(BUILD)/halide_sgemm_transB.h: $(BUILD)/blas_l3.generator
	$< -g sgemm -f halide_sgemm_transB -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) transpose_A=false transpose_B=true

$(BUILD)/halide_dgemm_transB.o $(BUILD)/halide_dgemm_transB.h: $(BUILD)/blas_l3.generator
	$< -g dgemm -f halide_dgemm_transB -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) transpose_A=false transpose_B=true

$(BUILD)/halide_sgemm_transAB.o $(BUILD)/halide_sgemm_transAB.h: $(BUILD)/blas_l3.generator
	$< -g sgemm -f halide_sgemm_transAB -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) transpose_A=true transpose_B=true

$(BUILD)/halide_dgemm_transAB.o $(BUILD)/halide_dgemm_transAB.h: $(BUILD)/blas_l3.generator
	$< -g dgemm -f halide_dgemm_transAB -o $(BUILD) -e $(EMIT_OPTIONS) \
	target=$(HL_TARGET_NR) transpose_A=true transpose_B=true
