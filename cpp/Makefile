CXX = g++
CXX_FLAGS = `llvm-config --cflags`
LLVM_DIR = ../llvm
LIBS = -L ../llvm/Release+Asserts/lib `llvm-config --libs all`

OBJECTS = CodeGen.o CodeGen_X86.o IR.o IRMutator.o IRPrinter.o IRVisitor.o CodeGen_C.o Substitute.o
INITIAL_MODULES = architecture.x86.initmod.o

all: libHalide.a

libHalide.a: $(OBJECTS) $(INITIAL_MODULES)
	ld -r -o Halide.o $(OBJECTS) $(INITIAL_MODULES) $(LIBS)
	rm -f libHalide.a
	ar q libHalide.a Halide.o
	ranlib libHalide.a

%.o: %.c
	$(CXX) -c $< -o $@

%.o: %.cpp %.h
	$(CXX) $(CXX_FLAGS) -c $< -o $@	

test_all: test_all.cpp libHalide.a
	$(CXX) $(CXX_FLAGS) -g test_all.cpp -L. -lHalide -o test_all -ldl -lpthread

clean:
	rm -f libHalide.a
	rm -f $(OBJECTS)
	rm -f test_all