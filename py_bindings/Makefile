all: cHalide_wrap.cxx #halide.pyx #environ_fix

cHalide_wrap.cxx: cHalide.i ../cpp_bindings/*.cpp ../cpp_bindings/*.h ../src/*.ml py_util.cpp environ_fix.cpp py_util.h environ_fix.h expand_types.h ../support/*.h
	cd ../cpp_bindings; make
	swig -c++ -python -I../cpp_bindings cHalide.i
	CXX=g++ CC=g++ python setup.py build_ext --inplace
#	python setup.py build_ext --inplace

#environ_fix: environ_fix.cpp
#	g++ environ_fix.cpp -c
#	ar rs environ_fix.a environ_fix.o
	
clean:
	rm -rf build
	rm -f cHalide_wrap.cxx
	rm -f halide.so
	rm -f _cHalide.so

test:
	python -c "import halide, autotune; halide.test(); autotune.test()"

