include ../support/Makefile.inc

EIGEN_DIR ?= /usr/include/eigen3
CXXFLAGS += -O3 -fopenmp -Wall -std=c++11
HL_TARGET ?= host
LIBHALIDE_BLAS = src/libhalide_blas.a

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
	dgemv_trans

BENCHMARKS = \
	benchmarks/eigen_benchmarks \
	benchmarks/halide_benchmarks

LIBS = $(filter-out -lrt -lz -lpthread -ldl , $(LLVM_STATIC_LIBS)) \
	../../$(LIB_HALIDE)

.PHONY: clean run_benchmarks
all: $(BENCHMARKS)
	make run_benchmarks

clean:
	rm -rf $(KERNEL_DIR)
	rm -rf src/*.o
	rm -rf $(LIBHALIDE_BLAS)
	rm -f $(BENCHMARKS)

KERNEL_HEADERS = $(KERNELS:%=$(KERNEL_DIR)/halide_%.h)
KERNEL_OBJECTS = $(KERNELS:%=$(KERNEL_DIR)/halide_%.o)
$(LIBHALIDE_BLAS): src/halide_blas.o $(KERNEL_OBJECTS)
	$(MAKE) -C ../../ $(LIB_HALIDE)
	$(LD) -r -o src/BLAS.o src/halide_blas.o $(KERNEL_OBJECTS) $(LIBS)
	rm -f $(LIBHALIDE_BLAS)
	ar q $(LIBHALIDE_BLAS) src/BLAS.o
	ranlib $(LIBHALIDE_BLAS)

src/halide_blas.o: src/halide_blas.cpp src/halide_blas.h $(KERNEL_HEADERS)
	$(CXX) $(CXXFLAGS) -c -o $(@) -I ../../include/ -I ../support -I$(KERNEL_DIR) $(<)

run_benchmarks: $(BENCHMARKS)
	benchmarks/eigen_benchmarks  axpy 8196
	benchmarks/halide_benchmarks axpy 8196
	benchmarks/eigen_benchmarks  gemv 1024
	benchmarks/halide_benchmarks gemv 1024

benchmarks/%: benchmarks/%.cpp $(LIBHALIDE_BLAS)
	$(CXX) $(CXXFLAGS) -o $(@) -I../../include/ -I../support -I$(EIGEN_DIR) -Isrc -I$(KERNEL_DIR) $(<) $(LIBHALIDE_BLAS) ../../$(LIB_HALIDE) -lpthread -ldl -lz $(LLVM_LDFLAGS)

$(KERNEL_DIR):
	@mkdir -p $(KERNEL_DIR)

$(KERNEL_DIR)/%.generator: src/%_generators.cpp $(GENGEN_DEPS) $(KERNEL_DIR)
	$(CXX) -std=c++11 -fno-rtti -I../../include $(<) ../../tools/GenGen.cpp ../../$(LIB_HALIDE) $(LLVM_LDFLAGS) -o $@

$(KERNEL_DIR)/halide_scopy_impl.o $(KERNEL_DIR)/halide_scopy_impl.h: $(KERNEL_DIR)/blas_l1.generator
	$(LD_PATH_SETUP) $< -g saxpy -f halide_scopy_impl -o $(KERNEL_DIR) target=$(HL_TARGET) vectorize=true scale_x=false add_to_y=false

$(KERNEL_DIR)/halide_dcopy_impl.o $(KERNEL_DIR)/halide_dcopy_impl.h: $(KERNEL_DIR)/blas_l1.generator
	$(LD_PATH_SETUP) $< -g daxpy -f halide_dcopy_impl -o $(KERNEL_DIR) target=$(HL_TARGET) vectorize=true scale_x=false add_to_y=false

$(KERNEL_DIR)/halide_sscal_impl.o $(KERNEL_DIR)/halide_sscal_impl.h: $(KERNEL_DIR)/blas_l1.generator
	$(LD_PATH_SETUP) $< -g saxpy -f halide_sscal_impl -o $(KERNEL_DIR) target=$(HL_TARGET) vectorize=true scale_x=true add_to_y=false

$(KERNEL_DIR)/halide_dscal_impl.o $(KERNEL_DIR)/halide_dscal_impl.h: $(KERNEL_DIR)/blas_l1.generator
	$(LD_PATH_SETUP) $< -g daxpy -f halide_dscal_impl -o $(KERNEL_DIR) target=$(HL_TARGET) vectorize=true scale_x=true add_to_y=false

$(KERNEL_DIR)/halide_saxpy_impl.o $(KERNEL_DIR)/halide_saxpy_impl.h: $(KERNEL_DIR)/blas_l1.generator
	$(LD_PATH_SETUP) $< -g saxpy -f halide_saxpy_impl -o $(KERNEL_DIR) target=$(HL_TARGET) vectorize=true scale_x=true add_to_y=true

$(KERNEL_DIR)/halide_daxpy_impl.o $(KERNEL_DIR)/halide_daxpy_impl.h: $(KERNEL_DIR)/blas_l1.generator
	$(LD_PATH_SETUP) $< -g daxpy -f halide_daxpy_impl -o $(KERNEL_DIR) target=$(HL_TARGET) vectorize=true scale_x=true add_to_y=true

$(KERNEL_DIR)/halide_sdot.o $(KERNEL_DIR)/halide_sdot.h: $(KERNEL_DIR)/blas_l1.generator
	$(LD_PATH_SETUP) $< -g sdot -f halide_sdot -o $(KERNEL_DIR) target=$(HL_TARGET) vectorize=true

$(KERNEL_DIR)/halide_ddot.o $(KERNEL_DIR)/halide_ddot.h: $(KERNEL_DIR)/blas_l1.generator
	$(LD_PATH_SETUP) $< -g ddot -f halide_ddot -o $(KERNEL_DIR) target=$(HL_TARGET) vectorize=true

$(KERNEL_DIR)/halide_sasum.o $(KERNEL_DIR)/halide_sasum.h: $(KERNEL_DIR)/blas_l1.generator
	$(LD_PATH_SETUP) $< -g sasum -f halide_sasum -o $(KERNEL_DIR) target=$(HL_TARGET) vectorize=true

$(KERNEL_DIR)/halide_dasum.o $(KERNEL_DIR)/halide_dasum.h: $(KERNEL_DIR)/blas_l1.generator
	$(LD_PATH_SETUP) $< -g dasum -f halide_dasum -o $(KERNEL_DIR) target=$(HL_TARGET) vectorize=true

$(KERNEL_DIR)/halide_sgemv_notrans.o $(KERNEL_DIR)/halide_sgemv_notrans.h: $(KERNEL_DIR)/blas_l2.generator
	$(LD_PATH_SETUP) $< -g sgemv -f halide_sgemv_notrans -o $(KERNEL_DIR) target=$(HL_TARGET) transpose=false vectorize=true

$(KERNEL_DIR)/halide_dgemv_notrans.o $(KERNEL_DIR)/halide_dgemv_notrans.h: $(KERNEL_DIR)/blas_l2.generator
	$(LD_PATH_SETUP) $< -g dgemv -f halide_dgemv_notrans -o $(KERNEL_DIR) target=$(HL_TARGET) transpose=false vectorize=true

$(KERNEL_DIR)/halide_sgemv_trans.o $(KERNEL_DIR)/halide_sgemv_trans.h: $(KERNEL_DIR)/blas_l2.generator
	$(LD_PATH_SETUP) $< -g sgemv -f halide_sgemv_trans -o $(KERNEL_DIR) target=$(HL_TARGET) transpose=true vectorize=true

$(KERNEL_DIR)/halide_dgemv_trans.o $(KERNEL_DIR)/halide_dgemv_trans.h: $(KERNEL_DIR)/blas_l2.generator
	$(LD_PATH_SETUP) $< -g dgemv -f halide_dgemv_trans -o $(KERNEL_DIR) target=$(HL_TARGET) transpose=true vectorize=true
