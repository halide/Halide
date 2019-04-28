include ../support/Makefile.inc

BIN ?= bin

HL_TARGET ?= host

PROTOC := $(shell which protoc)

ifdef PROTOC

PROTOCFLAGS = --cpp_out=.
CXXFLAGS += -DGOOGLE_PROTOBUF_NO_RTTI -Wno-sign-compare -Wno-unused-but-set-variable
CXXFLAGS += -I$(dir $(PROTOC))../include
LDFLAGS += -L$(dir $(PROTOC))../lib
LDFLAGS += -lprotobuf-lite

# Copy onnx.proto to $(BIN)
$(BIN)/onnx/onnx.proto:
	@mkdir -p $(@D)
	if [ -f $(ONNX_SRC_DIR)/onnx/onnx.proto ]; then \
		cp $(ONNX_SRC_DIR)/onnx/onnx.proto $@ ; \
	else \
		curl https://raw.githubusercontent.com/onnx/onnx/v1.4.1/onnx/onnx.proto > $@; \
	fi

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


$(BIN)/onnx_converter.generator : onnx_converter_generator.cc $(GENERATOR_DEPS) $(ONNX_CONVERTER_LIB)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

# Convolution with autopad=SAME_UPPER isn't supported yet (Also deprecated). TODO Enable this.
#$(BIN)/onnx_model_zoo/mnist/model.onnx:
#	@mkdir -p $(@D)
#	curl -o $(@D)/mnist.tar.gz https://onnxzoo.blob.core.windows.net/models/opset_8/mnist/mnist.tar.gz
#	tar xvzf $(@D)/mnist.tar.gz -C $(BIN)/onnx_model_zoo

#$(BIN)/onnx_mnist_model.a : $(BIN)/onnx_converter.generator $(BIN)/onnx_model_zoo/mnist/model.onnx
#	@mkdir -p $(@D)
#	$^ -g onnx_model_inference -o $(BIN) -f onnx_model_inference target=$(HL_TARGET) model_file_path=$(BIN)/onnx_model_zoo/mnist/model.onnx

$(BIN)/test_model.onnx: test_model_proto.txt $(BIN)/onnx/onnx.proto
	@mkdir -p $(@D)
	cat $< | protoc --encode=onnx.ModelProto $(BIN)/onnx/onnx.proto > $@

$(BIN)/test_model.a: $(BIN)/onnx_converter.generator $(BIN)/test_model.onnx
	@mkdir -p $(@D)
	$< -g onnx_model_generator -o $(BIN) -f test_model target=$(HL_TARGET) model_file_path=$(BIN)/test_model.onnx

$(BIN)/onnx_converter_generator_test: onnx_converter_generator_test.cc $(BIN)/test_model.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -I$(BIN) $^ -o $@ $(LDFLAGS)

test: $(BIN)/onnx_converter_test $(BIN)/onnx_converter_generator_test
	LD_LIBRARY_PATH=$(BIN) $(BIN)/onnx_converter_test
	LD_LIBRARY_PATH=$(BIN) $(BIN)/onnx_converter_generator_test

PYTHON ?= python3
PYBIND11_CFLAGS = $(shell $(PYTHON) -m pybind11 --includes) -frtti
PY_EXT = $(shell $(PYTHON)-config --extension-suffix)
PY_MODEL_EXT = model_cpp$(PY_EXT)
PYCXXFLAGS =  $(CXXFLAGS) $(PYBIND11_CFLAGS)

# Python extension for HalideModel
$(BIN)/$(PY_MODEL_EXT): model.cpp $(ONNX_CONVERTER_LIB)
	$(CXX) $(PYCXXFLAGS) -O3 -Wall -shared -fPIC -I$(BIN) $< $(ONNX_CONVERTER_LIB) $(LIB_HALIDE) -o $@ $(LDFLAGS)


model_test: $(BIN)/$(PY_MODEL_EXT)
	PYTHONPATH="$(BIN)/:$$PYTHONPATH" $(PYTHON) -m unittest model_test.py -v

halide_as_onnx_backend_test: $(BIN)/$(PY_MODEL_EXT)
	PYTHONPATH="$(BIN)/:$$PYTHONPATH" $(PYTHON) -m unittest halide_as_onnx_backend_test.py -v

# No Protoc
else
test:
	@echo "No protoc in $(PATH), you need to install protocol buffers"
endif
