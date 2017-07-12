include ../support/Makefile.inc

all: $(BIN)/filter

$(BIN)/bilateral_grid_exec: bilateral_grid_generator.cpp $(GENERATOR_DEPS)
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS)

$(BIN)/bilateral_grid.a: $(BIN)/bilateral_grid_exec
	@-mkdir -p $(BIN)
	$^ -o $(BIN)  target=$(HL_TARGET)

$(BIN)/viz/bilateral_grid.a: $(BIN)/bilateral_grid_exec
	@-mkdir -p $(BIN)
	@-mkdir -p $(BIN)/viz
	$^ -o $(BIN)/viz target=$(HL_TARGET)-trace_loads-trace_stores-trace_realizations

$(BIN)/filter: $(BIN)/bilateral_grid.a filter.cpp
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -O3 -ffast-math -Wall -Werror -I$(BIN) filter.cpp $(BIN)/bilateral_grid.a -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)
$(BIN)/filter_viz: $(BIN)/viz/bilateral_grid.a filter.cpp
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -O3 -ffast-math -Wall -Werror -I$(BIN)/viz filter.cpp $(BIN)/viz/bilateral_grid.a -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

$(BIN)/bilateral_grid.mp4: $(BIN)/filter_viz viz.sh
	@-mkdir -p $(BIN)
	bash viz.sh $(BIN)

$(BIN)/out.png: $(BIN)/filter
	@-mkdir -p $(BIN)
	$(BIN)/filter $(IMAGES)/gray.png $(BIN)/out.png 0.1 10

clean:
	rm -rf $(BIN)
