include ../support/Makefile.inc

all: $(BIN)/$(HL_TARGET)/filter

test: $(BIN)/$(HL_TARGET)/out.png

$(GENERATOR_BIN)/fully_connected.generator: fully_connected_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(GENERATOR_BIN)/fully_connected_rake.generator: fully_connected_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=1 && \
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/fully_connected_llvm.a: $(GENERATOR_BIN)/fully_connected.generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=0 && \
	export HL_ENABLE_RAKE_RULES=0 && \
	export HL_DISABLE_INTRINISICS=1 && \
	export HL_DISABLE_HALIDE_LOWERING=1 && \
	/usr/bin/time  $< -g fully_connected -f fully_connected_llvm -e stmt,assembly,static_library,c_header -o $(BIN)/$* target=$*-no_runtime-arm_dot_prod

$(BIN)/%/fully_connected_halide.a: $(GENERATOR_BIN)/fully_connected.generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=0 && \
	export HL_ENABLE_RAKE_RULES=0 && \
	export HL_DISABLE_INTRINISICS=0 && \
	export HL_DISABLE_HALIDE_LOWERING=0 && \
	/usr/bin/time  $< -g fully_connected -f fully_connected_halide -e stmt,assembly,static_library,c_header -o $(BIN)/$* target=$*-no_runtime-arm_dot_prod

$(BIN)/%/fully_connected_pitchfork.a: $(GENERATOR_BIN)/fully_connected.generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=0 && \
	export HL_ENABLE_RAKE_RULES=1 && \
	export HL_DISABLE_INTRINISICS=0 && \
	export HL_DISABLE_HALIDE_LOWERING=0 && \
	/usr/bin/time  $< -g fully_connected -f fully_connected_pitchfork -e stmt,assembly,static_library,c_header -o $(BIN)/$* target=$*-no_runtime-arm_dot_prod

$(BIN)/%/fully_connected_rake.a: $(GENERATOR_BIN)/fully_connected_rake.generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=1 && \
	export HL_ENABLE_RAKE_RULES=1 && \
	export HL_DISABLE_INTRINISICS=0 && \
	export HL_DISABLE_HALIDE_LOWERING=0 && \
	/usr/bin/time  $< -g fully_connected -f fully_connected_rake -e stmt,assembly,static_library,c_header -o $(BIN)/$* target=$*-no_runtime-arm_dot_prod

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/fully_connected.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(BIN)/$* target=$*

$(BIN)/%/filter: filter.cpp $(BIN)/%/fully_connected_rake.a $(BIN)/%/fully_connected_halide.a $(BIN)/%/fully_connected_pitchfork.a $(BIN)/%/fully_connected_llvm.a $(BIN)/%/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/filter
	HL_NUM_THREADS=1 $< 256 256 256 100

clean:
	rm -rf $(BIN)
