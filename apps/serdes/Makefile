include ../support/Makefile.inc

all: ir_serdes

test: $(BIN)/serdes_test
	$(BIN)/serdes_test

$(BIN)/halide_ir_generated.h: halide_ir.fbs
	flatc --cpp -o $(@D) $^

ir_serdes: bin/halide_ir_generated.h

$(BIN)/serdes_test: ir_serdes serdes_test.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 serdes_test.cpp -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(LIBHALIDE_LDFLAGS)

clean:
	rm -rf $(BIN)