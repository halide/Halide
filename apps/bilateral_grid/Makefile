include ../support/Makefile.inc

TARGET_BIN=$(BIN)/$(HL_TARGET)

all: $(BIN)/filter

$(BIN)/bilateral_grid_exec: bilateral_grid_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LDFLAGS)

$(TARGET_BIN)/bilateral_grid.a: $(BIN)/bilateral_grid_exec
	@mkdir -p $(@D)
	$^ -g bilateral_grid -o $(TARGET_BIN) -f bilateral_grid target=$(HL_TARGET) auto_schedule=false

$(TARGET_BIN)/bilateral_grid_auto_schedule.a: $(BIN)/bilateral_grid_exec
	@mkdir -p $(@D)
	$^ -g bilateral_grid -o $(TARGET_BIN) -f bilateral_grid_auto_schedule target=$(HL_TARGET)-no_runtime auto_schedule=true

$(TARGET_BIN)/viz/bilateral_grid.a: $(BIN)/bilateral_grid_exec
	@mkdir -p $(@D)
	$^ -o $(TARGET_BIN)/viz target=$(HL_TARGET)-trace_loads-trace_stores-trace_realizations

$(TARGET_BIN)/filter: $(TARGET_BIN)/bilateral_grid.a $(TARGET_BIN)/bilateral_grid_auto_schedule.a filter.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -O3 -ffast-math -Wall -Werror -I$(TARGET_BIN) filter.cpp $(TARGET_BIN)/bilateral_grid.a $(TARGET_BIN)/bilateral_grid_auto_schedule.a -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)
$(TARGET_BIN)/filter_viz: $(TARGET_BIN)/viz/bilateral_grid.a filter.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -O3 -ffast-math -Wall -Werror -I$(TARGET_BIN)/viz filter.cpp $(TARGET_BIN)/viz/bilateral_grid.a -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

$(BIN)/bilateral_grid.mp4: $(TARGET_BIN)/filter_viz viz.sh
	@mkdir -p $(@D)
	bash viz.sh $(TARGET_BIN)

$(BIN)/out.png: $(TARGET_BIN)/filter
	@mkdir -p $(@D)
	$(TARGET_BIN)/filter $(IMAGES)/gray.png $(BIN)/out.png 0.1 10

clean:
	rm -rf $(BIN)
