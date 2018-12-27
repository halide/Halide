AUTOSCHED_SRC=../autoscheduler

WEIGHT_OBJECTS=\
$(BIN)/weights_head1_conv1_weight.o \
$(BIN)/weights_head1_conv1_bias.o \
$(BIN)/weights_head2_conv1_weight.o \
$(BIN)/weights_head2_conv1_bias.o \
$(BIN)/weights_trunk_conv1_weight.o \
$(BIN)/weights_trunk_conv1_bias.o

$(BIN)/weights_%.cpp:
	@mkdir -p $(@D)
	../../bin/binary2cpp weights_$* < $(AUTOSCHED_SRC)/weights/$*.data > $@

$(BIN)/weights_%.o: $(BIN)/weights_%.cpp
	$(CXX) -c $< -o $@

COST_MODEL_LIBS=\
$(BIN)/cost_model/cost_model.a \
$(BIN)/cost_model/train_cost_model.a \

$(BIN)/cost_model.generator: $(AUTOSCHED_SRC)/cost_model_generator.cpp \
							$(AUTOSCHED_SRC)/Derivative.cpp \
							$(AUTOSCHED_SRC)/Derivative.h \
							$(AUTOSCHED_SRC)/DerivativeUtils.cpp \
							$(AUTOSCHED_SRC)/DerivativeUtils.h \
							$(AUTOSCHED_SRC)/cost_model_schedule.h \
							$(AUTOSCHED_SRC)/NetworkSize.h \
							$(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -g $(filter-out %.h,$^) -o $@ $(HALIDE_SYSTEM_LIBS)

$(BIN)/auto_schedule_runtime.a: $(BIN)/cost_model.generator
	@mkdir -p $(@D)
	$^ -r auto_schedule_runtime -o $(BIN) target=$(HL_TARGET)

$(BIN)/cost_model/%.a: $(BIN)/cost_model.generator
	@mkdir -p $(@D)
	HL_PERMIT_FAILED_UNROLL=1 $^ -g $* -o $(BIN)/cost_model -f $* target=$(HL_TARGET)-no_runtime auto_schedule=false

# It's important to use dynamic lookups for undefined symbols here: all of libHalide
# is expected to be present (in the loading binary), so we explicitly make the symbols
# undefined rather than dependent on libHalide.so.
$(BIN)/libauto_schedule.so: $(AUTOSCHED_SRC)/AutoSchedule.cpp \
							$(AUTOSCHED_SRC)/DefaultCostModel.cpp \
							$(AUTOSCHED_SRC)/FunctionDAG.h \
							$(AUTOSCHED_SRC)/Featurization.h \
							$(AUTOSCHED_SRC)/CostModel.h \
							$(AUTOSCHED_SRC)/PerfectHashMap.h \
							$(WEIGHT_OBJECTS) \
							$(COST_MODEL_LIBS) \
							$(GENERATOR_DEPS) \
							$(BIN)/auto_schedule_runtime.a
	@mkdir -p $(@D)
	$(CXX) -shared $(USE_EXPORT_DYNAMIC) -fPIC $(CXXFLAGS) -g -I $(BIN)/cost_model $(AUTOSCHED_SRC)/AutoSchedule.cpp DefaultCostModel.cpp $(WEIGHT_OBJECTS) $(COST_MODEL_LIBS) $(BIN)/auto_schedule_runtime.a -O3 -o $@ $(HALIDE_SYSTEM_LIBS)

$(BIN)/train_cost_model: $(AUTOSCHED_SRC)/train_cost_model.cpp \
						 $(AUTOSCHED_SRC)/DefaultCostModel.cpp \
						 $(AUTOSCHED_SRC)/CostModel.h \
						 $(AUTOSCHED_SRC)/NetworkSize.h \
						 $(COST_MODEL_LIBS) \
						 $(WEIGHT_OBJECTS) \
						 $(BIN)/auto_schedule_runtime.a
	$(CXX) $(CXXFLAGS) -Wall -I $(BIN)/cost_model -O3 $(filter-out %.h,$^) -o $@ $(LDFLAGS) $(USE_OPEN_MP)

$(BIN)/augment_sample: $(AUTOSCHED_SRC)/augment_sample.cpp
	$(CXX) $< -O3 -o $@

