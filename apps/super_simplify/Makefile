include ../support/Makefile.inc

OBJECTS = $(BIN)/parser.o $(BIN)/z3.o $(BIN)/expr_util.o $(BIN)/synthesize_predicate.o $(BIN)/super_simplify.o

all: $(BIN)/find_rules $(BIN)/synthesize_predicates $(BIN)/filter_rewrite_rules

CXXFLAGS += -g

$(BIN)/%.o: %.cpp %.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN)/find_rules: find_rules.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS)

$(BIN)/synthesize_predicates: synthesize_predicates.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) 

$(BIN)/filter_rewrite_rules: filter_rewrite_rules.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS)

clean:
	rm -rf $(BIN)
