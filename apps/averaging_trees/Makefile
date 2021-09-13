include ../support/Makefile.inc

bin/enumerate: enumerate.cpp
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LIBHALIDE_LDFLAGS)
