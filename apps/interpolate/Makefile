include ../support/Makefile.inc

CXXFLAGS += -g -Wall

.PHONY: clean

$(BIN)/interpolate: interpolate.cpp $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) interpolate.cpp $(LIB_HALIDE) -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/out.png: $(BIN)/interpolate
	@mkdir -p $(@D)
	$^ $(IMAGES)/rgba.png $@

clean:
	rm -rf $(BIN)

test: $(BIN)/out.png
