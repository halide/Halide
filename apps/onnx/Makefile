include ../support/Makefile.inc

BIN ?= bin

HL_TARTGET ?= host

ONNX_SRC_DIR ?= $(error ONNX_SRC_DIR must provided)

PROTOC = protoc
PROTOCFLAGS = --cpp_out=.

CXXFLAGS += -DGOOGLE_PROTOBUF_NO_RTTI -Wno-sign-compare -Wno-unused-but-set-variable
LDFLAGS += -lprotobuf-lite


# Copy onnx.proto to $(BIN)
$(BIN)/onnx/onnx.proto:
	@mkdir -p $(@D)
	cp $(ONNX_SRC_DIR)/onnx/onnx.proto $@

# protoc generates two files
$(BIN)/onnx/onnx.pb.cc: $(BIN)/onnx/onnx.proto
	@sed -i -e 's/package onnx;/package onnx;option optimize_for = LITE_RUNTIME;/g' $<
	@mkdir -p $(@D)
	$(PROTOC) $(PROTOCFLAGS) $<

$(BIN)/onnx/onnx_pb.h: $(BIN)/onnx/onnx.pb.cc
	cp $(BIN)/onnx/onnx.pb.h $(BIN)/onnx/onnx_pb.h

$(BIN)/onnx.pb.o: $(BIN)/onnx/onnx.pb.cc $(BIN)/onnx/onnx_pb.h
	@sed -i -e 's/bin\/onnx\/onnx.pb.h/onnx\/onnx.pb.h/g' $<
	$(CXX) $(CXXFLAGS) -I$(BIN) -fPIC -c $< -o $@

$(BIN)/onnx_converter_lib.o: onnx_converter.cc $(BIN)/onnx/onnx_pb.h 
	$(CXX) $(CXXFLAGS) -I$(BIN) -fPIC -c $< -o $@

ONNX_CONVERTER_LIB = \
		$(BIN)/onnx.pb.o \
		$(BIN)/onnx_converter_lib.o

clean:
	rm -rf $(BIN)

# Simple unit test
$(BIN)/onnx_converter_test: onnx_converter_test.cc $(ONNX_CONVERTER_LIB)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -I$(BIN) $< -o $@ $(LDFLAGS) $(ONNX_CONVERTER_LIB) $(LIB_HALIDE) $(HALIDE_SYSTEM_LIBS)

test: $(BIN)/onnx_converter_test
	LD_LIBRARY_PATH=$(BIN) $(BIN)/onnx_converter_test

