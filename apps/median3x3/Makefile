include ../support/Makefile.inc

all: $(BIN)/$(HL_TARGET)/filter

test: $(BIN)/$(HL_TARGET)/out.png

APP_NAME=median3x3

HVX_TARGET=hexagon-32-noos-no_bounds_query-no_asserts-hvx_128-hvx_v66
ARM_TARGET=arm-64-osx-no_runtime-arm_dot_prod
X86_TARGET=x86-64-linux-avx-avx2-fma-sse41

$(GENERATOR_BIN)/$(APP_NAME).generator: $(APP_NAME)_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(GENERATOR_BIN)/$(APP_NAME)_rake.generator: $(APP_NAME)_generator.cpp $(RAKE_GENERATOR_DEPS)
	@mkdir -p $(@D)
	export HL_RAKE_BENCHMARK_NAME=rake && \
	export HL_ENABLE_RAKE=1 && \
	$(CXX) $(RAKE_CXXFLAGS) -g -DOLD_SYNTAX $(filter %.cpp,$^) -o $@ $(RAKE_LIBHALIDE_LDFLAGS)

$(BIN)/%/$(APP_NAME)_llvm.a: $(GENERATOR_BIN)/$(APP_NAME).generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=0 && \
	export HL_ENABLE_RAKE_RULES=0 && \
	export HL_DISABLE_INTRINISICS=1 && \
	export HL_DISABLE_HALIDE_LOWERING=1 && \
	/usr/bin/time  $< -g $(APP_NAME) -f $(APP_NAME)_llvm -e stmt,assembly,static_library,c_header,llvm_assembly -o $(BIN)/$* target=$*

$(BIN)/%/$(APP_NAME)_halide.a: $(GENERATOR_BIN)/$(APP_NAME).generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=0 && \
	export HL_ENABLE_RAKE_RULES=0 && \
	export HL_DISABLE_INTRINISICS=0 && \
	export HL_DISABLE_HALIDE_LOWERING=0 && \
	/usr/bin/time  $< -g $(APP_NAME) -f $(APP_NAME)_halide -e stmt,assembly,static_library,c_header,llvm_assembly -o $(BIN)/$* target=$*

$(BIN)/%/$(APP_NAME)_pitchfork.a: $(GENERATOR_BIN)/$(APP_NAME).generator
	@mkdir -p $(@D)
	export HL_ENABLE_RAKE=0 && \
	export HL_ENABLE_RAKE_RULES=1 && \
	export HL_DISABLE_INTRINISICS=0 && \
	export HL_DISABLE_HALIDE_LOWERING=0 && \
	/usr/bin/time  $< -g $(APP_NAME) -f $(APP_NAME)_pitchfork -e stmt,assembly,static_library,c_header,llvm_assembly -o $(BIN)/$* target=$*

$(BIN)/%/$(APP_NAME)_rake.a: $(GENERATOR_BIN)/$(APP_NAME)_rake.generator
	@mkdir -p $(@D)
	export HL_RAKE_BENCHMARK_NAME=rake && \
	export HL_ENABLE_RAKE=1 && \
	export HL_ENABLE_RAKE_RULES=1 && \
	export HL_DISABLE_INTRINISICS=0 && \
	export HL_DISABLE_HALIDE_LOWERING=0 && \
	/usr/bin/time  $< -g $(APP_NAME) -f $(APP_NAME)_rake -e stmt,assembly,static_library,c_header,llvm_assembly -o $(BIN)/$* target=$*

$(BIN)/%/runtime.a: $(GENERATOR_BIN)/$(APP_NAME).generator
	@mkdir -p $(@D)
	$< -r runtime -o $(BIN)/$* target=$*

$(BIN)/%/filter: filter.cpp $(BIN)/%/$(APP_NAME)_rake.a $(BIN)/%/$(APP_NAME)_halide.a $(BIN)/%/$(APP_NAME)_pitchfork.a $(BIN)/%/$(APP_NAME)_llvm.a $(BIN)/%/runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/filter
	$< ../images/gray.png 100 $(BIN)/$*/out.png

clean:
	rm -rf $(BIN)


HEXAGON_SIM ?= /home/aroot/Qualcomm/Hexagon_SDK/3.5.4/tools/HEXAGON_Tools/8.3.07/Tools/bin/hexagon-sim
HEXAGON_CLANG ?= /home/aroot/Qualcomm/Hexagon_SDK/3.5.4/tools/HEXAGON_Tools/8.3.07/Tools/bin/hexagon-clang++
HALIDE_DIR ?= /home/aroot/Halide

# TODO: $(BIN)/$(HVX_TARGET)/$(APP_NAME)_rake.a
hvx: $(BIN)/$(HVX_TARGET)/$(APP_NAME)_halide.a $(BIN)/$(HVX_TARGET)/$(APP_NAME)_pitchfork.a $(BIN)/$(HVX_TARGET)/$(APP_NAME)_llvm.a $(BIN)/$(HVX_TARGET)/runtime.a
	$(HEXAGON_CLANG) \
        -D$(APP_NAME) \
        --std=c++17 -mhvx -mv66 -mhvx-length=128B \
        -I $(HALIDE_DIR)/include -I $(BIN)/$(HVX_TARGET)/ \
        ../shared_hvx/process.cpp \
		../shared_hvx/stubs.cpp \
		/home/aroot/rake-cache/$(APP_NAME)/$(APP_NAME)_hvx128.a \
		-I/home/aroot/rake-cache/$(APP_NAME)/ \
		$^ \
        /home/aroot/Halide/src/runtime/hexagon_remote/bin/v65/libsim_qurt.a -lhexagon \
        -o $(BIN)/$(HVX_TARGET)/$(APP_NAME)_run.out

	$(HEXAGON_SIM) -mv66 --memfill 0x0 --simulated_returnval --nullptr=2 $(BIN)/$(HVX_TARGET)/$(APP_NAME)_run.out -- 1920 1080 ../shared_hvx/test_vectors/football1920x1080.bin $(BIN)/$(HVX_TARGET)/out.bin

	$(HEXAGON_SIM) -mv66 --memfill 0x0 --simulated_returnval --timing --nullptr=2 $(BIN)/$(HVX_TARGET)/$(APP_NAME)_run.out -- 1920 1080 ../shared_hvx/test_vectors/football1920x1080.bin $(BIN)/$(HVX_TARGET)/out.bin


x86: $(BIN)/$(X86_TARGET)/out.png

arm: $(BIN)/$(ARM_TARGET)/out.png
