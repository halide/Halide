include ../support/Makefile.inc

all: ir_serdes serdes_test

test: $(BIN)/serdes_test
	$(BIN)/serdes_test

$(BIN)/halide_ir_generated.h: halide_ir.fbs
	flatc --cpp -o $(@D) $^

ir_serdes: $(BIN)/halide_ir_generated.h

serdes_test: $(BIN)/serdes_test

$(BIN)/serdes_test: ir_serdes serdes_test.cpp Serializer.cpp Serializer.h Deserializer.cpp Deserializer.h Printer.cpp Printer.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 serdes_test.cpp Serializer.cpp Deserializer.cpp Deserializer.h Printer.cpp Printer.h -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(LIBHALIDE_LDFLAGS)

clean:
	rm -rf $(BIN)