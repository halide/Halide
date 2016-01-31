include ../support/Makefile.inc

CXXFLAGS += -g -Wall

.PHONY: clean

unsharp: unsharp.cpp
	$(CXX) $(CXXFLAGS) unsharp.cpp $(LIB_HALIDE) -o unsharp -lpthread -ldl -lz \
	$(PNGFLAGS) $(LDFLAGS) $(LLVM_SHARED_LIBS)

clean:
	rm -f unsharp unsharp.h out.png
