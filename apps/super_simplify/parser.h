#ifndef PARSER_COMMON_H
#define PARSER_COMMON_H

#include <string>

#include "Halide.h"

// Helper routines for writing a parser and routines for parsing
// Halide Exprs.

// Move the input cursor past any whitespace, but not beyond the end
// pointer.
void consume_whitespace(const char **cursor, const char *end);

// If the input cursor starts with the expected string, update it to
// point to the end of the string and return true. Otherwise, return
// false and don't modify the input cursor.
bool consume(const char **cursor, const char *end, const char *expected);

// Calls consume and asserts that it succeeded.
void expect(const char **cursor, const char *end, const char *pattern);

// Consume and return a legal Halide identifier.
std::string consume_token(const char **cursor, const char *end);

// Consume and return a constant integer.
int64_t consume_int(const char **cursor, const char *end);

// Consume and return a constant float as a constant Halide Expr of
// the appropriate type.
Halide::Expr consume_float(const char **cursor, const char *end);

// Parse a full Halide Expr, as produced by a Halide IRPrinter elsewhere.
Halide::Expr parse_halide_expr(const char **cursor, const char *end, Halide::Type expected_type);

std::vector<Halide::Expr> parse_halide_exprs_from_file(const std::string &filename);
#endif
