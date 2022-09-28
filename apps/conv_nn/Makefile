include ../support/Makefile.inc

all: $(BIN)/$(HL_TARGET)/filter

test: $(BIN)/$(HL_TARGET)/out.png

$(GENERATOR_BIN)/conv_nn.generator: conv_nn_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(GENERATOR_BIN)/conv_nn_rake.generator: conv_nn_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=1 && \
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/conv_nn_llvm.a: $(GENERATOR_BIN)/conv_nn.generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=0 && \
	export HL_ENABLE_RAKE_RULES=0 && \
	export HL_DISABLE_INTRINISICS=1 && \
	export HL_DISABLE_ARM_LOWERING=1 && \
	$< -g conv_nn -f conv_nn_llvm -e stmt,assembly,static_library,c_header -o $(BIN)/$* target=$*-no_runtime-arm_dot_prod

$(BIN)/%/conv_nn_halide.a: $(GENERATOR_BIN)/conv_nn.generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=0 && \
	export HL_ENABLE_RAKE_RULES=0 && \
	export HL_DISABLE_INTRINISICS=0 && \
	export HL_DISABLE_ARM_LOWERING=0 && \
	$< -g conv_nn -f conv_nn_halide -e stmt,assembly,static_library,c_header -o $(BIN)/$* target=$*-no_runtime-arm_dot_prod

$(BIN)/%/conv_nn_pitchfork.a: $(GENERATOR_BIN)/conv_nn.generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=0 && \
	export HL_ENABLE_RAKE_RULES=1 && \
	export HL_DISABLE_INTRINISICS=0 && \
	export HL_DISABLE_ARM_LOWERING=0 && \
	$< -g conv_nn -f conv_nn_pitchfork -e stmt,assembly,static_library,c_header -o $(BIN)/$* target=$*-no_runtime-arm_dot_prod

$(BIN)/%/conv_nn_rake.a: $(GENERATOR_BIN)/conv_nn_rake.generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=1 && \
	export HL_ENABLE_RAKE_RULES=1 && \
	export HL_DISABLE_INTRINISICS=0 && \
	export HL_DISABLE_ARM_LOWERING=0 && \
	$< -g conv_nn -f conv_nn_rake -e stmt,assembly,static_library,c_header -o $(BIN)/$* target=$*-no_runtime-arm_dot_prod

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/conv_nn.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(BIN)/$* target=$*

$(BIN)/%/filter: filter.cpp $(BIN)/%/conv_nn_rake.a $(BIN)/%/conv_nn_halide.a $(BIN)/%/conv_nn_pitchfork.a $(BIN)/%/conv_nn_llvm.a $(BIN)/%/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/filter
	HL_NUM_THREADS=1 $< 0 0 0 0 100

clean:
	rm -rf $(BIN)
