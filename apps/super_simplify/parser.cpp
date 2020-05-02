#include "parser.h"

#include <fstream>
#include <stdio.h>

using std::map;
using std::ostringstream;
using std::string;
using std::vector;
using namespace Halide;
using namespace Halide::Internal;

bool is_whitespace(char c) {
    return c == ' ' || c == '\n' || c == '\t';
}

void consume_whitespace(const char **cursor, const char *end) {
    while (*cursor < end && is_whitespace(**cursor)) {
        (*cursor)++;
    }
}

bool consume(const char **cursor, const char *end, const char *expected) {
    const char *tmp = *cursor;
    while (*tmp == *expected && tmp < end && *expected) {
        tmp++;
        expected++;
    }
    if ((*expected) == 0) {
        *cursor = tmp;
        return true;
    } else {
        return false;
    }
}

void expect(const char **cursor, const char *end, const char *pattern) {
    if (!consume(cursor, end, pattern)) {
        printf("Parsing failed. Expected %s, got %s\n",
               pattern, *cursor);
        abort();
    }
}

bool check(const char **cursor, const char *end, const char *pattern) {
    const char *tmp_cursor = *cursor;
    return consume(&tmp_cursor, end, pattern);
}

string consume_token(const char **cursor, const char *end) {
    size_t sz = 0;
    while (*cursor + sz < end &&
           (std::isalnum((*cursor)[sz]) ||
            (*cursor)[sz] == '!' ||
            (*cursor)[sz] == '.' ||
            (*cursor)[sz] == '$' ||
            (*cursor)[sz] == '_'))
        sz++;
    string result{*cursor, sz};
    *cursor += sz;
    return result;
}

int64_t consume_int(const char **cursor, const char *end) {
    bool negative = consume(cursor, end, "-");
    int64_t n = 0;
    while (*cursor < end && **cursor >= '0' && **cursor <= '9') {
        n *= 10;
        n += (**cursor - '0');
        (*cursor)++;
    }
    return negative ? -n : n;
}

Expr consume_float(const char **cursor, const char *end) {
    bool negative = consume(cursor, end, "-");
    int64_t integer_part = consume_int(cursor, end);
    int64_t fractional_part = 0;
    int64_t denom = 1;
    if (consume(cursor, end, ".")) {
        while (*cursor < end && **cursor >= '0' && **cursor <= '9') {
            denom *= 10;
            fractional_part *= 10;
            fractional_part += (**cursor - '0');
            (*cursor)++;
        }
    }
    double d = integer_part + double(fractional_part) / denom;
    if (negative) {
        d = -d;
    }
    if (consume(cursor, end, "h")) {
        return make_const(Float(16), d);
    } else if (consume(cursor, end, "f")) {
        return make_const(Float(32), d);
    } else {
        return make_const(Float(64), d);
    }
}

class Parser {
    const char *cursor, *end;
    std::vector<std::pair<Expr, int>> stack;
    map<string, Type> var_types;

    void consume_whitespace() {
        ::consume_whitespace(&cursor, end);
    }

    bool consume(const char *str) {
        return ::consume(&cursor, end, str);
    }

    void expect(const char *str) {
        ::expect(&cursor, end, str);
    }

    int64_t consume_int() {
        return ::consume_int(&cursor, end);
    }

    Expr consume_float() {
        return ::consume_float(&cursor, end);
    }

    string consume_token() {
        return ::consume_token(&cursor, end);
    }

    char peek() const {
        return *cursor;
    }

public:
    Expr reparse_as_bool(const Expr &e) {
        const Call *op = e.as<Call>();
        if (e.type().is_bool()) {
            return e;
        } else if (const Variable *var = e.as<Variable>()) {
            return Variable::make(Bool(), var->name);
        } else if (op &&
                   (op->is_intrinsic(Call::likely) ||
                    op->is_intrinsic(Call::likely_if_innermost))) {
            return Call::make(Bool(), op->name, {reparse_as_bool(op->args[0])}, op->call_type);
        } else if (is_zero(e)) {
            return const_false();
        } else if (is_one(e)) {
            return const_true();
        } else {
            std::cerr << "Expected bool Expr: " << e << "\n";
            abort();
            return Expr();
        }
    }

    Expr parse_halide_expr(int precedence) {
        if (!stack.empty() && stack.back().second <= precedence) {
            Expr a = stack.back().first;
            stack.pop_back();
            return a;
        }

        struct TypePattern {
            const char *cast_prefix = nullptr;
            const char *constant_prefix = nullptr;
            Type type;
            string cast_prefix_storage, constant_prefix_storage;
            TypePattern(Type t) {
                ostringstream cast_prefix_stream, constant_prefix_stream;
                cast_prefix_stream << t << '(';
                cast_prefix_storage = cast_prefix_stream.str();
                cast_prefix = cast_prefix_storage.c_str();

                constant_prefix_stream << '(' << t << ')';
                constant_prefix_storage = constant_prefix_stream.str();
                constant_prefix = constant_prefix_storage.c_str();
                type = t;
            }
        };
        static TypePattern typenames[] = {{UInt(1)},
                                          {Int(8)},
                                          {UInt(8)},
                                          {Int(16)},
                                          {UInt(16)},
                                          {Int(32)},
                                          {UInt(32)},
                                          {Int(64)},
                                          {UInt(64)},
                                          {Float(64)},
                                          {Float(32)}};

        consume_whitespace();

        if (precedence == 10) {
            // type-cast
            for (auto t : typenames) {
                if (consume(t.cast_prefix)) {
                    Expr a = cast(t.type, parse_halide_expr(0));
                    expect(")");
                    return a;
                }
            }

            // Let binding. Always has parens
            if (consume("(let ")) {
                string name = consume_token();
                consume_whitespace();
                expect("=");
                consume_whitespace();

                Expr value = parse_halide_expr(0);

                consume_whitespace();
                expect("in");
                consume_whitespace();

                var_types[name] = value.type();

                Expr body = parse_halide_expr(0);

                Expr a = Let::make(name, value, body);
                expect(")");
                return a;
            }
            if (consume("min(")) {
                Expr a = parse_halide_expr(0);
                expect(",");
                Expr b = parse_halide_expr(0);
                consume_whitespace();
                expect(")");
                return min(a, b);
            }
            if (consume("max(")) {
                Expr a = parse_halide_expr(0);
                expect(",");
                Expr b = parse_halide_expr(0);
                consume_whitespace();
                expect(")");
                return max(a, b);
            }
            if (consume("select(")) {
                Expr a = parse_halide_expr(0);
                a = reparse_as_bool(a);
                expect(",");
                Expr b = parse_halide_expr(0);
                expect(",");
                Expr c = parse_halide_expr(0);
                consume_whitespace();
                expect(")");
                if (b.type().is_bool() && !c.type().is_bool()) {
                    c = reparse_as_bool(c);
                } else if (!b.type().is_bool() && c.type().is_bool()) {
                    b = reparse_as_bool(b);
                }

                return select(a, b, c);
            }
            Call::IntrinsicOp binary_intrinsics[] = {Call::bitwise_and,
                                                     Call::bitwise_or,
                                                     Call::shift_left,
                                                     Call::shift_right};
            for (auto intrin : binary_intrinsics) {
                if (consume(Call::get_intrinsic_name(intrin))) {
                    expect("(");
                    Expr a = parse_halide_expr(0);
                    expect(",");
                    Expr b = parse_halide_expr(0);
                    consume_whitespace();
                    expect(")");
                    return Call::make(a.type(), intrin, {a, b}, Call::PureIntrinsic);
                }
            }

            if (consume("fold(")) {
                // strip folds
                Expr e = parse_halide_expr(0);
                expect(")");
                return e;
            }

            if (consume("!")) {
                Expr e = parse_halide_expr(precedence);
                e = reparse_as_bool(e);
                return !e;
            }

            // Parse entire rewrite rules as exprs
            if (consume("rewrite(")) {
                Expr lhs = parse_halide_expr(0);
                expect(",");
                Expr rhs = parse_halide_expr(0);
                if (lhs.type().is_bool()) {
                    rhs = reparse_as_bool(rhs);
                }
                if (rhs.type().is_bool()) {
                    lhs = reparse_as_bool(lhs);
                }
                Expr predicate = const_true();
                consume_whitespace();
                if (consume(",")) {
                    predicate = parse_halide_expr(0);
                    predicate = reparse_as_bool(predicate);
                }
                expect(")");
                return Call::make(Bool(), "rewrite", {lhs, rhs, predicate}, Call::Extern);
            }

            if (consume("round_f32(")) {
                Expr a = parse_halide_expr(0);
                expect(")");
                return round(a);
            }
            if (consume("ceil_f32(")) {
                Expr a = parse_halide_expr(0);
                expect(")");
                return ceil(a);
            }
            if (consume("floor_f32(")) {
                Expr a = parse_halide_expr(0);
                expect(")");
                return floor(a);
            }
            if (consume("likely(")) {
                Expr a = parse_halide_expr(0);
                expect(")");
                return likely(a);
            }
            if (consume("likely_if_innermost(")) {
                Expr a = parse_halide_expr(0);
                expect(")");
                return likely(a);
            }

            Type expected_type = Int(32);
            for (auto t : typenames) {
                // A type annotation for the token that follows
                if (consume(t.constant_prefix)) {
                    expected_type = t.type;
                }
            }

            // An expression in parens
            if (consume("(")) {
                Expr e = parse_halide_expr(0);
                expect(")");
                return e;
            }

            // Constants
            if ((peek() >= '0' && peek() <= '9') || peek() == '-') {
                const char *tmp = cursor;
                Expr e = make_const(Int(32), consume_int());
                if (peek() == '.') {
                    // Rewind and parse as float instead
                    cursor = tmp;
                    e = consume_float();
                }
                return e;
            }
            if (consume("true")) {
                return const_true();
            }
            if (consume("false")) {
                return const_false();
            }

            // Variables, loads, and calls
            if ((peek() >= 'a' && peek() <= 'z') ||
                (peek() >= 'A' && peek() <= 'Z') ||
                peek() == '$' ||
                peek() == '_' ||
                peek() == '.') {
                string name = consume_token();
                if (consume("[")) {
                    Expr index = parse_halide_expr(0);
                    expect("]");
                    if (expected_type == Type{}) {
                        expected_type = Int(32);
                    }
                    return Load::make(expected_type, name, index, Buffer<>(),
                                      Parameter(), const_true(), ModulusRemainder());
                } else if (consume("(")) {
                    vector<Expr> args;
                    while (1) {
                        consume_whitespace();
                        if (consume(")")) break;
                        args.push_back(parse_halide_expr(0));
                        consume_whitespace();
                        consume(",");
                    }
                    return Call::make(expected_type, name, args, Call::PureExtern);
                } else {
                    auto it = var_types.find(name);
                    if (it != var_types.end()) {
                        expected_type = it->second;
                    }
                    if (expected_type == Type{}) {
                        expected_type = Int(32);
                    }
                    return Variable::make(expected_type, name);
                }
            }

            for (auto p : stack) {
                std::cerr << p.first << " " << p.second << "\n";
            }

            std::cerr << "Failed to parse starting at: " << *cursor << "\n";
            abort();
            return Expr();

        } else if (precedence == 9) {
            // Multiplicative things

            Expr a = parse_halide_expr(precedence + 1);
            Expr result;
            while (1) {
                consume_whitespace();
                if (consume("*")) {
                    a *= parse_halide_expr(precedence + 1);
                } else if (consume("/")) {
                    a /= parse_halide_expr(precedence + 1);
                } else if (consume("%")) {
                    a = a % parse_halide_expr(precedence + 1);
                } else {
                    stack.emplace_back(a, precedence + 1);
                    break;
                }
            }
        } else if (precedence == 8) {
            // Additive things

            Expr a = parse_halide_expr(precedence + 1);
            Expr result;
            while (1) {
                consume_whitespace();
                if (consume("+")) {
                    a += parse_halide_expr(precedence + 1);
                } else if (consume("-")) {
                    a -= parse_halide_expr(precedence + 1);
                } else {
                    stack.emplace_back(a, precedence + 1);
                    break;
                }
            }
        } else if (precedence == 7) {
            // Comparisons

            Expr a = parse_halide_expr(precedence + 1);
            Expr result;
            consume_whitespace();
            if (consume("<=")) {
                return a <= parse_halide_expr(precedence);
            } else if (consume(">=")) {
                return a >= parse_halide_expr(precedence);
            } else if (consume("<")) {
                return a < parse_halide_expr(precedence);
            } else if (consume(">")) {
                return a > parse_halide_expr(precedence);
            } else if (consume("==")) {
                return a == parse_halide_expr(precedence);
            } else if (consume("!=")) {
                return a != parse_halide_expr(precedence);
            } else {
                stack.emplace_back(a, precedence + 1);
            }
        } else if (precedence == 6) {
            // Logical and
            Expr a = parse_halide_expr(precedence + 1);
            Expr result;
            if (consume("&&")) {
                Expr b = parse_halide_expr(precedence);
                a = reparse_as_bool(a);
                b = reparse_as_bool(b);
                return a && b;
            } else {
                stack.emplace_back(a, precedence + 1);
            }
        } else if (precedence == 5) {
            // Logical or
            Expr a = parse_halide_expr(precedence + 1);
            Expr result;
            if (consume("||")) {
                Expr b = parse_halide_expr(precedence);
                a = reparse_as_bool(a);
                b = reparse_as_bool(b);
                return a || b;
            } else {
                stack.emplace_back(a, 6);
            }
        }

        // Try increasing precedence
        return parse_halide_expr(precedence + 1);
    }

    Parser(const char *c, const char *e)
        : cursor(c), end(e) {
    }
};

Expr parse_halide_expr(const char *cursor, const char *end, Type expected_type) {
    Parser parser(cursor, end);
    Expr result = parser.parse_halide_expr(0);
    if (expected_type.is_bool()) {
        result = parser.reparse_as_bool(result);
    }
    return result;
}

vector<Expr> parse_halide_exprs_from_file(const std::string &filename) {
    vector<Expr> exprs;
    std::ifstream input;
    input.open(filename);
    if (input.fail()) {
        debug(0) << "parse_halide_exprs_from_file: Unable to open " << filename;
        assert(false);
    }
    for (string line; std::getline(input, line);) {
        if (line.empty()) continue;
        // It's possible to comment out lines for debugging
        if (line[0] == '#') continue;  // for python-style comments
        if (line[0] == '/') continue;  // for //
        if (line[0] == '*') continue;  // for */

        // There are some extraneous newlines in some of the files. Balance parentheses...
        size_t open, close;
        while (1) {
            open = std::count(line.begin(), line.end(), '(');
            close = std::count(line.begin(), line.end(), ')');
            if (open <= close) break;
            string next;
            debug(0) << "Unbalanced parens in :\n\n"
                     << line << "\n\n";
            assert(std::getline(input, next));
            line += next;
        }
        const char *start = &line[0];
        const char *end = &line[line.size()];
        debug(1) << "Parsing: " << line << "\n";
        exprs.push_back(parse_halide_expr(start, end, Type{}));
    }

    return exprs;
}
