include ../support/Makefile.inc

OBJECTS = $(BIN)/parser.o $(BIN)/z3.o $(BIN)/expr_util.o $(BIN)/reduction_order.o \
					$(BIN)/synthesize_predicate.o $(BIN)/super_simplify.o $(BIN)/generate_bounds_cegis.o \
					$(BIN)/ExprToJuliaLP.o $(BIN)/tropical_optimization.o $(BIN)/bounds_simplify.o \
					$(BIN)/bounds_incremental.o

all: $(BIN)/find_rules $(BIN)/synthesize_predicates $(BIN)/filter_rewrite_rules $(BIN)/super_simplify

CXXFLAGS += -O3 -g

LDFLAGS += -L ../../distrib/bin -L ../../distrib/lib -Wl,-rpath ../../distrib/lib

STATIC_LDFLAGS = $(filter-out -lpthread,$(LDFLAGS)) -pthread -Wl,--whole-archive -lpthread -Wl,--no-whole-archive

$(BIN)/%.o: %.cpp %.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN)/find_rules: find_rules.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LDFLAGS) -lHalide

$(BIN)/find_bounds_rules: find_bounds_rules.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LDFLAGS) -lHalide

$(BIN)/make_julia_lp: make_julia_lp.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LDFLAGS) -lHalide

$(BIN)/tropical_optimization: tropical_optimization.cpp tropical_optimization.h $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LDFLAGS) -lHalide

$(BIN)/find_bounds_tool: find_bounds_tool.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LDFLAGS) -lHalide

$(BIN)/bounds_simplify_tool: bounds_simplify_tool.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LDFLAGS) -lHalide

$(BIN)/bounds_incremental_tool: bounds_incremental_tool.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LDFLAGS) -lHalide

# Statically-linked version for use on a cluster
$(BIN)/find_rules_static: find_rules.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) -static $(CXXFLAGS) -O1 $< $(OBJECTS) -o $@ $(STATIC_LDFLAGS) -lHalide

$(BIN)/synthesize_predicates: synthesize_predicates.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LDFLAGS) -lHalide

$(BIN)/filter_rewrite_rules: filter_rewrite_rules.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LDFLAGS) -lHalide

$(BIN)/super_simplify: super_simplify_tool.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LDFLAGS) -lHalide

$(BIN)/enumerate_exprs: enumerate_exprs.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LDFLAGS) -lHalide

$(BIN)/anonymize_exprs: anonymize_exprs.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LDFLAGS) -lHalide

$(BIN)/simplify_benchmark: simplify_benchmark.cpp $(BIN)/parser.o $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(BIN)/parser.o -o $@ $(LDFLAGS) -lHalide

clean:
	rm -rf $(BIN)
