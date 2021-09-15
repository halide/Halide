include ../support/Makefile.inc

bin/%: %.cpp
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LIBHALIDE_LDFLAGS)
