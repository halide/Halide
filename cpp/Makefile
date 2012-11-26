CXX = g++
CXX_FLAGS = `llvm-config --cflags`
LLVM_DIR = ../llvm
LIBS = -L ../llvm/Release+Asserts/lib `llvm-config --libs`

OBJECTS = CodeGen.o CodeGen_X86.o IR.o IRMutator.o IRPrinter.o IRVisitor.o

all: libHalide.a

libHalide.a: $(OBJECTS)
	ld -r -o Halide.o $(OBJECTS) $(LIBS)
	rm -f libHalide.a
	ar q libHalide.a Halide.o
	ranlib libHalide.a

%.o: %.cpp %.h
	$(CXX) $(CXX_FLAGS) -c $< -o $@	

test_all: test_all.cpp libHalide.a
	$(CXX) $(CXX_FLAGS) -g test_all.cpp -L. -lHalide -o test_all -ldl -lpthread

clean:
	rm -f libHalide.a
	rm -f $(OBJECTS)
	rm -f test_all