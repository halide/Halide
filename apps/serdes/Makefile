include ../support/Makefile.inc

all: ir_serialize serialize_test

test: $(BIN)/serialize_test
	$(BIN)/serialize_test

$(BIN)/halide_ir_generated.h: halide_ir.fbs
	flatc --cpp -o $(@D) $^

ir_serialize: $(BIN)/halide_ir_generated.h

serialize_test: $(BIN)/serialize_test

$(BIN)/serialize_test: ir_serialize serialize_test.cpp Serializer.cpp Serializer.h Deserializer.cpp Deserializer.h Printer.cpp Printer.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -Wall -O3 serialize_test.cpp Serializer.cpp Deserializer.cpp Deserializer.h Printer.cpp Printer.h -o $@ $(LDFLAGS) $(IMAGE_IO_FLAGS) $(CUDA_LDFLAGS) $(OPENCL_LDFLAGS) $(LIBHALIDE_LDFLAGS)

clean:
	rm -rf $(BIN)