include ../support/Makefile.inc

bin/libgradient_autoscheduler.so: GradientAutoscheduler.cpp
	@mkdir -p $(@D)
	$(CXX) -shared $(USE_EXPORT_DYNAMIC) -fPIC $(CXXFLAGS) -g $(OPTIMIZE) GradientAutoscheduler.cpp -o $@ $(HALIDE_SYSTEM_LIBS)

# Simple jit-based test
bin/test: test.cpp bin/libgradient_autoscheduler.so
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) test.cpp -o $@ $(LDFLAGS) $(LIB_HALIDE) $(HALIDE_SYSTEM_LIBS)

test: bin/test
	LD_LIBRARY_PATH=bin $<

clean:
	rm -rf bin