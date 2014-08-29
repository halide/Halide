include ../support/Makefile.inc

all: test

boundary_test: ../../$(LIB_HALIDE) boundary_test.cpp
	$(MAKE) -C ../../ $(LIB_HALIDE)
	$(CXX) -g boundary_test.cpp -I ../../include -o boundary_test ../../$(LIB_HALIDE) -ldl -lpthread -lz $(LDFLAGS)

boundary_test.o: boundary_test
	./boundary_test 2

# g++ on OS X might actually be system clang without openmp
CXX_VERSION=$(shell $(CXX) --version)
ifeq (,$(findstring clang,$(CXX_VERSION)))
OPENMP_FLAGS=-fopenmp
else
OPENMP_FLAGS=
endif

test: boundary_test.o

clean:
	rm -f boundary_test.o boundary_test
