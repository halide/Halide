include ../support/Makefile.inc

all: $(BIN)/filter

$(BIN)/bilateral_grid.a: bilateral_grid_generator.cpp
	@-mkdir -p $(BIN)
	$(HALIDE_SRC_PATH)/tools/gengen.sh -c $(CXX) -l $(LIB_HALIDE) -o $(BIN) -s $^ target=host

$(BIN)/filter: $(BIN)/bilateral_grid.a filter.cpp
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -O3 -ffast-math -Wall -Werror -I$(BIN) filter.cpp $(BIN)/bilateral_grid.a -o $@ $(PNGFLAGS) $(LDFLAGS)

$(BIN)/bilateral_grid.mp4: bilateral_grid_generator.cpp viz.sh
	@-mkdir -p $(BIN)
	bash viz.sh $(BIN)

$(BIN)/out.png: $(BIN)/filter
	@-mkdir -p $(BIN)
	$(BIN)/filter $(IMAGES)/gray.png $(BIN)/out.png 0.1 10

clean:
	rm -rf $(BIN)
