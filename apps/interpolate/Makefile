include ../support/Makefile.inc

CXXFLAGS += -g -Wall

.PHONY: clean

$(BIN)/interpolate: interpolate.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) interpolate.cpp $(LIB_HALIDE) -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(BIN)/out.png: $(BIN)/interpolate
	@mkdir -p $(@D)
	$^ $(IMAGES)/rgba.png $@

clean:
	rm -rf $(BIN)

test: $(BIN)/out.png
