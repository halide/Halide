EXT_NAME = HalideLinAlg

SOURCE_FILES = \
	Matrix.cpp

HEADER_FILES = \
	Matrix.h

ifdef EIGEN_DIR
WITH_EIGEN = 1
EIGEN_CXX_FLAGS = -DWITH_EIGEN -I$(EIGEN_DIR)
endif

ifdef BLAS_DIR
WITH_BLAS = 1
BLAS_CXX_FLAGS = -DWITH_BLAS -I$(BLAS_DIR)/include
BLAS_LDFLAGS = -L$(BLAS_DIR)/lib -lblas
endif

include ../support/Makefile.inc

TEST_CXX_FLAGS += $(EIGEN_CXX_FLAGS) $(BLAS_CXX_FLAGS)

# "matrix_multiply_0" is produced by using matrix_multiply with different generator args.
$(FILTERS_DIR)/matrix_multiply_0.o $(FILTERS_DIR)/matrix_multiply_0.h: $(FILTERS_DIR)/matrix_multiply.generator
	@-mkdir -p tmp
	cd tmp; $(LD_PATH_SETUP) ../$< -o ../$(FILTERS_DIR) -f matrix_multiply_0 target=$(HL_TARGET)-no_asserts-no_bounds_query algorithm=0

# "matrix_multiply_1" is produced by using matrix_multiply with different generator args.
$(FILTERS_DIR)/matrix_multiply_1.o $(FILTERS_DIR)/matrix_multiply_1.h: $(FILTERS_DIR)/matrix_multiply.generator
	@-mkdir -p tmp
	cd tmp; $(LD_PATH_SETUP) ../$< -o ../$(FILTERS_DIR) -f matrix_multiply_1 target=$(HL_TARGET)-no_asserts-no_bounds_query algorithm=1

# "matrix_multiply_2" is produced by using matrix_multiply with different generator args.
$(FILTERS_DIR)/matrix_multiply_2.o $(FILTERS_DIR)/matrix_multiply_2.h: $(FILTERS_DIR)/matrix_multiply.generator
	@-mkdir -p tmp
	cd tmp; $(LD_PATH_SETUP) ../$< -o ../$(FILTERS_DIR) -f matrix_multiply_2 target=$(HL_TARGET)-no_asserts-no_bounds_query algorithm=2

# "matrix_multiply_3" is produced by using matrix_multiply with different generator args.
$(FILTERS_DIR)/matrix_multiply_3.o $(FILTERS_DIR)/matrix_multiply_3.h: $(FILTERS_DIR)/matrix_multiply.generator
	@-mkdir -p tmp
	cd tmp; $(LD_PATH_SETUP) ../$< -o ../$(FILTERS_DIR) -f matrix_multiply_3 target=$(HL_TARGET)-no_asserts-no_bounds_query algorithm=3

# matrix_multiply test using the internal matrix class.
$(BIN_DIR)/generator_aot_matrix_multiply: test/generator/matrix_multiply_aottest.cpp $(FILTERS_DIR)/matrix_multiply_0.o $(FILTERS_DIR)/matrix_multiply_1.o $(FILTERS_DIR)/matrix_multiply_2.o $(FILTERS_DIR)/matrix_multiply_3.o
	$(CXX) $(TEST_CXX_FLAGS) $(filter-out %.h,$^) -Iinclude -I$(FILTERS_DIR) -I apps/support -I src/runtime -lpthread $(STATIC_TEST_LIBS) $(BLAS_LDFLAGS) -o $@

