include ../support/Makefile.inc

.PHONY: build clean test

build: $(BIN)/$(HL_TARGET)/filter

$(GENERATOR_BIN)/bilateral_grid.generator: bilateral_grid_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/%/bilateral_grid.a: $(GENERATOR_BIN)/bilateral_grid.generator
	@mkdir -p $(@D)
	$^ -g bilateral_grid -e $(GENERATOR_OUTPUTS) -o $(@D) -f bilateral_grid target=$*

$(BIN)/%/bilateral_grid_auto_schedule.a: $(GENERATOR_BIN)/bilateral_grid.generator
	@mkdir -p $(@D)
	$^ -g bilateral_grid -e $(GENERATOR_OUTPUTS) -o $(@D) -f bilateral_grid_auto_schedule target=$*-no_runtime autoscheduler=Mullapudi2016

$(BIN)/%/filter: filter.cpp $(BIN)/%/bilateral_grid.a $(BIN)/%/bilateral_grid_auto_schedule.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* $^ -o $@ $(IMAGE_IO_FLAGS) $(LDFLAGS)

$(BIN)/%/out.png: $(BIN)/%/filter
	@mkdir -p $(@D)
	$< $(IMAGES)/gray.png $@ 0.1 10

$(BIN)/%/out.tiff: $(BIN)/%/filter
	@mkdir -p $(@D)
	$< $(IMAGES)/gray.png $@ 0.1 10

clean:
	rm -rf $(BIN)

test: $(BIN)/$(HL_TARGET)/out.png $(BIN)/$(HL_TARGET)/out.tiff
