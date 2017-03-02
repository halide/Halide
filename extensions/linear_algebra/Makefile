EXT_NAME = HalideLinAlg

SOURCE_FILES = \
	Matrix.cpp

HEADER_FILES = \
	Matrix.h

EIGEN_DIR ?= /usr/include/eigen3
ifdef EIGEN_DIR
WITH_EIGEN = 1
EIGEN_CXX_FLAGS = -DWITH_EIGEN -I$(EIGEN_DIR)
endif

BLAS_DIR ?= /usr
ifdef BLAS_DIR
WITH_BLAS = 1
BLAS_CXX_FLAGS = -DWITH_BLAS -I$(BLAS_DIR)/include
BLAS_LDFLAGS = -L$(BLAS_DIR)/lib -lblas
endif

include ../support/Makefile.inc

TEST_CXX_FLAGS += $(EIGEN_CXX_FLAGS) $(BLAS_CXX_FLAGS)
FILTERS_DIR = $(BUILD_DIR)/filters

# "matrix_multiply_class" is produced by using matrix_multiply with different generator args.
$(FILTERS_DIR)/matrix_multiply_class.o $(FILTERS_DIR)/matrix_multiply_class.h: $(FILTERS_DIR)/matrix_multiply.generator
	@-mkdir -p tmp
	cd tmp; $(LD_PATH_SETUP) ../$< -o ../$(FILTERS_DIR) -f matrix_multiply_class -e stmt,assembly \
	target=$(HL_TARGET)-no_asserts-no_bounds_query algorithm=class_multiply

# "matrix_multiply_explicit" is produced by using matrix_multiply with different generator args.
$(FILTERS_DIR)/matrix_multiply_explicit.o $(FILTERS_DIR)/matrix_multiply_explicit.h: $(FILTERS_DIR)/matrix_multiply.generator
	@-mkdir -p tmp
	cd tmp; $(LD_PATH_SETUP) ../$< -o ../$(FILTERS_DIR) -f matrix_multiply_explicit -e stmt,assembly \
	target=$(HL_TARGET)-no_asserts-no_bounds_query algorithm=explicit_multiply

AOTTEST_INCLUDES = \
	-I$(SRCTOP)/include \
	-I$(FILTERS_DIR) \
	-I$(SRCTOP)/test \
	-I$(SRCTOP)/apps/support \
	-I$(SRCTOP)/src/runtime

# matrix_multiply test using the internal matrix class.
$(BIN_DIR)/generator_aot_matrix_multiply: test/generator/matrix_multiply_aottest.cpp $(FILTERS_DIR)/matrix_multiply_class.o $(FILTERS_DIR)/matrix_multiply_explicit.o
	$(CXX) -O3 -DNDEBUG $(TEST_CXX_FLAGS) $(filter-out %.h,$^) $(AOTTEST_INCLUDES) -lpthread $(STATIC_TEST_LIBS) $(BLAS_LDFLAGS) -o $@

