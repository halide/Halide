cmake_minimum_required(VERSION 3.28)
project(simplifier_rule_verifier)

enable_testing()

# Set up language settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS NO)

# Find Halide
find_package(Halide REQUIRED)

add_library(
    rule_verifier_support STATIC
    expr_util.cpp
    parser.cpp
    reduction_order.cpp
    super_simplify.cpp
    z3.cpp
)
target_link_libraries(rule_verifier_support PUBLIC Halide::Halide Halide::Tools)
target_include_directories(rule_verifier_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(filter_rewrite_rules filter_rewrite_rules.cpp)
target_link_libraries(filter_rewrite_rules PRIVATE rule_verifier_support)

add_executable(super_simplify super_simplify_tool.cpp)
target_link_libraries(super_simplify PRIVATE rule_verifier_support)

# Both tools shell out to z3, so there's nothing to test without it.
find_program(Z3_EXECUTABLE z3)
if (NOT Z3_EXECUTABLE)
    message(STATUS "z3 not found; skipping the apps/simplifier_rule_verifier tests")
    return()
endif ()

add_test(
    NAME rule_verifier_good_rules
    COMMAND filter_rewrite_rules ${CMAKE_CURRENT_SOURCE_DIR}/test/good_rules.txt
)
set_tests_properties(
    rule_verifier_good_rules
    PROPERTIES
    ENVIRONMENT "HL_Z3=${Z3_EXECUTABLE}"
    LABELS simplifier_rule_verifier
    PASS_REGULAR_EXPRESSION
        "0 rule\\(s\\) were disproved by z3\n0 rule\\(s\\) could not be verified by z3\n0 rule\\(s\\) did not obey the reduction order\nSuccess!"
)

# These rules are supposed to be rejected, so the tool is expected to fail. Pin
# down the number of each sort of failure, so that a rule slipping through
# unnoticed is a test failure too.
add_test(
    NAME rule_verifier_bad_rules
    COMMAND filter_rewrite_rules ${CMAKE_CURRENT_SOURCE_DIR}/test/bad_rules.txt
)
set_tests_properties(
    rule_verifier_bad_rules
    PROPERTIES
    ENVIRONMENT "HL_Z3=${Z3_EXECUTABLE}"
    LABELS simplifier_rule_verifier
    PASS_REGULAR_EXPRESSION
        "2 rule\\(s\\) were disproved by z3\n0 rule\\(s\\) could not be verified by z3\n8 rule\\(s\\) did not obey the reduction order\nFailure!"
)

# Every rule here is only true if the parser reads it with the intended
# precedence and associativity, so a regression in the grammar shows up as a
# rule z3 can disprove.
add_test(
    NAME rule_verifier_parser_rules
    COMMAND filter_rewrite_rules ${CMAKE_CURRENT_SOURCE_DIR}/test/parser_rules.txt
)
set_tests_properties(
    rule_verifier_parser_rules
    PROPERTIES
    ENVIRONMENT "HL_Z3=${Z3_EXECUTABLE}"
    LABELS simplifier_rule_verifier
    PASS_REGULAR_EXPRESSION
        "0 rule\\(s\\) were disproved by z3\n0 rule\\(s\\) could not be verified by z3\n0 rule\\(s\\) did not obey the reduction order\nSuccess!"
)

add_test(
    NAME rule_verifier_narrow_int_rules
    COMMAND filter_rewrite_rules ${CMAKE_CURRENT_SOURCE_DIR}/test/narrow_int_rules.txt
)
set_tests_properties(
    rule_verifier_narrow_int_rules
    PROPERTIES
    ENVIRONMENT "HL_Z3=${Z3_EXECUTABLE}"
    LABELS simplifier_rule_verifier
    PASS_REGULAR_EXPRESSION
        "0 rule\\(s\\) were disproved by z3\n0 rule\\(s\\) could not be verified by z3\n0 rule\\(s\\) did not obey the reduction order\nSuccess!"
)

add_test(
    NAME rule_verifier_synthesize_predicates
    COMMAND filter_rewrite_rules ${CMAKE_CURRENT_SOURCE_DIR}/test/rules_needing_predicates.txt
)
set_tests_properties(
    rule_verifier_synthesize_predicates
    PROPERTIES
    ENVIRONMENT "HL_Z3=${Z3_EXECUTABLE}"
    LABELS simplifier_rule_verifier
    PASS_REGULAR_EXPRESSION "rewrite\\(min\\(x\\*c0, y\\*c0\\), min\\(x, y\\)\\*c0, 0 <= c0\\)"
)

add_test(
    NAME rule_verifier_super_simplify
    COMMAND super_simplify ${CMAKE_CURRENT_SOURCE_DIR}/test/exprs.txt 4
)
set_tests_properties(
    rule_verifier_super_simplify
    PROPERTIES
    ENVIRONMENT "HL_Z3=${Z3_EXECUTABLE}"
    LABELS simplifier_rule_verifier
    PASS_REGULAR_EXPRESSION "select\\(x < y, x, y\\) -> min\\(x, y\\)"
)
