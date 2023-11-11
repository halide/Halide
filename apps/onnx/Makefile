# Default to the adams 2019 autoscheduler
AUTOSCHEDULER ?= adams2019
include ../support/Makefile.inc

ifneq (,$(findstring -m32,$(CXX) $(CC) $(CCFLAGS) $(CXXFLAGS)))
build:
	@echo "Not testing this app in a 32-bit build (-m32 found in flags)"
test: build
else

PROTOC := $(shell which protoc)

ifdef PROTOC

CXXFLAGS += -DGOOGLE_PROTOBUF_NO_RTTI -Wno-sign-compare -Wno-unused-but-set-variable
CXXFLAGS += -I$(dir $(PROTOC))../include
LDFLAGS += -L$(dir $(PROTOC))../lib
LDFLAGS += $(shell pkg-config protobuf-lite --libs)

# Copy onnx.proto to $(BIN)
$(BIN)/%/onnx/onnx.proto:
	@mkdir -p $(@D)
	@if [ -f $(ONNX_SRC_DIR)/onnx/onnx.proto ]; then \
		cp $(ONNX_SRC_DIR)/onnx/onnx.proto $@ ; \
	else \
		curl -s https://raw.githubusercontent.com/onnx/onnx/v1.4.1/onnx/onnx.proto > $@; \
	fi

# protoc generates two files
$(BIN)/%/onnx/onnx.pb.cc: $(BIN)/%/onnx/onnx.proto
	@sed -i -e 's/package onnx;/package onnx;option optimize_for = LITE_RUNTIME;/g' $<
	@mkdir -p $(@D)
	$(PROTOC) --cpp_out=$(@D) --proto_path=$(dir $<) $<

$(BIN)/%/onnx/onnx_pb.h: $(BIN)/%/onnx/onnx.pb.cc
	cp $(BIN)/$*/onnx/onnx.pb.h $@

$(BIN)/%/onnx.pb.o: $(BIN)/%/onnx/onnx.pb.cc $(BIN)/%/onnx/onnx_pb.h
	@sed -i -e 's/bin\/onnx\/onnx.pb.h/onnx\/onnx.pb.h/g' $<
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -I. -fPIC -c $< -o $@

$(BIN)/%/onnx_converter_lib.o: onnx_converter.cc $(BIN)/%/onnx/onnx_pb.h
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* -fPIC -c $< -o $@

$(BIN)/%/oclib.a: $(BIN)/%/onnx_converter_lib.o $(BIN)/%/onnx.pb.o
	ar q $@ $^

clean:
	rm -rf $(BIN)

# Simple unit test
$(BIN)/%/onnx_converter_test: onnx_converter_test.cc $(BIN)/%/oclib.a
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -I$(BIN)/$* $^ -o $@ $(LIBHALIDE_LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(GENERATOR_BIN)/onnx_converter.generator : onnx_converter_generator.cc $(GENERATOR_DEPS) $(GENERATOR_BIN)/oclib.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(GENERATOR_BIN) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS) $(HALIDE_SYSTEM_LIBS)

# Convolution with autopad=SAME_UPPER isn't supported yet (Also deprecated). TODO Enable this.
#$(BIN)/%/onnx_model_zoo/mnist/model.onnx:
#	@mkdir -p $(@D)
#	curl -o $(@D)/mnist.tar.gz https://onnxzoo.blob.core.windows.net/models/opset_8/mnist/mnist.tar.gz
#	tar xvzf $(@D)/mnist.tar.gz -C $(BIN)/$*/onnx_model_zoo

#$(BIN)/%/onnx_mnist_model.a : $(GENERATOR_BIN)/onnx_converter.generator $(BIN)/%/onnx_model_zoo/mnist/model.onnx
#	@mkdir -p $(@D)
#	$^ -g onnx_model_inference -o $(@D) -f onnx_model_inference target=$* model_file_path=$(BIN)/$*/onnx_model_zoo/mnist/model.onnx

$(BIN)/%/test_model.onnx: test_model_proto.txt $(BIN)/%/onnx/onnx.proto
	@mkdir -p $(@D)
	cat $< | $(PROTOC) --encode=onnx.ModelProto --proto_path=$(BIN)/$*/onnx $(BIN)/$*/onnx/onnx.proto > $@

$(BIN)/%/test_model.a: $(GENERATOR_BIN)/onnx_converter.generator $(BIN)/%/test_model.onnx
	@mkdir -p $(@D)
	$< -g onnx_model_generator -o $(@D) -f test_model target=$* model_file_path=$(BIN)/$*/test_model.onnx

$(BIN)/%/onnx_converter_generator_test: onnx_converter_generator_test.cc $(BIN)/%/test_model.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -I$(BIN)/$* $^ -o $@ $(LDFLAGS)

build: $(BIN)/$(HL_TARGET)/onnx_converter_test $(BIN)/$(HL_TARGET)/onnx_converter_generator_test

test: build model_test
	LD_LIBRARY_PATH=$(BIN) $(BIN)/$(HL_TARGET)/onnx_converter_test
	LD_LIBRARY_PATH=$(BIN) $(BIN)/$(HL_TARGET)/onnx_converter_generator_test

PYTHON ?= python3
PYBIND11_CFLAGS = $(shell pybind11-config --includes) -frtti -std=c++17
ifeq ($(UNAME), Darwin)
    # Keep OS X builds from complaining about missing libpython symbols
    PYBIND11_CFLAGS += -undefined dynamic_lookup
endif
PY_EXT = $(shell $(PYTHON)-config --extension-suffix)
PY_MODEL_EXT = model_cpp$(PY_EXT)
PYCXXFLAGS = $(PYBIND11_CFLAGS) $(CXXFLAGS) -Wno-deprecated-register

# Python extension for HalideModel
$(BIN)/%/$(PY_MODEL_EXT): model.cpp $(BIN)/%/oclib.a
	$(CXX) $(PYCXXFLAGS) -Wall -shared -fPIC -I$(BIN)/$* $^ $(LIBHALIDE_LDFLAGS) -o $@ $(LDFLAGS)


model_test: $(BIN)/$(HL_TARGET)/$(PY_MODEL_EXT)
	PYTHONPATH="$(BIN)/$(HL_TARGET)/:$$PYTHONPATH" $(PYTHON) -m unittest model_test.py -v

halide_as_onnx_backend_test: $(BIN)/$(HL_TARGET)/$(PY_MODEL_EXT)
	PYTHONPATH="$(BIN)/$(HL_TARGET)/:$$PYTHONPATH" $(PYTHON) -m unittest halide_as_onnx_backend_test.py -v

# No Protoc
else
build:
	@echo "No protoc in $(PATH), you need to install protocol buffers"
test: build

endif
endif
