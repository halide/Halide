include ../support/Makefile.inc

all: $(BIN)/$(HL_TARGET)/filter

test: $(BIN)/$(HL_TARGET)/out.png

$(GENERATOR_BIN)/average_pool.generator: average_pool_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(GENERATOR_BIN)/average_pool_rake.generator: average_pool_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=1 && \
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/average_pool_llvm.a: $(GENERATOR_BIN)/average_pool.generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=0 && \
	export HL_ENABLE_RAKE_RULES=0 && \
	export HL_DISABLE_INTRINISICS=1 && \
	export HL_DISABLE_HALIDE_LOWERING=1 && \
	$< -g average_pool -f average_pool_llvm -e stmt,assembly,static_library,c_header -o $(BIN)/$* target=$*-no_runtime-arm_dot_prod

$(BIN)/%/average_pool_halide.a: $(GENERATOR_BIN)/average_pool.generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=0 && \
	export HL_ENABLE_RAKE_RULES=0 && \
	export HL_DISABLE_INTRINISICS=0 && \
	export HL_DISABLE_HALIDE_LOWERING=0 && \
	$< -g average_pool -f average_pool_halide -e stmt,assembly,static_library,c_header -o $(BIN)/$* target=$*-no_runtime-arm_dot_prod

$(BIN)/%/average_pool_pitchfork.a: $(GENERATOR_BIN)/average_pool.generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=0 && \
	export HL_ENABLE_RAKE_RULES=1 && \
	export HL_DISABLE_INTRINISICS=0 && \
	export HL_DISABLE_HALIDE_LOWERING=0 && \
	$< -g average_pool -f average_pool_pitchfork -e stmt,assembly,static_library,c_header -o $(BIN)/$* target=$*-no_runtime-arm_dot_prod

$(BIN)/%/average_pool_rake.a: $(GENERATOR_BIN)/average_pool_rake.generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=1 && \
	export HL_ENABLE_RAKE_RULES=1 && \
	export HL_DISABLE_INTRINISICS=0 && \
	export HL_DISABLE_HALIDE_LOWERING=0 && \
	$< -g average_pool -f average_pool_rake -e stmt,assembly,static_library,c_header -o $(BIN)/$* target=$*-no_runtime-arm_dot_prod

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/average_pool.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(BIN)/$* target=$*

$(BIN)/%/filter: filter.cpp $(BIN)/%/average_pool_rake.a $(BIN)/%/average_pool_halide.a $(BIN)/%/average_pool_pitchfork.a $(BIN)/%/average_pool_llvm.a $(BIN)/%/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/filter
	$< 4 256 256 4 10

clean:
	rm -rf $(BIN)
