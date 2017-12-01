include ../support/Makefile.inc

BIN ?= bin

all: $(BIN)/process

$(BIN)/conv_layer.generator: conv_layer_generator.cpp $(GENERATOR_DEPS)
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS)

$(BIN)/conv_layer.a: $(BIN)/conv_layer.generator
	@-mkdir -p $(BIN)
	$^ -g conv_layer -o $(BIN) -f conv_layer target=$(HL_TARGET) auto_schedule=false

$(BIN)/conv_layer_auto_schedule.a: $(BIN)/conv_layer.generator
	@-mkdir -p $(BIN)
	$^ -g conv_layer -o $(BIN) -f conv_layer_auto_schedule target=$(HL_TARGET)-no_runtime auto_schedule=true

$(BIN)/process: process.cpp $(BIN)/conv_layer.a $(BIN)/conv_layer_auto_schedule.a
	@-mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) -I$(BIN) -Wall -O3 $^ -o $@ $(LDFLAGS)

run: $(BIN)/process
	@-mkdir -p $(BIN)
	$(BIN)/process

clean:
	rm -rf $(BIN)

test: run
