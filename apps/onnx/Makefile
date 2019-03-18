include ../support/Makefile.inc

BIN ?= bin

HL_TARTGET ?= host

ONNX_SRC_DIR ?= $(error ONNX_SRC_DIR must provided)

PROTOC = protoc
PROTOCFLAGS = --cpp_out=.

CXXFLAGS += -DGOOGLE_PROTOBUF_NO_RTTI -Wno-sign-compare --no-warning
LDFLAGS += -lprotobuf-lite


# Copy onnx.proto to $(BIN)
$(BIN)/onnx.proto:
	@mkdir -p $(@D)
	cp $(ONNX_SRC_DIR)/onnx/onnx.proto $@


# protoc generates two files
$(BIN)/onnx.pb.cc: $(BIN)/onnx.proto
	@sed -i -e 's/package onnx;/package onnx;option optimize_for = LITE_RUNTIME;/g' $<
	$(PROTOC) $(PROTOCFLAGS) $<

$(BIN)/onnx.pb.h: $(BIN)/onnx.pb.cc


$(BIN)/onnx.pb.o: $(BIN)/onnx.pb.cc $(BIN)/onnx.pb.h
	@sed -i -e 's/bin\/onnx.pb.h/onnx.pb.h/g' $<
	$(CXX) $(CXXFLAGS) -I$(BIN) -fPIC -c $< -o $@

$(BIN)/onnx_converter_lib.o: onnx_converter.cc
	$(CXX) $(CXXFLAGS) -I$(BIN) -fPIC -c $< -o $@


ONNX_CONVERTER_LIB = \
		$(BIN)/onnx.pb.o \
		$(BIN)/onnx_converter_lib.o

PYTHON ?= python3

PYBIND11_CFLAGS = $(shell $(PYTHON) -m pybind11 --includes) -frtti


PY_EXT = $(shell $(PYTHON)-config --extension-suffix)

PYCXXFLAGS =  $(CXXFLAGS) $(PYBIND11_CFLAGS) 
HALIDE_LIB = $(HALIDE_DISTRIB_PATH)/bin/libHalide.so

# Python extenstion for HalideModel
$(BIN)/model_cpp: model.cpp $(ONNX_CONVERTER_LIB)
	$(CXX) $(PYCXXFLAGS) -O3 -Wall -shared -fPIC -I$(BIN) $< $(ONNX_CONVERTER_LIB) $(HALIDE_LIB) -o $@$(PY_EXT) $(LDFLAGS)


clean:
	rm -rf $(BIN)

# Run python unittests.
test_model_python: $(BIN)/model_cpp
	$(PYTHON) -m unittest model_test

