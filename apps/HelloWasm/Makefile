include ../support/Makefile.inc

# The emscripten compiler
EMCC ?= emcc
EMCC_FLAGS ?= -std=c++17 -s WASM=1 -s USE_SDL=2 -s TOTAL_MEMORY=512MB -O3 -I $(HALIDE_DISTRIB_PATH)/include
EMCC_THREADS_FLAGS ?= $(EMCC_FLAGS) -pthread -matomics

# the output dir for the .js products must be fixed, because that's what index.html looks for

all: js/index.js js/index_simd.js js/index_threads.js js/index_simd_threads.js

$(GENERATOR_BIN)/reaction_diffusion_generator: reaction_diffusion_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/wasm/reaction_diffusion_%.a: $(GENERATOR_BIN)/reaction_diffusion_generator
	@mkdir -p $(@D)
	$< -g reaction_diffusion_$* -e $(GENERATOR_OUTPUTS) -o $(@D) target=wasm-32-wasmrt-no_runtime threads=false

$(BIN)/wasm/runtime.a: $(GENERATOR_BIN)/reaction_diffusion_generator
	@mkdir -p $(@D)
	$< -r runtime -o $(@D) target=wasm-32-wasmrt

$(BIN)/wasm_simd/reaction_diffusion_%.a: $(GENERATOR_BIN)/reaction_diffusion_generator
	@mkdir -p $(@D)
	$< -g reaction_diffusion_$* -e $(GENERATOR_OUTPUTS) -o $(@D) target=wasm-32-wasmrt-wasm_simd128-no_runtime threads=false

$(BIN)/wasm_simd/runtime.a: $(GENERATOR_BIN)/reaction_diffusion_generator
	@mkdir -p $(@D)
	$< -r runtime -o $(@D) target=wasm-32-wasmrt-wasm_simd128

$(BIN)/wasm_threads/reaction_diffusion_%.a: $(GENERATOR_BIN)/reaction_diffusion_generator
	@mkdir -p $(@D)
	$< -g reaction_diffusion_$* -o $(@D) target=wasm-32-wasmrt-wasm_threads-no_runtime threads=true

$(BIN)/wasm_threads/runtime.a: $(GENERATOR_BIN)/reaction_diffusion_generator
	@mkdir -p $(@D)
	$< -r runtime -o $(@D) target=wasm-32-wasmrt-wasm_threads

$(BIN)/wasm_simd_threads/reaction_diffusion_%.a: $(GENERATOR_BIN)/reaction_diffusion_generator
	@mkdir -p $(@D)
	$< -g reaction_diffusion_$* -o $(@D) target=wasm-32-wasmrt-wasm_simd128-wasm_threads-no_runtime threads=true

$(BIN)/wasm_simd_threads/runtime.a: $(GENERATOR_BIN)/reaction_diffusion_generator
	@mkdir -p $(@D)
	$< -r runtime -o $(@D) target=wasm-32-wasmrt-wasm_simd128-wasm_threads

$(BIN)/$(HL_TARGET)/reaction_diffusion_%.a: $(GENERATOR_BIN)/reaction_diffusion_generator
	@mkdir -p $(@D)
	$< -g reaction_diffusion_$* -o $(@D) -e registration,static_library target=$(HL_TARGET)-no_runtime threads=true

$(BIN)/$(HL_TARGET)/runtime.a: $(GENERATOR_BIN)/reaction_diffusion_generator
	@mkdir -p $(@D)
	$< -r runtime -o $(@D) target=$(HL_TARGET)

js/index.js: core.cpp $(BIN)/wasm/reaction_diffusion_init.a $(BIN)/wasm/reaction_diffusion_update.a $(BIN)/wasm/reaction_diffusion_render.a $(BIN)/wasm/runtime.a
	@mkdir -p $(@D)
	$(EMCC) $^ $(EMCC_FLAGS) -I $(BIN)/wasm -o $@

js/index_simd.js: core.cpp $(BIN)/wasm_simd/reaction_diffusion_init.a $(BIN)/wasm_simd/reaction_diffusion_update.a $(BIN)/wasm_simd/reaction_diffusion_render.a $(BIN)/wasm_simd/runtime.a
	@mkdir -p $(@D)
	$(EMCC) $^ $(EMCC_FLAGS) -I $(BIN)/wasm_simd -o $@

js/index_threads.js: core.cpp $(BIN)/wasm_threads/reaction_diffusion_init.a $(BIN)/wasm_threads/reaction_diffusion_update.a $(BIN)/wasm_threads/reaction_diffusion_render.a $(BIN)/wasm_threads/runtime.a
	@mkdir -p $(@D)
	$(EMCC) $^ $(EMCC_THREADS_FLAGS) -I $(BIN)/wasm_threads -o $@

js/index_simd_threads.js: core.cpp $(BIN)/wasm_simd_threads/reaction_diffusion_init.a $(BIN)/wasm_simd_threads/reaction_diffusion_update.a $(BIN)/wasm_simd_threads/reaction_diffusion_render.a $(BIN)/wasm_simd_threads/runtime.a
	@mkdir -p $(@D)
	$(EMCC) $^ $(EMCC_THREADS_FLAGS) -I $(BIN)/wasm_simd_threads -o $@

clean:
	rm -rf js
	rm -rf $(BIN)

$(BIN)/$(HL_TARGET)/run: $(BIN)/$(HL_TARGET)/reaction_diffusion_init.a $(BIN)/$(HL_TARGET)/reaction_diffusion_update.a $(BIN)/$(HL_TARGET)/reaction_diffusion_render.a $(BIN)/$(HL_TARGET)/runtime.a
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(HALIDE_DISTRIB_PATH)/tools/RunGenMain.cpp $(BIN)/$(HL_TARGET)/reaction_diffusion_*.registration.cpp $^ -o $@ -I $(HALIDE_DISTRIB_PATH)/include $(IMAGE_IO_FLAGS) $(LDFLAGS)

benchmark_native: $(BIN)/$(HL_TARGET)/run
	$(BIN)/$(HL_TARGET)/run --benchmarks=all --benchmark_min_time=1 --name=reaction_diffusion_init --output_extents=[1024,1024] --parsable_output
	$(BIN)/$(HL_TARGET)/run --benchmarks=all --benchmark_min_time=1 --name=reaction_diffusion_update --output_extents=[1024,1024] frame=0 mouse_x=0 mouse_y=0 state=random:0:[1024,1024,3] --parsable_output
	$(BIN)/$(HL_TARGET)/run --benchmarks=all --benchmark_min_time=1 --name=reaction_diffusion_render --output_extents=[1024,1024]  state=random:0:[1024,1024,3] --parsable_output
