include ../support/Makefile.inc

BUILD_DIR = build

OPS = $(BIN)/$(HL_TARGET)/add.a \
	  $(BIN)/$(HL_TARGET)/add_double.a \
	  $(BIN)/$(HL_TARGET)/add_grad.a \
	  $(BIN)/$(HL_TARGET)/add_grad_double.a

# Check whether we have cuda installed, if add the CUDA ops as dependencies
ifeq ($(shell which nvcc),)
HAS_CUDA = 0
CUDA_OPS =
else
HAS_CUDA = 1
CUDA_OPS = $(BIN)/$(HL_TARGET)/add_cuda.a \
		   $(BIN)/$(HL_TARGET)/add_cuda_double.a 
endif


all: test

# Run the pytorch tests to verify the module is compiled correctly
test: .wrapper
	# @python test.py
	@PYTHONPATH=lib:${PYTHONPATH} python test.py

# Build the python pytorch extension that links against the Halide operators
# TODO: do not rebuild
.PHONY: wrapper
.wrapper: $(OPS) $(CUDA_OPS)
	@mkdir -p lib
	@HAS_CUDA=$(HAS_CUDA) HALIDE_DISTRIB_PATH=$(HALIDE_DISTRIB_PATH) BIN=$(BIN)/$(HL_TARGET) PYTHONPATH=lib:${PYTHONPATH} python setup.py install --install-lib lib
	@touch .wrapper

# Generate the CPU float version of the op
$(BIN)/%/add.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	@echo Producing CPU operator
	@$^ -g add input_a.type=float32 input_b.type=float32 output.type=float32 -f add -e static_library,h,pytorch_wrapper -o $(@D) target=$* auto_schedule=false

$(BIN)/%/add_grad.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	@echo Producing CPU gradient
	@$^ -g add_grad d_input_a.type=float32 d_input_b.type=float32 d_output.type=float32 -f add_grad -e static_library,h,pytorch_wrapper -o $(@D) target=$* auto_schedule=false

# Generate the CPU double version of the op
$(BIN)/%/add_double.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	@echo "Producing CPU (double) operator"
	@$^ -g add input_a.type=float64 input_b.type=float64 output.type=float64 -f add_double -e static_library,h,pytorch_wrapper -o $(@D) target=$* auto_schedule=false

$(BIN)/%/add_grad_double.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	@echo "Producing CPU (double) gradient"
	@$^ -g add_grad d_input_a.type=float64 d_input_b.type=float64 d_output.type=float64 -f add_grad_double -e static_library,h,pytorch_wrapper -o $(@D) target=$* auto_schedule=false

# Generate the GPU float version of the op
$(BIN)/%/add_cuda.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	@echo Producing CUDA operator
	@$^ -g add input_a.type=float32 input_b.type=float32 output.type=float32 -f add_cuda -e static_library,h,pytorch_wrapper -o $(@D) target=host-cuda-cuda_capability_61-user_context auto_schedule=false

# Generate the GPU double version of the op
$(BIN)/%/add_cuda_double.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	@echo "Producing CUDA (double) operator"
	@$^ -g add input_a.type=float64 input_b.type=float64 output.type=float64 -f add_cuda_double -e static_library,h,pytorch_wrapper -o $(@D) target=host-cuda-cuda_capability_61-user_context auto_schedule=false

# Build the Halide generator for the operator
.SECONDARY: $(generator_bin)/add.generator
$(generator_bin)/add.generator: src/add.cpp $(GENERATOR_DEPS)
	@echo Building Generator
	@mkdir -p $(@D) 
	@$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

joe:
	echo $(CXX) $(CXXFLAGS)
	
clean:
	rm -rf $(BIN) build lib dist halide_ops.egg-info .wrapper
