AUTOSCHED_SRC ?= ../autoscheduler
AUTOSCHED_BIN ?= $(AUTOSCHED_SRC)/bin

AUTOSCHED_WEIGHT_OBJECTS=\
$(AUTOSCHED_BIN)/weights_head1_conv1_weight.o \
$(AUTOSCHED_BIN)/weights_head1_conv1_bias.o \
$(AUTOSCHED_BIN)/weights_head2_conv1_weight.o \
$(AUTOSCHED_BIN)/weights_head2_conv1_bias.o \
$(AUTOSCHED_BIN)/weights_trunk_conv1_weight.o \
$(AUTOSCHED_BIN)/weights_trunk_conv1_bias.o

# TODO(srj): depending on something not in the distrib folder isn't strictly
# kosher, but this is still experimental
$(AUTOSCHED_BIN)/binary2cpp: ../../tools/binary2cpp.cpp
	@mkdir -p $(@D)
	$(CXX) $< -o $@

$(AUTOSCHED_BIN)/weights_%.cpp: $(AUTOSCHED_BIN)/binary2cpp
	@mkdir -p $(@D)
	$(AUTOSCHED_BIN)/binary2cpp weights_$* < $(AUTOSCHED_SRC)/weights/$*.data > $@

$(AUTOSCHED_BIN)/weights_%.o: $(AUTOSCHED_BIN)/weights_%.cpp
	$(CXX) -c $< -o $@

AUTOSCHED_COST_MODEL_LIBS=\
$(AUTOSCHED_BIN)/cost_model/cost_model.a \
$(AUTOSCHED_BIN)/cost_model/train_cost_model.a \

$(AUTOSCHED_BIN)/cost_model.generator: $(AUTOSCHED_SRC)/cost_model_generator.cpp \
							$(AUTOSCHED_SRC)/Derivative.cpp \
							$(AUTOSCHED_SRC)/Derivative.h \
							$(AUTOSCHED_SRC)/DerivativeUtils.cpp \
							$(AUTOSCHED_SRC)/DerivativeUtils.h \
							$(AUTOSCHED_SRC)/cost_model_schedule.h \
							$(AUTOSCHED_SRC)/NetworkSize.h \
							$(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(HALIDE_SYSTEM_LIBS) $(USE_EXPORT_DYNAMIC)

$(AUTOSCHED_BIN)/auto_schedule_runtime.a: $(AUTOSCHED_BIN)/cost_model.generator
	@mkdir -p $(@D)
	$^ -r auto_schedule_runtime -o $(AUTOSCHED_BIN) target=$(HL_TARGET)

$(AUTOSCHED_BIN)/cost_model/%.a: $(AUTOSCHED_BIN)/cost_model.generator
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 $^ -g $* -o $(AUTOSCHED_BIN)/cost_model -f $* target=$(HL_TARGET)-no_runtime auto_schedule=false -e stmt,static_library,h,assembly

# It's important to use dynamic lookups for undefined symbols here: all of libHalide
# is expected to be present (in the loading binary), so we explicitly make the symbols
# undefined rather than dependent on libHalide.so.
$(AUTOSCHED_BIN)/libauto_schedule.so: $(AUTOSCHED_SRC)/AutoSchedule.cpp \
							$(AUTOSCHED_SRC)/DefaultCostModel.cpp \
							$(AUTOSCHED_SRC)/FunctionDAG.h \
							$(AUTOSCHED_SRC)/Featurization.h \
							$(AUTOSCHED_SRC)/CostModel.h \
							$(AUTOSCHED_SRC)/PerfectHashMap.h \
							$(AUTOSCHED_WEIGHT_OBJECTS) \
							$(AUTOSCHED_COST_MODEL_LIBS) \
							$(GENERATOR_DEPS) \
							$(AUTOSCHED_BIN)/auto_schedule_runtime.a
	@mkdir -p $(@D)
	$(CXX) -shared $(USE_EXPORT_DYNAMIC) -fPIC $(CXXFLAGS) -g -I $(AUTOSCHED_BIN)/cost_model $(AUTOSCHED_SRC)/AutoSchedule.cpp $(AUTOSCHED_SRC)/DefaultCostModel.cpp $(AUTOSCHED_WEIGHT_OBJECTS) $(AUTOSCHED_COST_MODEL_LIBS) $(AUTOSCHED_BIN)/auto_schedule_runtime.a -O3 -o $@ $(HALIDE_SYSTEM_LIBS)

$(AUTOSCHED_BIN)/train_cost_model: $(AUTOSCHED_SRC)/train_cost_model.cpp \
						 $(AUTOSCHED_SRC)/DefaultCostModel.cpp \
						 $(AUTOSCHED_SRC)/CostModel.h \
						 $(AUTOSCHED_SRC)/NetworkSize.h \
						 $(AUTOSCHED_COST_MODEL_LIBS) \
						 $(AUTOSCHED_WEIGHT_OBJECTS) \
						 $(AUTOSCHED_BIN)/auto_schedule_runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -Wall -I $(AUTOSCHED_BIN)/cost_model -O3 $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(USE_OPEN_MP)

$(AUTOSCHED_BIN)/augment_sample: $(AUTOSCHED_SRC)/augment_sample.cpp
	@mkdir -p $(@D)
	$(CXX) $< -O3 -o $@


# This is the value that machine_params defaults to if no custom value is specified;
# see MachineParams::generic()
HL_MACHINE_PARAMS ?= 32,25165824,160


