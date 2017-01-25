include ../support/Makefile.inc

CXXFLAGS += -g -Wall

BIN ?= bin

.PHONY: clean

$(BIN)/resize: ../../ resize.cpp
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) resize.cpp $(LIB_HALIDE) -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

$(BIN)/out.png: $(BIN)/resize
	@-mkdir -p $(BIN)
	$(BIN)/resize $(IMAGES)/rgba.png $(BIN)/out.png -f 2.0 -t cubic -s 3

clean:
	rm -rf $(BIN)
