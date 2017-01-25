include ../support/Makefile.inc

CXXFLAGS += -g -Wall

.PHONY: clean

$(BIN)/interpolate: interpolate.cpp
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) interpolate.cpp $(LIB_HALIDE) -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS) $(LLVM_SHARED_LIBS)

$(BIN)/out.png: $(BIN)/interpolate
	@-mkdir -p $(BIN)
	$^ $(IMAGES)/rgba.png $@

clean:
	rm -rf $(BIN)
