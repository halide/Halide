ifneq ($(shell env -i command -v g++-4.7),)
GXX ?= g++-4.7
endif
GXX ?= g++-4.6

HL_MODULES = daubechies_x.bc inverse_daubechies_x.bc haar_x.bc inverse_haar_x.bc

all: filter

wavelet: ../../cpp_bindings/ wavelet.cpp
	$(MAKE) -C ../../cpp_bindings libHalide.a
	$(GXX) wavelet.cpp -I ../../cpp_bindings/ ../../cpp_bindings/Halide.a  -std=c++0x -o wavelet  -lpthread -ldl

$(HL_MODULES): wavelet 
	./wavelet

%.o: %.bc
	cat $< | opt -O3 | llc -O3 -filetype=obj -o=$@

filter.o: filter.cpp $(HL_MODULES:%.bc=%.o)
	$(GXX) -std=c++0x -O3 -c filter.cpp

filter: filter.o $(HL_MODULES:%.bc=%.o)
	$(GXX) filter.o $(HL_MODULES:%.bc=%.o) -lpng -o filter -pthread

clean:
	rm -f wavelet filter filter.o $(HL_MODULES) $(HL_MODULES:%.bc=%.o) $(HL_MODULES:%.bc=%.h) *.lowered*