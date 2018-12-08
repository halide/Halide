include ../support/Makefile.inc

BIN ?= bin

WEIGHT_OBJECTS=\
$(BIN)/weights_head1_conv1_weight.o \
$(BIN)/weights_head1_conv1_bias.o \
$(BIN)/weights_head2_conv1_weight.o \
$(BIN)/weights_head2_conv1_bias.o \
$(BIN)/weights_trunk_conv1_weight.o \
$(BIN)/weights_trunk_conv1_bias.o \
$(BIN)/weights_schedule_mean.o \
$(BIN)/weights_schedule_std.o \
$(BIN)/weights_pipeline_mean.o \
$(BIN)/weights_pipeline_std.o \

$(BIN)/weights_%.cpp: 
	@mkdir -p $(@D)
	../../bin/binary2cpp halide_internal_weights_$* < weights/$*.data > $@

$(BIN)/weights_%.o: $(BIN)/weights_%.cpp
	$(CXX) -c $< -o $@

COST_MODEL_LIBS=\
$(BIN)/halide_autoscheduler_cost_model.a \
$(BIN)/halide_autoscheduler_train_cost_model.a \

$(BIN)/cost_model.generator: cost_model_generator.cpp Derivative.cpp Derivative.h DerivativeUtils.cpp DerivativeUtils.h cost_model_schedule.h $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/halide_autoscheduler_%.a: $(BIN)/cost_model.generator
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 $^ -g halide_autoscheduler_$* -o $(BIN) -f halide_autoscheduler_$* target=$(HL_TARGET) auto_schedule=false

$(BIN)/auto_schedule.so: AutoSchedule.cpp $(WEIGHT_OBJECTS) $(COST_MODEL_LIBS) $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) -shared -fPIC $(CXXFLAGS) AutoSchedule.cpp $(WEIGHT_OBJECTS) $(COST_MODEL_LIBS) -O3 -fno-rtti -o $@ $(HALIDE_SYSTEM_LIBS)

$(BIN)/train_cost_model: train_cost_model.cpp $(COST_MODEL_LIBS)
	$(CXX) $< -O3 -o $@

$(BIN)/augment_sample: augment_sample.cpp
	$(CXX) $< -O3 -o $@

# A sample generator to autoschedule. Note that if it statically links
# to libHalide, then it must be build with -rdynamic, or the
# autoscheduler can't find the libHalide symbols that it needs.
$(BIN)/demo.generator: demo_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -rdynamic -g -fno-rtti $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(HALIDE_SYSTEM_LIBS)

$(BIN)/demo.a: $(BIN)/demo.generator $(BIN)/auto_schedule.so
	HL_MACHINE_PARAMS=16,1,1 HL_PERMIT_FAILED_UNROLL=1 HL_WEIGHTS_DIR=weights \
	$(BIN)/demo.generator -g demo -o $(BIN) -f demo -e static_library,h target=$(HL_TARGET) auto_schedule=true -p $(BIN)/auto_schedule.so

$(BIN)/demo.rungen: $(BIN)/demo.a
	$(CXX) -std=c++11 -I $(BIN) -DHL_RUNGEN_FILTER_HEADER="\"demo.h\"" -I ../../include ../../tools/RunGenMain.cpp ../../tools/RunGenStubs.cpp  $(BIN)/demo.a -o $@ $(HALIDE_SYSTEM_LIBS) -lpng -ljpeg

# demonstrates single-shot use of the autoscheduler
test: $(BIN)/demo.rungen
	$(BIN)/demo.rungen --benchmarks=all --benchmark_min_time=1 --default_input_buffers=random:0:auto --output_extents=estimate --default_input_scalars=estimate

# demonstrates an autotuning loop
autotune: $(BIN)/demo.generator autotune_loop.sh
	bash autotune_loop.sh

clean:
	rm -rf $(BIN)


