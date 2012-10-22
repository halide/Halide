all: cHalide_wrap.cxx #halide.pyx #environ_fix

cHalide_wrap.cxx: cHalide.i ../cpp_bindings/*.cpp ../cpp_bindings/*.h ../src/*.ml
	cd ../cpp_bindings; make
	swig -c++ -python -I../cpp_bindings cHalide.i
	CXX=g++ CC=g++ python setup.py build_ext --inplace
#	python setup.py build_ext --inplace

#environ_fix: environ_fix.cpp
#	g++ environ_fix.cpp -c
#	ar rs environ_fix.a environ_fix.o
	
clean:
	rm -rf build
	rm halide.so
	rm _cHalide.so
	rm apollo*.jpg
