UNAME = $(shell uname)

ifeq ($(UNAME), Linux)
CCFLAGS=$(shell python3-config --cflags) -std=c++11 -fPIC
LDFLAGS=$(shell python3-config --ldflags) -lboost_python-py34 -lz
endif

ifeq ($(UNAME), Darwin)
# The /opt includes are in case this is a macports install of python and boost python
CCFLAGS=$(shell python-config --cflags) -I /opt/local/include -std=c++11
LDFLAGS=$(shell python-config --ldflags) -L /opt/local/lib -lboost_python3-mt -lz
endif

NUMPY_PATH=$(shell python3 -c "import numpy; print(numpy.__path__[0] + '/core/include')")

PY_SRCS=$(shell ls python/*.cpp)
PY_OBJS=$(PY_SRCS:python/%.cpp=build/py_%.o)
NUMPY_SRCS=$(shell ls numpy/*.cpp)
NUMPY_OBJS=$(NUMPY_SRCS:numpy/%.cpp=build/numpy_%.o)



build/py_%.o: python/%.cpp
	mkdir -p build
	$(CXX) $(CCFLAGS) -c $< -o $@

build/numpy_%.o: numpy/%.cpp
	mkdir -p build
	$(CXX) $(CCFLAGS) -I $(NUMPY_PATH) -c $< -o $@

build/halide.so: $(PY_SRCS) $(PY_OBJS) $(NUMPY_SRCS) $(NUMPY_OBJS)
	mkdir -p build
	$(CXX) $(PY_OBJS) $(NUMPY_OBJS) $(LDFLAGS) ../bin/libHalide.a -shared -o $@

clean:
	rm -rf build

test: build/halide.so
	./run_apps.sh && ./run_tutorial.sh

