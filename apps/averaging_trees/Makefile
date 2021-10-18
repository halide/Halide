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

bin/bilinear_upsample_%.a: bin/bilinear_upsample_generator
	@mkdir -p $(@D)
	$^ -g bilinear_upsample -e $(GENERATOR_OUTPUTS) -o $(@D) -f bilinear_upsample_$* method=$* target=host-disable_llvm_loop_opt input.type=uint8 output.type=uint8

bin/bilinear_upsample_float.a: bin/bilinear_upsample_generator
	@mkdir -p $(@D)
	$^ -g bilinear_upsample -e $(GENERATOR_OUTPUTS) -o $(@D) -f bilinear_upsample_float method=float target=host-disable_llvm_loop_opt input.type=float32 output.type=float32

bin/bilinear_upsample_float_dither.a: bin/bilinear_upsample_generator
	@mkdir -p $(@D)
	$^ -g bilinear_upsample -e $(GENERATOR_OUTPUTS) -o $(@D) -f bilinear_upsample_float_dither method=dither target=host-disable_llvm_loop_opt input.type=float32 output.type=uint8

bin/benchmark: benchmark.cpp bin/bilinear_upsample_averaging.a bin/bilinear_upsample_round_up.a bin/bilinear_upsample_round_to_even.a bin/bilinear_upsample_dither.a bin/bilinear_upsample_float.a bin/bilinear_upsample_float_dither.a
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -march=native $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS)
