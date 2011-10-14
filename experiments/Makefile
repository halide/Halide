CXX=g++ -m64
CXXFLAGS=-Iobjs/ -O3 -Wall
ISPC=ispc
ISPCFLAGS=-O2 --arch=x86-64 --target=sse4

default: fused_convolution

.PHONY: dirs clean

dirs:
	/bin/mkdir -p objs/

clean:
	/bin/rm -rf objs *~ fused_convolution

fused_convolution: dirs  objs/fused_convolution.o objs/fused_convolution_ispc.o
	$(CXX) $(CXXFLAGS) -o $@ objs/fused_convolution.o objs/fused_convolution_ispc.o

objs/fused_convolution.o: fused_convolution.cpp objs/fused_convolution_ispc.h 
	$(CXX) $(CXXFLAGS) -c -o $@ $<

objs/%_ispc.h objs/%_ispc.o: %.ispc
	$(ISPC) $(ISPCFLAGS) $< -o objs/$*_ispc.o -h objs/$*_ispc.h
