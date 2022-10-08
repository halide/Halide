#ifndef HALIDE_SEXP_PARSER_H
#define HALIDE_SEXP_PARSER_H
#include "Util.h"
#include "IR.h"

namespace Halide {
namespace Internal {

enum class TokenType {
    Unknown,
    LeftParen,
    RightParen,
    Symbol,
    Number,
    FloatNumber
};

struct Token  {
    // If we had C++17, we could do something nicer
    // using std::variant
    std::string str;
    int num;
    double dbl;
    TokenType type;
    Token() : str("") { }
};

class SExpParser {
    inline void close_sexp(std::string &sexp);
    Expr parse_binop(Token &tok, std::string &sexp, Type expected_type);
    Type parse_type(const std::string &str);
    std::vector<Expr> parse_param_list(std::string &sexp);
    Expr parse_intrinsic(Token &tok, std::string &sexp);
    Expr parse_cast(Token &tok, std::string &sexp);

public:
    Expr parse(std::string &sexp, Type expected_type = Type());
};

void sexp_parser_test();
}
}

#endif  // HALIDE_SEXP_PARSER_H
