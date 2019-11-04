include ../support/Makefile.inc

OBJECTS = $(BIN)/parser.o $(BIN)/z3.o $(BIN)/expr_util.o $(BIN)/synthesize_predicate.o $(BIN)/super_simplify.o

all: $(BIN)/find_rules $(BIN)/synthesize_predicates $(BIN)/filter_rewrite_rules $(BIN)/super_simplify

CXXFLAGS += -g

LDFLAGS += -L ../../distrib/bin -L ../../distrib/lib -rpath ../../distrib/bin 

$(BIN)/%.o: %.cpp %.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN)/find_rules: find_rules.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LDFLAGS) -lHalide

# Statically-linked version for use on a cluster
$(BIN)/find_rules_static: find_rules.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) -static $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LDFLAGS) -lHalide

$(BIN)/synthesize_predicates: synthesize_predicates.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LDFLAGS) -lHalide

$(BIN)/filter_rewrite_rules: filter_rewrite_rules.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LDFLAGS) -lHalide

$(BIN)/super_simplify: super_simplify_tool.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LDFLAGS) -lHalide

clean:
	rm -rf $(BIN)
