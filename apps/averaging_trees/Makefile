include ../support/Makefile.inc

bin/%: %.cpp dag.h
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -march=native $< -o $@ $(LIBHALIDE_LDFLAGS)

bin/try_median_network: try_median_network.cpp sorting_network.cpp 
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LIBHALIDE_LDFLAGS)

bin/bilinear_upsample_generator: bilinear_upsample_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

bin/bilinear_upsample_averaging.a: bin/bilinear_upsample_generator
	@mkdir -p $(@D)
	$^ -g bilinear_upsample_averaging -e $(GENERATOR_OUTPUTS) -o $(@D) -f bilinear_upsample_averaging target=host-disable_llvm_loop_opt

bin/bilinear_upsample_round_up.a: bin/bilinear_upsample_generator
	@mkdir -p $(@D)
	$^ -g bilinear_upsample_round_up -e $(GENERATOR_OUTPUTS) -o $(@D) -f bilinear_upsample_round_up target=host

bin/benchmark: benchmark.cpp bin/bilinear_upsample_averaging.a bin/bilinear_upsample_round_up.a
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -march=native $^ -o $@ $(LDFLAGS)
