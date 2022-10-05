include ../support/Makefile.inc

all: $(BIN)/$(HL_TARGET)/filter

test: $(BIN)/$(HL_TARGET)/out.png

$(GENERATOR_BIN)/matmul.generator: matmul_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(GENERATOR_BIN)/matmul_rake.generator: matmul_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=1 && \
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/matmul_llvm.a: $(GENERATOR_BIN)/matmul.generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=0 && \
	export HL_ENABLE_RAKE_RULES=0 && \
	export HL_DISABLE_INTRINISICS=1 && \
	export HL_DISABLE_HALIDE_LOWERING=1 && \
	/usr/bin/time  $< -g matmul -f matmul_llvm -e stmt,assembly,static_library,c_header -o $(BIN)/$* target=$*-no_runtime-arm_dot_prod

$(BIN)/%/matmul_halide.a: $(GENERATOR_BIN)/matmul.generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=0 && \
	export HL_ENABLE_RAKE_RULES=0 && \
	export HL_DISABLE_INTRINISICS=0 && \
	export HL_DISABLE_HALIDE_LOWERING=0 && \
	/usr/bin/time  $< -g matmul -f matmul_halide -e stmt,assembly,static_library,c_header -o $(BIN)/$* target=$*-no_runtime-arm_dot_prod

$(BIN)/%/matmul_pitchfork.a: $(GENERATOR_BIN)/matmul.generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=0 && \
	export HL_ENABLE_RAKE_RULES=1 && \
	export HL_DISABLE_INTRINISICS=0 && \
	export HL_DISABLE_HALIDE_LOWERING=0 && \
	/usr/bin/time  $< -g matmul -f matmul_pitchfork -e stmt,assembly,static_library,c_header -o $(BIN)/$* target=$*-no_runtime-arm_dot_prod

$(BIN)/%/matmul_rake.a: $(GENERATOR_BIN)/matmul_rake.generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=1 && \
	export HL_ENABLE_RAKE_RULES=1 && \
	export HL_DISABLE_INTRINISICS=0 && \
	export HL_DISABLE_HALIDE_LOWERING=0 && \
	/usr/bin/time  $< -g matmul -f matmul_rake -e stmt,assembly,static_library,c_header -o $(BIN)/$* target=$*-no_runtime-arm_dot_prod

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/matmul.generator
	@mkdir -p $(@D)
	$< -r runtime -o $(BIN)/$* target=$*

$(BIN)/%/filter: filter.cpp $(BIN)/%/matmul_rake.a $(BIN)/%/matmul_halide.a $(BIN)/%/matmul_pitchfork.a $(BIN)/%/matmul_llvm.a $(BIN)/%/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/filter
	HL_NUM_THREADS=1 $< 256 256 256 100

clean:
	rm -rf $(BIN)
