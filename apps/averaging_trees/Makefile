include ../support/Makefile.inc

bin/%: %.cpp dag.h
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -march=native $< -o $@ $(LIBHALIDE_LDFLAGS)

bin/try_median_network: try_median_network.cpp sorting_network.cpp 
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LIBHALIDE_LDFLAGS)
