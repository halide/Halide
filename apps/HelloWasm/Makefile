HALIDE_DIR ?= /home/abadams/projects/Halide_wasm_demo/distrib

all: bin/index.js bin/index_simd.js bin/index_threads.js bin/index_simd_threads.js

bin/reaction_diffusion_generator: reaction_diffusion_generator.cpp
	@mkdir -p $(@D)
	$(CXX) $< -I $(HALIDE_DIR)/include -L $(HALIDE_DIR)/lib -lHalide  $(HALIDE_DIR)/tools/GenGen.cpp  -o $@ -Wl,-rpath -Wl,$(HALIDE_DIR)/lib

bin/wasm/reaction_diffusion_%.a: bin/reaction_diffusion_generator
	@mkdir -p $(@D)
	$< -g reaction_diffusion_$* -o $(@D) target=wasm-32-wasmrt-no_runtime threads=false

bin/wasm/runtime.a: bin/reaction_diffusion_generator
	@mkdir -p $(@D)
	$< -r runtime -o $(@D) target=wasm-32-wasmrt

bin/wasm_simd/reaction_diffusion_%.a: bin/reaction_diffusion_generator
	@mkdir -p $(@D)
	$< -g reaction_diffusion_$* -o $(@D) target=wasm-32-wasmrt-wasm_simd128-no_runtime threads=false

bin/wasm_simd/runtime.a: bin/reaction_diffusion_generator
	@mkdir -p $(@D)
	$< -r runtime -o $(@D) target=wasm-32-wasmrt-wasm_simd128

bin/wasm_threads/reaction_diffusion_%.a: bin/reaction_diffusion_generator
	@mkdir -p $(@D)
	$< -g reaction_diffusion_$* -o $(@D) target=wasm-32-wasmrt-wasm_threads-no_runtime threads=true

bin/wasm_threads/runtime.a: bin/reaction_diffusion_generator
	@mkdir -p $(@D)
	$< -r runtime -o $(@D) target=wasm-32-wasmrt-wasm_threads

bin/wasm_simd_threads/reaction_diffusion_%.a: bin/reaction_diffusion_generator
	@mkdir -p $(@D)
	$< -g reaction_diffusion_$* -o $(@D) target=wasm-32-wasmrt-wasm_simd128-wasm_threads-no_runtime threads=true

bin/wasm_simd_threads/runtime.a: bin/reaction_diffusion_generator
	@mkdir -p $(@D)
	$< -r runtime -o $(@D) target=wasm-32-wasmrt-wasm_simd128-wasm_threads

bin/native/reaction_diffusion_%.a: bin/reaction_diffusion_generator
	@mkdir -p $(@D)
	$< -g reaction_diffusion_$* -o $(@D) -e registration,static_library target=host-no_runtime threads=true

bin/native/runtime.a: bin/reaction_diffusion_generator
	@mkdir -p $(@D)
	$< -r runtime -o $(@D) target=host


bin/index.js: core.cpp bin/wasm/reaction_diffusion_init.a bin/wasm/reaction_diffusion_update.a bin/wasm/reaction_diffusion_render.a bin/wasm/runtime.a
	emcc $^ -s WASM=1 -s USE_SDL=2 -s TOTAL_MEMORY=512MB -O3 -o $@ -I $(HALIDE_DIR)/include/ -I bin/wasm 

bin/index_simd.js: core.cpp bin/wasm_simd/reaction_diffusion_init.a bin/wasm_simd/reaction_diffusion_update.a bin/wasm_simd/reaction_diffusion_render.a bin/wasm_simd/runtime.a
	emcc $^ -s WASM=1 -s USE_SDL=2 -s TOTAL_MEMORY=512MB -O3 -o $@ -I $(HALIDE_DIR)/include/ -I bin/wasm_simd  

bin/index_threads.js: core.cpp bin/wasm_threads/reaction_diffusion_init.a bin/wasm_threads/reaction_diffusion_update.a bin/wasm_threads/reaction_diffusion_render.a bin/wasm_threads/runtime.a
	emcc $^ -s WASM=1 -s USE_SDL=2 -s TOTAL_MEMORY=512MB -O3 -o $@ -I $(HALIDE_DIR)/include/ -I bin/wasm_threads -pthread -matomics -mbulk-memory

bin/index_simd_threads.js: core.cpp bin/wasm_simd_threads/reaction_diffusion_init.a bin/wasm_simd_threads/reaction_diffusion_update.a bin/wasm_simd_threads/reaction_diffusion_render.a bin/wasm_simd_threads/runtime.a
	emcc $^ -s WASM=1 -s USE_SDL=2 -s TOTAL_MEMORY=512MB -O3 -o $@ -I $(HALIDE_DIR)/include/ -I bin/wasm_simd_threads -pthread -matomics -mbulk-memory

clean:
	rm -rf bin

bin/native/run: bin/native/reaction_diffusion_init.a bin/native/reaction_diffusion_update.a bin/native/reaction_diffusion_render.a bin/native/runtime.a 
	$(CXX) $(HALIDE_DIR)/tools/RunGenMain.cpp bin/native/reaction_diffusion_*.registration.cpp $^ -o $@ -I $(HALIDE_DIR)/include -lpthread -ldl -lpng -ljpeg

benchmark_native: bin/native/run
	bin/native/run --benchmarks=all --benchmark_min_time=1 --name=reaction_diffusion_init --output_extents=[1024,1024]
	bin/native/run --benchmarks=all --benchmark_min_time=1 --name=reaction_diffusion_update --output_extents=[1024,1024] frame=0 mouse_x=0 mouse_y=0 state=random:0:[1024,1024,3]
	bin/native/run --benchmarks=all --benchmark_min_time=1 --name=reaction_diffusion_render --output_extents=[1024,1024]  state=random:0:[1024,1024,3]
