include ../support/Makefile.inc

CXXFLAGS += -O2 -g

OBJECTS = $(BIN)/expr_util.o $(BIN)/parser.o $(BIN)/reduction_order.o \
          $(BIN)/super_simplify.o $(BIN)/z3.o

all: $(BIN)/filter_rewrite_rules $(BIN)/super_simplify

$(BIN)/%.o: %.cpp %.h
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN)/filter_rewrite_rules: filter_rewrite_rules.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LIBHALIDE_LDFLAGS)

$(BIN)/super_simplify: super_simplify_tool.cpp $(OBJECTS) $(LIB_HALIDE)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $< $(OBJECTS) -o $@ $(LIBHALIDE_LDFLAGS)

test: $(BIN)/filter_rewrite_rules $(BIN)/super_simplify
	$(BIN)/filter_rewrite_rules test/good_rules.txt
	! $(BIN)/filter_rewrite_rules test/bad_rules.txt
	$(BIN)/filter_rewrite_rules test/parser_rules.txt
	$(BIN)/filter_rewrite_rules test/narrow_int_rules.txt
	$(BIN)/filter_rewrite_rules test/rules_needing_predicates.txt
	$(BIN)/super_simplify test/exprs.txt 4

clean:
	rm -rf $(BIN)
