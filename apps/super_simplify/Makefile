include ../support/Makefile.inc

OBJECTS = find_rules.o parser.o z3.o expr_util.o synthesize_predicate.o

all: $(BIN)/find_rules

%.o: %.cpp %.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN)/find_rules: $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS)

clean:
	rm -rf $(BIN)
