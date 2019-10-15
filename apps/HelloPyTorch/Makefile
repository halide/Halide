include ../support/Makefile.inc

OPS = $(BIN)/$(HL_TARGET)/add_float32.a \
      $(BIN)/$(HL_TARGET)/add_float64.a \
      $(BIN)/$(HL_TARGET)/add_grad_float32.a \
      $(BIN)/$(HL_TARGET)/add_grad_float64.a

# Check whether we have cuda installed, if add the CUDA ops as dependencies
ifeq ($(shell which nvcc),)
HAS_CUDA = 0
CUDA_OPS =
else
HAS_CUDA = 1
CUDA_OPS = $(BIN)/$(HL_TARGET)/add_cuda_float32.a \
	   $(BIN)/$(HL_TARGET)/add_cuda_float64.a \
           $(BIN)/$(HL_TARGET)/add_grad_cuda_float32.a \
	   $(BIN)/$(HL_TARGET)/add_grad_cuda_float64.a 
endif

EXT_LIB=$(BIN)/lib
PYTHON ?= python3
PIP ?= pip3

all: test


# Run the PyTorch tests to verify the module is compiled correctly
# .wrapper is a dummy file whose timestamps allow Make to track the dependency
# on the Python side of the build 


test: $(BIN)/.wrapper
	@PYTHONPATH=$(EXT_LIB):${PYTHONPATH} $(PYTHON) test.py


# Build the python PyTorch extension that links against the Halide operators
$(BIN)/.wrapper: $(OPS) $(CUDA_OPS) setup.py
	@mkdir -p $(EXT_LIB)
	@HAS_CUDA=$(HAS_CUDA) \
	      HALIDE_DISTRIB_PATH=$(HALIDE_DISTRIB_PATH) \
	      BIN=$(BIN)/$(HL_TARGET) \
	      PYTHONPATH=$(EXT_LIB):${PYTHONPATH} \
	      $(PIP) install . -t $(EXT_LIB) -b $(BIN)
	@touch $(BIN)/.wrapper


# Generate the CPU version of the op ------------------------------------------
$(BIN)/%/add_float32.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	@echo Producing CPU operator
	@$^ -g add input_a.type=float32 input_b.type=float32 output.type=float32 \
		-f add_float32 -e static_library,c_header,pytorch_wrapper -o $(@D) \
		target=$* auto_schedule=false

$(BIN)/%/add_grad_float32.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	@echo Producing CPU gradient
	@$^ -g add_grad \
		input_a.type=float32 input_b.type=float32 \
		d_input_a.type=float32 d_input_b.type=float32 \
		d_output.type=float32 -f add_grad_float32 \
		-e static_library,c_header,pytorch_wrapper -o $(@D) \
		target=$* auto_schedule=false

$(BIN)/%/add_float64.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	@echo "Producing CPU (double) operator"
	@$^ -g add input_a.type=float64 input_b.type=float64 output.type=float64 \
		-f add_float64 -e static_library,c_header,pytorch_wrapper -o $(@D) \
		target=$* auto_schedule=false

$(BIN)/%/add_grad_float64.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	@echo "Producing CPU (double) gradient"
	@$^ -g add_grad \
		input_a.type=float64 input_b.type=float64 \
		d_input_a.type=float64 d_input_b.type=float64 \
		d_output.type=float64 -f add_grad_float64 \
		-e static_library,c_header,pytorch_wrapper -o $(@D) \
		target=$* auto_schedule=false
# -----------------------------------------------------------------------------

# Generate the GPU version of the op ------------------------------------------
$(BIN)/%/add_cuda_float32.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	@echo Producing CUDA operator
	@$^ -g add input_a.type=float32 input_b.type=float32 output.type=float32 \
		-f add_cuda_float32 -e static_library,c_header,pytorch_wrapper -o $(@D) \
		target=host-cuda-cuda_capability_61-user_context auto_schedule=false

$(BIN)/%/add_grad_cuda_float32.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	@echo "Producing CUDA (double) gradient"
	@$^ -g add_grad \
		input_a.type=float32 input_b.type=float32 \
		d_input_a.type=float32 d_input_b.type=float32 \
		d_output.type=float32 -f add_grad_cuda_float32 \
		-e static_library,c_header,pytorch_wrapper -o $(@D) \
		target=host-cuda-cuda_capability_61-user_context auto_schedule=false

$(BIN)/%/add_cuda_float64.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	@echo "Producing CUDA (double) operator"
	@$^ -g add input_a.type=float64 input_b.type=float64 output.type=float64 \
		-f add_cuda_float64 -e static_library,c_header,pytorch_wrapper -o $(@D) \
		target=host-cuda-cuda_capability_61-user_context auto_schedule=false

$(BIN)/%/add_grad_cuda_float64.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	@echo Producing CUDA gradient
	@$^ -g add_grad \
		input_a.type=float64 input_b.type=float64 \
		d_input_a.type=float64 d_input_b.type=float64 \
		d_output.type=float64 -f add_grad_cuda_float64 \
		-e static_library,c_header,pytorch_wrapper -o $(@D) \
		target=host-cuda-cuda_capability_61-user_context auto_schedule=false
# -----------------------------------------------------------------------------

# Build the Halide generator for the operator
$(GENERATOR_BIN)/add.generator: src/add_generator.cpp $(GENERATOR_DEPS)
	@echo Building Generator
	@mkdir -p $(@D)
	@$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) \
		-o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

clean:
	rm -rf $(BIN) __pycache__
