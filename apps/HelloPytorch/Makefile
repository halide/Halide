include ../support/Makefile.inc

BUILD_DIR = build

test: 
	@python test.py

wrapper: $(BIN)/$(HL_TARGET)/add.a
	HALIDE_DISTRIB_PATH=$(HALIDE_DISTRIB_PATH) BIN=$(BIN)/$(HL_TARGET) python setup.py install

$(BIN)/%/add.a: $(GENERATOR_BIN)/add.generator
	@mkdir -p $(@D)
	@echo Producing binary $(HL_TARGET)
	$^ -g add -e static_library,h,pytorch_wrapper -o $(@D) -f add target=$* auto_schedule=false

$(GENERATOR_BIN)/add.generator: add.cpp $(GENERATOR_DEPS)
	@echo Building Generator
	@mkdir -p $(@D) 
	@$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)
	
clean:
	rm -rf $(BIN)
