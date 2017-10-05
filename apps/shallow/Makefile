include ../support/Makefile.inc

BIN ?= bin

all: $(BIN)/process

$(BIN)/shallow_lower_bound_exec: shallow_lower_bound_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(BIN)/shallow_lower_bound.a: $(BIN)/shallow_lower_bound_exec
	@mkdir -p $(@D)
	$^ -g shallow_lower_bound -o $(BIN) -f shallow_lower_bound target=$(HL_TARGET) auto_schedule=false

$(BIN)/shallow_lower_bound_auto_schedule.a: $(BIN)/shallow_lower_bound_exec
	@mkdir -p $(@D)
	$^ -g shallow_lower_bound -o $(BIN) -f shallow_lower_bound_auto_schedule target=$(HL_TARGET)-no_runtime auto_schedule=true

$(BIN)/process: process.cpp $(BIN)/shallow_lower_bound.a $(BIN)/shallow_lower_bound_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(OPENGL_LDFLAGS)

run: $(BIN)/process
	@mkdir -p $(@D)
	$(BIN)/process

clean:
	rm -rf $(BIN)
