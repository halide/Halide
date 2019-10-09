#include "parser.h"

#include <stdio.h>

using std::string;
using std::ostringstream;
using namespace Halide;
using namespace Halide::Internal;

bool is_whitespace(char c) {
    return c == ' '  || c == '\n' || c == '\t';
}

void consume_whitespace(char **cursor, char *end) {
    while (*cursor < end && is_whitespace(**cursor)) {
        (*cursor)++;
    }
}

bool consume(char **cursor, char *end, const char *expected) {
    char *tmp = *cursor;
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

void expect(char **cursor, char *end, const char *pattern) {
    if (!consume(cursor, end, pattern)) {
        printf("Parsing failed. Expected %s, got %s\n",
               pattern, *cursor);
        abort();
    }
}

bool check(char **cursor, char *end, const char *pattern) {
    char *tmp_cursor = *cursor;
    return consume(&tmp_cursor, end, pattern);
}

string consume_token(char **cursor, char *end) {
    size_t sz = 0;
    while (*cursor + sz < end &&
           (std::isalnum((*cursor)[sz]) ||
            (*cursor)[sz] == '!' ||
            (*cursor)[sz] == '.' ||
            (*cursor)[sz] == '$' ||
            (*cursor)[sz] == '_')) sz++;
    string result{*cursor, sz};
    *cursor += sz;
    return result;
}

int64_t consume_int(char **cursor, char *end) {
    bool negative = consume(cursor, end, "-");
    int64_t n = 0;
    while (*cursor < end && **cursor >= '0' && **cursor <= '9') {
        n *= 10;
        n += (**cursor - '0');
        (*cursor)++;
    }
    return negative ? -n : n;
}

Expr consume_float(char **cursor, char *end) {
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



Expr parse_halide_expr(char **cursor, char *end, Type expected_type) {
    consume_whitespace(cursor, end);

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

    static TypePattern typenames[] = {
        {UInt(1)},
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
    for (auto t : typenames) {
        if (consume(cursor, end, t.cast_prefix)) {
            Expr a = cast(t.type, parse_halide_expr(cursor, end, Type{}));
            expect(cursor, end, ")");
            return a;
        }
        if (consume(cursor, end, t.constant_prefix)) {
            return make_const(t.type, consume_int(cursor, end));
        }
    }
    if (consume(cursor, end, "(let ")) {
        string name = consume_token(cursor, end);
        consume_whitespace(cursor, end);
        expect(cursor, end, "=");
        consume_whitespace(cursor, end);

        Expr value = parse_halide_expr(cursor, end, Type{});

        consume_whitespace(cursor, end);
        expect(cursor, end, "in");
        consume_whitespace(cursor, end);

        Expr body = parse_halide_expr(cursor, end, expected_type);

        Expr a = Let::make(name, value, body);
        expect(cursor, end, ")");
        return a;
    }
    if (consume(cursor, end, "min(")) {
        Expr a = parse_halide_expr(cursor, end, expected_type);
        expect(cursor, end, ",");
        Expr b = parse_halide_expr(cursor, end, expected_type);
        consume_whitespace(cursor, end);
        expect(cursor, end, ")");
        return min(a, b);
    }
    if (consume(cursor, end, "max(")) {
        Expr a = parse_halide_expr(cursor, end, expected_type);
        expect(cursor, end, ",");
        Expr b = parse_halide_expr(cursor, end, expected_type);
        consume_whitespace(cursor, end);
        expect(cursor, end, ")");
        return max(a, b);
    }
    if (consume(cursor, end, "select(")) {
        Expr a = parse_halide_expr(cursor, end, Bool());
        expect(cursor, end, ",");
        Expr b = parse_halide_expr(cursor, end, expected_type);
        expect(cursor, end, ",");
        Expr c = parse_halide_expr(cursor, end, expected_type);
        consume_whitespace(cursor, end);
        expect(cursor, end, ")");
        return select(a, b, c);
    }
    Call::ConstString binary_intrinsics[] =
        {Call::bitwise_and,
         Call::bitwise_or,
         Call::shift_left,
         Call::shift_right};
    for (auto intrin : binary_intrinsics) {
        if (consume(cursor, end, intrin)) {
            expect(cursor, end, "(");
            Expr a = parse_halide_expr(cursor, end, expected_type);
            expect(cursor, end, ",");
            Expr b = parse_halide_expr(cursor, end, expected_type);
            consume_whitespace(cursor, end);
            expect(cursor, end, ")");
            return Call::make(a.type(), intrin, {a, b}, Call::PureIntrinsic);
        }
    }

    if (consume(cursor, end, "round_f32(")) {
        Expr a = parse_halide_expr(cursor, end, Float(32));
        expect(cursor, end, ")");
        return round(a);
    }
    if (consume(cursor, end, "ceil_f32(")) {
        Expr a = parse_halide_expr(cursor, end, Float(32));
        expect(cursor, end, ")");
        return ceil(a);
    }
    if (consume(cursor, end, "floor_f32(")) {
        Expr a = parse_halide_expr(cursor, end, Float(32));
        expect(cursor, end, ")");
        return floor(a);
    }
    if (consume(cursor, end, "likely(")) {
        Expr a = parse_halide_expr(cursor, end, expected_type);
        expect(cursor, end, ")");
        return likely(a);
    }
    if (consume(cursor, end, "likely_if_innermost(")) {
        Expr a = parse_halide_expr(cursor, end, expected_type);
        expect(cursor, end, ")");
        return likely(a);
    }

    if (consume(cursor, end, "(")) {
        Expr a = parse_halide_expr(cursor, end, Type{});
        Expr result;
        consume_whitespace(cursor, end);
        if (consume(cursor, end, "+")) {
            result = a + parse_halide_expr(cursor, end, expected_type);
        }
        if (consume(cursor, end, "*")) {
            result = a * parse_halide_expr(cursor, end, expected_type);
        }
        if (consume(cursor, end, "-")) {
            result = a - parse_halide_expr(cursor, end, expected_type);
        }
        if (consume(cursor, end, "/")) {
            result = a / parse_halide_expr(cursor, end, expected_type);
        }
        if (consume(cursor, end, "%")) {
            result = a % parse_halide_expr(cursor, end, expected_type);
        }
        if (consume(cursor, end, "<=")) {
            result = a <= parse_halide_expr(cursor, end, Type{});
        }
        if (consume(cursor, end, "<")) {
            result = a < parse_halide_expr(cursor, end, Type{});
        }
        if (consume(cursor, end, ">=")) {
            result = a >= parse_halide_expr(cursor, end, Type{});
        }
        if (consume(cursor, end, ">")) {
            result = a > parse_halide_expr(cursor, end, Type{});
        }
        if (consume(cursor, end, "==")) {
            result = a == parse_halide_expr(cursor, end, Type{});
        }
        if (consume(cursor, end, "!=")) {
            result = a != parse_halide_expr(cursor, end, Type{});
        }
        if (consume(cursor, end, "&&")) {
            result = a && parse_halide_expr(cursor, end, Bool());
        }
        if (consume(cursor, end, "||")) {
            result = a || parse_halide_expr(cursor, end, Bool());
        }
        if (result.defined()) {
            consume_whitespace(cursor, end);
            expect(cursor, end, ")");
            return result;
        }
    }
    if (consume(cursor, end, "v")) {
        if (expected_type == Type{}) {
            expected_type = Int(32);
        }
        Expr a = Variable::make(expected_type, "v" + std::to_string(consume_int(cursor, end)));
        return a;
    }
    if ((**cursor >= '0' && **cursor <= '9') || **cursor == '-') {
        Expr e = make_const(Int(32), consume_int(cursor, end));
        if (**cursor == '.') {
            e += consume_float(cursor, end);
        }
        return e;
    }
    if (consume(cursor, end, "true")) {
        return const_true();
    }
    if (consume(cursor, end, "false")) {
        return const_false();
    }
    if (consume(cursor, end, "!")) {
        return !parse_halide_expr(cursor, end, Bool());
    }

    if ((**cursor >= 'a' && **cursor <= 'z') || **cursor == '.') {
        char **tmp = cursor;
        string name = consume_token(tmp, end);
        if (consume(tmp, end, "[")) {
            *cursor = *tmp;
            Expr index = parse_halide_expr(cursor, end, Int(32));
            expect(cursor, end, "]");
            if (expected_type == Type{}) {
                expected_type = Int(32);
            }
            return Load::make(expected_type, name, index, Buffer<>(), Parameter(), const_true(), ModulusRemainder());
        } else {
            if (expected_type == Type{}) {
                expected_type = Int(32);
            }
            return Variable::make(expected_type, name);
        }
    }

    std::cerr << "Failed to parse Halide Expr starting at " << *cursor << "\n";
    abort();
    return Expr();
}
