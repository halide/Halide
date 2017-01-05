include ../support/Makefile.inc

all: $(BIN)/filter

$(BIN)/bilateral_grid_exec: bilateral_grid_generator.cpp $(GENERATOR_DEPS)
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS)

$(BIN)/bilateral_grid.a: $(BIN)/bilateral_grid_exec
	@-mkdir -p $(BIN)
	$^ -o $(BIN)  target=$(HL_TARGET)

$(BIN)/filter: $(BIN)/bilateral_grid.a filter.cpp
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -O3 -ffast-math -Wall -Werror -I$(BIN) filter.cpp $(BIN)/bilateral_grid.a -o $@ $(PNGFLAGS) $(LDFLAGS)

$(BIN)/bilateral_grid.mp4: $(BIN)/filter viz.sh
	@-mkdir -p $(BIN)
	bash viz.sh $(BIN)

$(BIN)/out.png: $(BIN)/filter
	@-mkdir -p $(BIN)
	$(BIN)/filter $(IMAGES)/gray.png $(BIN)/out.png 0.1 10

clean:
	rm -rf $(BIN)
