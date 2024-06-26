THIS_MAKEFILE = $(realpath $(filter %Makefile, $(MAKEFILE_LIST)))
SRC = $(strip $(shell dirname $(THIS_MAKEFILE)))
HALIDE_SRC_ROOT = $(realpath $(SRC)/../../../)
COMMON_DIR ?= $(realpath $(SRC)/../common/)
HALIDE_TEST_DIR ?= $(HALIDE_SRC_ROOT)/test/autoschedulers/anderson2021

HALIDE_DISTRIB_PATH ?= $(HALIDE_SRC_ROOT)/distrib
HL_TARGET ?= host

$(info Looking for Halide distro at $(HALIDE_DISTRIB_PATH). If this is incorrect, set the make variable HALIDE_DISTRIB_PATH)

# Don't include an autoscheduler in the generator deps
AUTOSCHEDULER=
include $(HALIDE_SRC_ROOT)/apps/support/Makefile.inc

# Add the relative location of libHalide.so in the rpath in a distro
ifeq ($(UNAME), Darwin)
HALIDE_RPATH_FOR_BIN = '-Wl,-rpath,@executable_path/../lib'
HALIDE_RPATH_FOR_LIB = '-Wl,-rpath,@loader_path'
else
HALIDE_RPATH_FOR_BIN = '-Wl,-rpath,$$ORIGIN/../lib'
HALIDE_RPATH_FOR_LIB = '-Wl,-rpath,$$ORIGIN'
endif

CXXFLAGS += -I$(COMMON_DIR)
	
ENABLE_DEBUG_OUTPUT ?= false

AUTOSCHED_SAMPLES_OUT ?= $(SRC)/samples

AUTOSCHED_WEIGHT_OBJECTS=$(BIN)/baseline_weights.o

$(BIN)/binary2cpp: $(HALIDE_SRC_ROOT)/tools/binary2cpp.cpp
	@mkdir -p $(@D)
	$(CXX) $< -o $@

$(BIN)/baseline_weights.cpp: $(BIN)/binary2cpp $(SRC)/baseline.weights
	@mkdir -p $(@D)
	$(BIN)/binary2cpp baseline_weights < $(SRC)/baseline.weights > $@

$(BIN)/baseline_weights.o: $(BIN)/baseline_weights.cpp
	$(CXX) -c $< -o $@

AUTOSCHED_COST_MODEL_LIBS=\
$(BIN)/cost_model/anderson2021_cost_model.a \
$(BIN)/cost_model/anderson2021_train_cost_model.a \

$(BIN)/cost_model.generator: $(SRC)/cost_model_generator.cpp \
							$(SRC)/cost_model_schedule.h \
							$(SRC)/NetworkSize.h \
							$(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS) $(USE_EXPORT_DYNAMIC)

$(BIN)/auto_schedule_runtime.a: $(BIN)/cost_model.generator
	@mkdir -p $(@D)
	$^ -r auto_schedule_runtime -o $(BIN) target=$(HL_TARGET)

$(BIN)/cost_model/anderson2021_%.a: $(BIN)/cost_model.generator
	@mkdir -p $(@D)
	$^ -g $* -o $(BIN)/anderson2021_cost_model -f $* -n anderson2021_$* target=$(HL_TARGET)-no_runtime enable_debug_output=$(ENABLE_DEBUG_OUTPUT) -e stmt,static_library,h,assembly

# It's important to use dynamic lookups for undefined symbols here: all of libHalide
# is expected to be present (in the loading binary), so we explicitly make the symbols
# undefined rather than dependent on libHalide.so.
$(BIN)/libautoschedule_anderson2021.$(PLUGIN_EXT): $(SRC)/AutoSchedule.cpp \
		  							$(SRC)/AutoSchedule.h \
		  							$(COMMON_DIR)/ASLog.cpp \
										$(SRC)/DefaultCostModel.h \
										$(SRC)/DefaultCostModel.cpp \
										$(SRC)/Weights.h \
										$(SRC)/Weights.cpp \
										$(SRC)/FunctionDAG.h \
										$(SRC)/FunctionDAG.cpp \
										$(SRC)/LoopNest.h \
										$(SRC)/LoopNest.cpp \
										$(SRC)/LoopNestParser.h \
										$(SRC)/GPUMemInfo.h \
										$(SRC)/GPULoopInfo.h \
										$(SRC)/GPULoopInfo.cpp \
										$(SRC)/ThreadInfo.h \
										$(SRC)/Featurization.h \
										$(SRC)/CostModel.h \
										$(COMMON_DIR)/PerfectHashMap.h \
										$(SRC)/SearchSpace.h \
										$(SRC)/SearchSpace.cpp \
										$(SRC)/SearchSpaceOptions.h \
										$(SRC)/State.h \
										$(SRC)/State.cpp \
										$(SRC)/Statistics.h \
										$(SRC)/Tiling.h \
										$(SRC)/Tiling.cpp \
										$(AUTOSCHED_WEIGHT_OBJECTS) \
										$(AUTOSCHED_COST_MODEL_LIBS) \
										$(GENERATOR_DEPS) \
										$(BIN)/auto_schedule_runtime.a
	@mkdir -p $(@D)
	$(CXX) -shared $(USE_EXPORT_DYNAMIC) -fPIC -fvisibility=hidden -fvisibility-inlines-hidden $(CXXFLAGS) $(OPTIMIZE) -I $(BIN)/anderson2021_cost_model $(filter-out %.h $(LIBHALIDE_LDFLAGS),$^) -o $@ $(HALIDE_SYSTEM_LIBS) -I .

$(BIN)/anderson2021_retrain_cost_model: $(SRC)/retrain_cost_model.cpp \
									$(COMMON_DIR)/ASLog.cpp \
									$(SRC)/DefaultCostModel.h \
									$(SRC)/DefaultCostModel.cpp \
									$(SRC)/Weights.h \
									$(SRC)/Weights.cpp \
									$(SRC)/CostModel.h \
									$(SRC)/NetworkSize.h \
									$(AUTOSCHED_COST_MODEL_LIBS) \
									$(AUTOSCHED_WEIGHT_OBJECTS) \
									$(BIN)/auto_schedule_runtime.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -frtti -Wall -I ../support -I $(BIN)/anderson2021_cost_model $(OPTIMIZE) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS) $(USE_OPEN_MP)

$(BIN)/anderson2021_weightsdir_to_weightsfile: $(SRC)/weightsdir_to_weightsfile.cpp $(SRC)/Weights.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ $(OPTIMIZE) -o $@ -I .

# A sample generator to autoschedule. Note that if it statically links
# to libHalide, then it must be build with $(USE_EXPORT_DYNAMIC), or the
# autoscheduler can't find the libHalide symbols that it needs.
$(GENERATOR_BIN)/anderson2021_demo.generator: $(HALIDE_TEST_DIR)/demo_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -g $(filter %.cpp,$^) -o $@ $(LIBHALIDE_LDFLAGS)

# To use the autoscheduler, set a few environment variables and use the -p flag to the generator to load the autoscheduler as a plugin
$(BIN)/%/anderson2021_demo.a: $(GENERATOR_BIN)/anderson2021_demo.generator $(BIN)/libautoschedule_anderson2021.$(PLUGIN_EXT)
	@mkdir -p $(@D)
	HL_WEIGHTS_DIR=$(SRC)/baseline.weights \
	$(GENERATOR_BIN)/anderson2021_demo.generator -g demo -o $(@D) -f anderson2021_demo target=$* -p $(BIN)/libautoschedule_anderson2021.$(PLUGIN_EXT) autoscheduler=Anderson2021

$(BIN)/%/anderson2021_demo.rungen: $(BIN)/%/RunGenMain.o $(BIN)/%/anderson2021_demo.registration.cpp $(BIN)/%/anderson2021_demo.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* $^ -o $@ $(HALIDE_SYSTEM_LIBS) $(IMAGE_IO_FLAGS)

# demonstrates single-shot use of the autoscheduler
anderson2021_demo: $(BIN)/$(HL_TARGET)/anderson2021_demo.rungen $(BIN)/libautoschedule_anderson2021.$(PLUGIN_EXT)
	$< --benchmarks=all --benchmark_min_time=1 --estimate_all

$(BIN)/featurization_to_sample $(BIN)/get_host_target:
	@mkdir -p $(@D)
	$(MAKE) -f $(COMMON_DIR)/Makefile $(BIN)/featurization_to_sample $(BIN)/get_host_target

# demonstrates an autotuning loop
# (using $(BIN) and $(SRC) here seems overkill, but makes copy-n-paste elsewhere easier)
autotune: $(GENERATOR_BIN)/anderson2021_demo.generator $(BIN)/featurization_to_sample $(BIN)/get_host_target $(BIN)/anderson2021_retrain_cost_model $(BIN)/libautoschedule_anderson2021.$(PLUGIN_EXT) $(SRC)/anderson2021_autotune_loop.sh
	SAMPLES_DIR=test_autotuned_samples \
	bash $(SRC)/anderson2021_autotune_loop.sh \
		$(GENERATOR_BIN)/anderson2021_demo.generator \
		anderson2021_demo \
		"" \
		$(SRC)/baseline.weights \
		$(BIN) \
		0

$(BIN)/anderson2021_test_function_dag: test_function_dag.cpp FunctionDAG.h FunctionDAG.cpp $(COMMON_DIR)/ASLog.h $(COMMON_DIR)/ASLog.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS) $(HALIDE_SYSTEM_LIBS)

# Simple jit-based test
$(BIN)/$(HL_TARGET)/test: $(HALIDE_TEST_DIR)/test.cpp $(BIN)/libautoschedule_anderson2021.$(PLUGIN_EXT)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) $^ -o $@ $(LIBHALIDE_LDFLAGS) $(HALIDE_SYSTEM_LIBS)

anderson2021_test_function_dag: $(BIN)/anderson2021_test_function_dag
	$^

run_test: $(BIN)/$(HL_TARGET)/test $(BIN)/libautoschedule_anderson2021.$(PLUGIN_EXT)
	HL_WEIGHTS_DIR=$(SRC)/baseline.weights LD_LIBRARY_PATH=$(BIN) $< $(BIN)/libautoschedule_anderson2021.$(PLUGIN_EXT)

.PHONY: test clean

# Note that when running the *test*, we want to ensure that we generate samples
# to a subdir of $(BIN), so that they don't get inadvertently generated into
# our source tree. (Normally we want samples/ to be retained, to avoid data loss;
# for the test target, however, it's imperative it go into a transitory directory,
# to avoid eventually consuming all disk space on the buildbot...)
test: AUTOSCHED_SAMPLES_OUT = $(BIN)/test_samples_out

# Note that 'make build' and 'make test' is used by Halide buildbots
# to spot-check changes, so it's important to try a little of each of
# the important paths here, including single-shot and autotune-loop
build: $(BIN)/$(HL_TARGET)/test \
	$(BIN)/anderson2021_test_function_dag \
	$(BIN)/$(HL_TARGET)/included_schedule_file.rungen \
	$(GENERATOR_BIN)/anderson2021_demo.generator \
	$(BIN)/anderson2021_retrain_cost_model \
	$(BIN)/libautoschedule_anderson2021.$(PLUGIN_EXT)

test: test_bounds test_tiling test_storage_strides test_parser test_state test_thread_info run_test anderson2021_test_function_dag anderson2021_demo included_schedule_file autotune

TEST_DIR=$(SRC)/test

$(BIN)/test_bounds: $(TEST_DIR)/bounds.cpp LoopNest.h LoopNest.cpp FunctionDAG.cpp FunctionDAG.h $(COMMON_DIR)/ASLog.h $(COMMON_DIR)/ASLog.cpp GPULoopInfo.cpp GPULoopInfo.h GPUMemInfo.h Tiling.h Tiling.cpp
	@mkdir -p $(@D)
	$(CXX) $(OPTIMIZE) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS) $(HALIDE_SYSTEM_LIBS) -I$(SRC)

test_bounds: $(BIN)/test_bounds
	$^

$(BIN)/test_tiling: $(TEST_DIR)/tiling.cpp Tiling.h Tiling.cpp
	@mkdir -p $(@D)
	$(CXX) $(OPTIMIZE) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS) $(HALIDE_SYSTEM_LIBS) -I$(SRC)

test_tiling: $(BIN)/test_tiling
	$^

$(BIN)/test_storage_strides: $(TEST_DIR)/storage_strides.cpp LoopNest.h LoopNest.cpp FunctionDAG.cpp FunctionDAG.h $(COMMON_DIR)/ASLog.h $(COMMON_DIR)/ASLog.cpp GPULoopInfo.cpp GPULoopInfo.h GPUMemInfo.h Tiling.h Tiling.cpp
	@mkdir -p $(@D)
	$(CXX) $(OPTIMIZE) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS) $(HALIDE_SYSTEM_LIBS) -I$(SRC)

test_storage_strides: $(BIN)/test_storage_strides
	$^

$(BIN)/test_parser: $(TEST_DIR)/parser.cpp LoopNestParser.h $(COMMON_DIR)/ASLog.h $(COMMON_DIR)/ASLog.cpp
	@mkdir -p $(@D)
	$(CXX) $(OPTIMIZE) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS) $(HALIDE_SYSTEM_LIBS) -I$(SRC)

test_parser: $(BIN)/test_parser
	$^

$(BIN)/test_state: $(TEST_DIR)/state.cpp State.h State.cpp LoopNest.h LoopNest.cpp FunctionDAG.cpp FunctionDAG.h $(COMMON_DIR)/ASLog.h $(COMMON_DIR)/ASLog.cpp GPULoopInfo.cpp GPULoopInfo.h GPUMemInfo.h Tiling.h Tiling.cpp
	@mkdir -p $(@D)
	$(CXX) $(OPTIMIZE) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS) $(HALIDE_SYSTEM_LIBS) -I$(SRC)

test_state: $(BIN)/test_state
	$^

$(BIN)/test_thread_info: $(TEST_DIR)/thread_info.cpp LoopNest.h LoopNest.cpp FunctionDAG.cpp FunctionDAG.h $(COMMON_DIR)/ASLog.h $(COMMON_DIR)/ASLog.cpp GPULoopInfo.cpp GPULoopInfo.h GPUMemInfo.h Tiling.h Tiling.cpp
	@mkdir -p $(@D)
	$(CXX) $(OPTIMIZE) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS) $(HALIDE_SYSTEM_LIBS) -I$(SRC)

test_thread_info: $(BIN)/test_thread_info
	$^

clean:
	rm -rf $(BIN)

# A sample generator to demonstrate including autogenerated .sample.h
# files for scheduling purposes; the catch here is that we'll need
# to be able to compile the Generator two different ways:
#
#   - one that will be used to generate the .schedule.h
#   - one that will consume the .schedule.h generated above
#
# We'll use the preprocessor (GENERATING_SCHEDULE) to distinguish between these two.

$(GENERATOR_BIN)/included_schedule_file_none.generator: $(HALIDE_TEST_DIR)/included_schedule_file_generator.cpp $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -DGENERATING_SCHEDULE -g $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS) $(HALIDE_SYSTEM_LIBS)

# This is the target you build to (re)generate the schedule file.
# (Note that we only need the schedule output, so we pass `-e schedule` to
# the Generator so that it can skip producing other outputs.)
$(BIN)/%/included_schedule_file.schedule.h: $(GENERATOR_BIN)/included_schedule_file_none.generator $(BIN)/libautoschedule_anderson2021.$(PLUGIN_EXT)
	@mkdir -p $(@D)
	HL_WEIGHTS_DIR=$(SRC)/baseline.weights \
	$< -g included_schedule_file -o $(@D) -f included_schedule_file target=$* -p $(BIN)/libautoschedule_anderson2021.$(PLUGIN_EXT) autoscheduler=Anderson2021 autoscheduler.parallelism=80 -e schedule

# Note that this depends on $(HALIDE_TEST_DIR)/included_schedule_file.schedule.h rather than $(BIN)/%/included_schedule_file.schedule.h --
# the former should be generated by something like
#
#    make bin/host/included_schedule_file.schedule.h
#    cp bin/host/included_schedule_file.schedule.h included_schedule_file.schedule.h
#
$(GENERATOR_BIN)/included_schedule_file.generator: $(HALIDE_TEST_DIR)/included_schedule_file_generator.cpp $(HALIDE_TEST_DIR)/included_schedule_file.schedule.h $(GENERATOR_DEPS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(USE_EXPORT_DYNAMIC) -g $(filter-out %.h,$^) -o $@ $(LIBHALIDE_LDFLAGS) $(HALIDE_SYSTEM_LIBS)

# Note that this does not depend on libauto_schedule nor does it call
# the autoscheduler at build time; it includes the generated schedule (included_schedule_file.schedule.h),
# which has been added to our local source control.
$(BIN)/%/included_schedule_file.a: $(GENERATOR_BIN)/included_schedule_file.generator
	@mkdir -p $(@D)
	$< -g included_schedule_file -o $(@D) -f included_schedule_file target=$*

$(BIN)/%/included_schedule_file.rungen: $(BIN)/%/RunGenMain.o $(BIN)/%/included_schedule_file.registration.cpp $(BIN)/%/included_schedule_file.a
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -I$(BIN)/$* $^ -o $@ $(HALIDE_SYSTEM_LIBS) $(IMAGE_IO_FLAGS)

included_schedule_file: $(BIN)/$(HL_TARGET)/included_schedule_file.rungen
	$^ --benchmarks=all --benchmark_min_time=1 --estimate_all
