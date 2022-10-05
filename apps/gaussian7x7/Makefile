include ../support/Makefile.inc

all: $(BIN)/$(HL_TARGET)/filter

test: $(BIN)/$(HL_TARGET)/out.png

$(GENERATOR_BIN)/gaussian7x7.generator: gaussian7x7_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(GENERATOR_BIN)/gaussian7x7_rake.generator: gaussian7x7_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=1 && \
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/gaussian7x7_llvm.a: $(GENERATOR_BIN)/gaussian7x7.generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=0 && \
	export HL_ENABLE_RAKE_RULES=0 && \
	export HL_DISABLE_INTRINISICS=1 && \
	export HL_DISABLE_HALIDE_LOWERING=1 && \
	/usr/bin/time  $< -g gaussian7x7 -f gaussian7x7_llvm -e stmt,assembly,static_library,c_header -o $(BIN)/$* target=$*-no_runtime-arm_dot_prod

$(BIN)/%/gaussian7x7_halide.a: $(GENERATOR_BIN)/gaussian7x7.generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=0 && \
	export HL_ENABLE_RAKE_RULES=0 && \
	export HL_DISABLE_INTRINISICS=0 && \
	export HL_DISABLE_HALIDE_LOWERING=0 && \
	/usr/bin/time  $< -g gaussian7x7 -f gaussian7x7_halide -e stmt,assembly,static_library,c_header -o $(BIN)/$* target=$*-no_runtime-arm_dot_prod

$(BIN)/%/gaussian7x7_pitchfork.a: $(GENERATOR_BIN)/gaussian7x7.generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=0 && \
	export HL_ENABLE_RAKE_RULES=1 && \
	export HL_DISABLE_INTRINISICS=0 && \
	export HL_DISABLE_HALIDE_LOWERING=0 && \
	/usr/bin/time  $< -g gaussian7x7 -f gaussian7x7_pitchfork -e stmt,assembly,static_library,c_header -o $(BIN)/$* target=$*-no_runtime-arm_dot_prod

$(BIN)/%/gaussian7x7_rake.a: $(GENERATOR_BIN)/gaussian7x7_rake.generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=1 && \
	export HL_ENABLE_RAKE_RULES=1 && \
	export HL_DISABLE_INTRINISICS=0 && \
	export HL_DISABLE_HALIDE_LOWERING=0 && \
	/usr/bin/time  $< -g gaussian7x7 -f gaussian7x7_rake -e stmt,assembly,static_library,c_header -o $(BIN)/$* target=$*-no_runtime-arm_dot_prod

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/gaussian7x7.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(BIN)/$* target=$*

$(BIN)/%/filter: filter.cpp $(BIN)/%/gaussian7x7_rake.a $(BIN)/%/gaussian7x7_halide.a $(BIN)/%/gaussian7x7_pitchfork.a $(BIN)/%/gaussian7x7_llvm.a $(BIN)/%/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/filter
	$< ../images/gray.png 100 $(BIN)/$*/out.png

clean:
	rm -rf $(BIN)
