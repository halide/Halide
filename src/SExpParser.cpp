#include "SExpParser.h"
#include "IR.h"
#include "IROperator.h"
#include "Type.h"

using namespace std;

namespace Halide {

namespace Internal {

namespace {

inline bool is_binop(char a) {
    return (a == '+' ||
            a == '-' ||
            a == '*' ||
            a == '%' ||
            a == '/');
}

bool is_cast_op(const string &str) {
    // first, find the underlying type (uint, int, float)
    auto pos = str.find_first_of("0123456789");
    if (pos == string::npos) {
        return false;
    }
    auto type_str = str.substr(0, pos);

    // There must be an x, for scalar casts, we use the form uint8x1
    auto x_pos = str.find_first_of('x');
    if (x_pos == string::npos) {
        return false;
    }
    
    if (starts_with(type_str, "uint")) {
        return true;
    } else if (starts_with(type_str, "int")) {
        return true;
    } else if (starts_with(type_str, "float")) {
        return true;
    } else {
        return false;
    }
}

bool get_token(string &sexp, Token &token) {
    token.type = TokenType::Unknown;
    while (isspace(sexp[0])) {
      sexp = sexp.substr(1);
    }

    if (starts_with(sexp, "(")) {
        token.type = TokenType::LeftParen;
        sexp = sexp.substr(1);
        return true;
    } else if (starts_with(sexp, ")")) {
        token.type = TokenType::RightParen;
        sexp = sexp.substr(1);
        return true;
    } else if (isalpha(sexp[0]) || (is_binop(sexp[0]) && isspace(sexp[1]))) {
        auto it = sexp.begin();
        while (it != sexp.end() && !isspace(*it) && *it !=')') {
            it++;
        }
        if (it == sexp.end()) {
            return false;
        } else {
            token.type = TokenType::Symbol;
            token.str = sexp.substr(0, it - sexp.begin());
            sexp = sexp.substr(it - sexp.begin());
            return true;
        }
    } else if (isdigit(sexp[0]) || (sexp[0] == '-' && isdigit(sexp[1]))) {
        bool found_x = false;
        bool found_dot = false;
        auto it = sexp.begin();
        while (it != sexp.end() && *it != ' ' && *it !=')') {
            found_dot = found_dot || (*it == '.');
            found_x = found_x || (*it == 'x');
            it++;
        }
        if (found_dot) {
            // floating point number
            token.type = TokenType::FloatNumber;
            token.dbl = stod(sexp.substr(0, it - sexp.begin()));
            sexp = sexp.substr(it - sexp.begin());
            return true;
        } else {
            // Integer
            token.type = TokenType::Number;
            if (found_x) {
                token.num = (int) stol(sexp.substr(0, it - sexp.begin()), nullptr, 16);
            } else {
                // std::cerr << "stoi 0: " << sexp.substr(0, it - sexp.begin()) << "\n";
                token.num = stoi(sexp.substr(0, it - sexp.begin()));
            }
            sexp = sexp.substr(it - sexp.begin());
            return true;
        }
    }

    // Error state
    return false;
}

} // anonymous namespace

inline void SExpParser::close_sexp(string &sexp) {
    // ensure closing token
    Token final_token;
    internal_assert(get_token(sexp, final_token)) << "Failed to close sexp\n";
    internal_assert(final_token.type == TokenType::RightParen) << "Failed to close sexp (right paren)\n";
}

Expr SExpParser::parse_binop(Token &tok, string &sexp, Type expected_type) {
    if (getenv("RAKE_PARSER_DEBUG")) {
        debug(0) << "parsing binop " << tok.str << "\n";
    }

    auto a = parse(sexp, expected_type);
    auto b = parse(sexp, expected_type);
    
    close_sexp(sexp);

    if (tok.str == "+") {
        return Add::make(a, b);
    } else if (tok.str == "-") {
        return Sub::make(a, b);
    } else if (tok.str == "*") {
        return Mul::make(a, b);
    } else if (tok.str == "/") {
        return Div::make(a, b);
    } else if (tok.str == "%") {
        return Mod::make(a, b);
    } else if (tok.str == "min") {
        return Min::make(a, b);
    } else if (tok.str == "max") {
        return Max::make(a, b);
    } else {
      internal_error << "SExpParser::parse_binop failed" << sexp << "\n";
      return Expr();
    }
}

Type SExpParser::parse_type(const string &str) {
    if (getenv("RAKE_PARSER_DEBUG")) {
        debug(0) << "parsing type " << str << "\n";
    }

    // first, find the underlying type (uint, int, float)
    auto pos = str.find_first_of("0123456789");
    internal_assert(pos != string::npos) << "failed to parse type: " << str << "\n";
    auto type_str = str.substr(0, pos);

    // check if there's an x to see if there's more than one lane
    int lanes = 1;
    auto x_pos = str.find_first_of('x');
    // std::cerr << "stoi 1: " << str.substr(pos, x_pos-pos) << "\n";
    auto bits = stoi(str.substr(pos, x_pos-pos));
    if (x_pos != string::npos) {
      // std::cerr << "stoi 2: " << str.substr(x_pos+1, string::npos) << "\n";
      lanes = stoi(str.substr(x_pos+1, string::npos));
    }

    if (starts_with(type_str, "uint")) {
      return UInt(bits, lanes);
    } else if (starts_with(type_str, "int")) {
      return Int(bits, lanes);
    } else if (starts_with(type_str, "float")) {
      return Float(bits, lanes);
    } else {
      internal_error << "Unknown type: " << str << "\n";
      return Type();
    }
}

vector<Expr> SExpParser::parse_param_list(string &sexp) {
    if (getenv("RAKE_PARSER_DEBUG")) {
        debug(0) << "parsing param list"
                                              << "\n";
    }

    vector<Expr> params;

    // the first two tokens here need to be LeftParen
    // and "list"
    Token tok;
    internal_assert(get_token(sexp, tok)) << "failed to get first token:\n" << sexp << "\n";
    internal_assert(tok.type == TokenType::LeftParen) << "first token not a LeftParen:\n" << sexp << "\n";
    internal_assert(get_token(sexp, tok)) << "failed to get second token:\n" << sexp << "\n";
    internal_assert(tok.type == TokenType::Symbol &&
                tok.str == "list") << "second token not a'list':\n" << sexp << "\n";


    // now we have (type val)
    internal_assert(get_token(sexp, tok)) << "need type after list\n" << sexp << "\n";
    while (tok.type == TokenType::LeftParen) {
        Token tp_token;
        internal_assert(get_token(sexp, tp_token)) << "need symbol after left paren\n" << sexp << "\n";
        internal_assert(tp_token.type == TokenType::Symbol) << "didn't find symbol after left paren\n" << sexp << "\n";
        Type t = parse_type(tp_token.str);
        params.push_back(parse(sexp, t));
        close_sexp(sexp);
        // get the next token
        // if it's another param, this loop will continue; otherwise,
        // it'll be the close paren
        internal_assert(get_token(sexp, tok)) << "failed to get next token\n" << sexp << "\n";
        internal_assert(tok.type == TokenType::LeftParen ||
                    tok.type == TokenType::RightParen) << "next token not a right or left paren\n" << sexp << "\n";
    }

    if (getenv("RAKE_PARSER_DEBUG")) {
        debug(0) << params[0] << "\n";
    }

    return params;

}

// Strip half slices. Special handling for reinterpret or broadcasts.
Expr strip_first_half(const Expr &expr) {
    // First check special cases.
    const Call *call = expr.as<Call>();
    const Broadcast *broadcast = expr.as<Broadcast>();
    if (call && call->name == "reinterpret") {
        Expr s = strip_first_half(call->args[0]);
        if (s.defined()) {
            Type t = expr.type().with_lanes(s.type().lanes());
            s = reinterpret(t, s);
        }
        return s;
    } else if (broadcast) {
        return Broadcast::make(broadcast->value, broadcast->lanes * 2);
    }
    // Lastly check shuffles.
    const Shuffle *shuffle = expr.as<Shuffle>();
    if (!shuffle || !shuffle->is_slice() || !(shuffle->vectors.size() == 1) || shuffle->indices.empty() || (shuffle->indices[0] != 0)) {
        return Expr();
    }
    return shuffle->vectors[0];
}

Expr strip_second_half(const Expr &expr) {
    // First check special cases.
    const Call *call = expr.as<Call>();
    const Broadcast *broadcast = expr.as<Broadcast>();
    if (call && call->name == "reinterpret") {
        Expr s = strip_second_half(call->args[0]);
        if (s.defined()) {
            Type t = expr.type().with_lanes(s.type().lanes());
            s = reinterpret(t, s);
        }
        return s;
    } else if (broadcast) {
        return Broadcast::make(broadcast->value, broadcast->lanes * 2);
    }
    // Lastly check shuffles.
    const Shuffle *shuffle = expr.as<Shuffle>();
    const int lanes = expr.type().lanes();
    if (!shuffle || !shuffle->is_slice() || !(shuffle->vectors.size() == 1) || shuffle->indices.empty() || (shuffle->indices[0] != lanes)) {
        return Expr();
    }
    return shuffle->vectors[0];
}

namespace {
Expr first_half(const Expr &a) {
    return Shuffle::make_slice(a, 0, 1, a.type().lanes() / 2);
}

Expr second_half(const Expr &a) {
    return Shuffle::make_slice(a, a.type().lanes() / 2, 1, a.type().lanes() / 2);
}

Expr make_half_wide_binary_call(const Type &return_type, const Expr &a, const Type &at, const Expr &b, const Type &bt, const std::string &name) {
    Expr v0 = strip_first_half(a);
    Expr v1 = strip_first_half(b);
    
    if (v0.defined() && v1.defined()) {
        internal_assert(v0.type() == at && v1.type() == bt)
          << name << " failure\n"
          << v0 << " and " << v1 << "\n";
        Expr full_call = Call::make(return_type, name, {v0, v1}, Call::CallType::PureExtern);
        return first_half(full_call);
    }

    v0 = strip_second_half(a);
    v1 = strip_second_half(b);

    if (v0.defined() && v1.defined()) {
        internal_assert(v0.type() == at && v1.type() == bt)
          << name << " 2 failure\n"
          << v0 << " and " << v1 << "\n";
        Expr full_call = Call::make(return_type, name, {v0, v1}, Call::CallType::PureExtern);
        return second_half(full_call);
    }
    internal_error << name << " failed horribly\n" << "a = " << a << "\nb = " << b << "\n";
    return Expr();
}

Expr make_half_wide_ternary_call(const Type &return_type, const Expr &a, const Type &at, const Expr &b, const Type &bt, const Expr &c, const Type &ct, const std::string &name) {
    Expr v0 = strip_first_half(a);
    Expr v1 = strip_first_half(b);
    Expr v2 = strip_first_half(c);

    if (v0.defined() && v1.defined() && v2.defined()) {
        internal_assert(v0.type() == at && v1.type() == bt && v2.type() == ct)
          << name << " failure\n"
          << v0 << " and " << v1 << " and " << v2 << "\n";
        Expr full_call = Call::make(return_type, name, {v0, v1, v2}, Call::CallType::PureExtern);
        return first_half(full_call);
    }

    v0 = strip_second_half(a);
    v1 = strip_second_half(b);
    v2 = strip_second_half(c);

    if (v0.defined() && v1.defined() && v2.defined()) {
        internal_assert(v0.type() == at && v1.type() == bt && v2.type() == ct)
          << name << " 2 failure\n"
          << v0 << " and " << v1 << " and " << v2 << "\n";
        Expr full_call = Call::make(return_type, name, {v0, v1, v2}, Call::CallType::PureExtern);
        return second_half(full_call);
    }

    internal_error << name << " failed horribly\n" << "a = " << a << "\nb = " << b << "\nc = " << c << "\n";
    return Expr();
}
}

Expr SExpParser::parse_intrinsic(Token &tok, string &sexp) {
    if (getenv("RAKE_PARSER_DEBUG")) {
        debug(0) << "parsing intrinsic " << tok.str << "\n";
    }

    // we'll create a Call node here
    auto func_name = tok.str;
    internal_assert(get_token(sexp, tok)) << "failed to get intrinsic token\n" << sexp << "\n";
    internal_assert(tok.type == TokenType::Symbol)  << "failed to get intrinsic symbol\n" << sexp << "\n";
    auto return_type = parse_type(tok.str);

    auto params = parse_param_list(sexp);

    close_sexp(sexp);
    if (func_name.find("vread") != std::string::npos){
      Expr index = (return_type.lanes() == 1 ? params[1] : Ramp::make(params[1], 1, return_type.lanes()));
      return Load::make(
        return_type,
        params[0].as<Variable>()->name,
        index,
        Buffer<>(),
        Parameter(),
        const_true(return_type.lanes()),
        (params.size() == 4?
          ModulusRemainder(params[2].as<IntImm>()->value, params[3].as<IntImm>()->value) :
          ModulusRemainder())
        );
    }
    else if (func_name.find("load.scalar") != std::string::npos) {
        Expr index = (return_type.lanes() == 1 ? params[1] : Ramp::make(params[1], 1, return_type.lanes()));
        return Load::make(
            return_type,
            params[0].as<Variable>()->name,
            index,
            Buffer<>(),
            Parameter(),
            const_true(return_type.lanes()),
            ModulusRemainder()
        );
    }
    else if(func_name.find("concat_vectors") != std::string::npos){
      return Shuffle::make_concat(params);
    } 
    else if (func_name.find("x128") != std::string::npos) {
        return Broadcast::make(params[0], 128);
    } else if (func_name.find("x64") != std::string::npos) {
        return Broadcast::make(params[0], 64);
    } else if (func_name.find("reinterpret") != std::string::npos) {
        return reinterpret(return_type, params[0]);
    } else if (func_name.find("rounding_shift_right") != std::string::npos) {
        return rounding_shift_right(params[0], params[1]);
    } else if (func_name.find("halide.ir.x2") != std::string::npos) {
        return Broadcast::make(params[0], 2);
    } else if (func_name.find("halide.ir.x4") != std::string::npos) {
        return Broadcast::make(params[0], 4);
    } else if (func_name.find("halide.ir.x8") != std::string::npos) {
        return Broadcast::make(params[0], 8);
    } else if (func_name.find("halide.ir.x16") != std::string::npos) {
        return Broadcast::make(params[0], 16);
    } else if (func_name.find("halide.ir.x32") != std::string::npos) {
        return Broadcast::make(params[0], 32);
    } else if (func_name.find("halide.ir.x64") != std::string::npos) {
        return Broadcast::make(params[0], 64);
    } else if (func_name.find("llvm.aarch64.neon.ld") != std::string::npos) {
        Expr index = (return_type.lanes() == 1 ? params[1] : Ramp::make(params[1], 1, return_type.lanes()));
        internal_assert(params[0].as<Variable>()) << "llvm.aarch64.neon.ld did not receive a variable\n" << sexp << "\n";
        return Load::make(
          return_type,
          params[0].as<Variable>()->name,
          index,
          Buffer<>(),
          Parameter(),
          const_true(return_type.lanes()),
          (params.size() == 4?
            ModulusRemainder(params[2].as<IntImm>()->value, params[3].as<IntImm>()->value) :
            ModulusRemainder())
          );
    } else if (func_name.find("halide.ir.add") != std::string::npos) {
        // No LLVM representation, generate normal Halide code.
        internal_assert(params.size() == 2 && params[0].type().is_vector()) << "halide.ir.add requires the first argument to be a vector.\n";
        Expr add = params[0] + params[1];
        internal_assert(add.type() == return_type)
          << "halide.ir.add failed to produce return type: " << return_type
          << "\nwith add: " << add << "\n";
        return add;
    } else if (func_name.find("halide.ir.ramp") != std::string::npos) {
        internal_assert(is_const(params[2], return_type.lanes())) << "halide.ir.ramp has incorrect lanes: " << params[2] << "\n";
        return Ramp::make(params[0], params[1], return_type.lanes());
    } else if (func_name.find("llvm.x86.avx2.ld") != std::string::npos) {
        Expr index = (return_type.lanes() == 1 ? params[1] : Ramp::make(params[1], 1, return_type.lanes()));
        internal_assert(params[0].as<Variable>()) << "llvm.x86.avx2.ld did not receive a variable\n" << sexp << "\n";
        return Load::make(
          return_type,
          params[0].as<Variable>()->name,
          index,
          Buffer<>(),
          Parameter(),
          const_true(return_type.lanes()),
          (params.size() == 4?
            ModulusRemainder(params[2].as<IntImm>()->value, params[3].as<IntImm>()->value) :
            ModulusRemainder())
          );
    } else if (func_name.find("halide.ir.fhalf") != std::string::npos) {
        // notation for first half of a vector
        internal_assert(params.size() == 1 && params[0].type().is_vector()) << "halide.ir.fhalf requires a single vector argument.\n";
        const int lanes = params[0].type().lanes() / 2;
        internal_assert(lanes == return_type.lanes()) << "halide.ir.fhalf should take only half of the vector arg, instead: "
          << return_type << " from " << params[0] << "\n";
        return Shuffle::make_slice(params[0], 0, 1, lanes);
    } else if (func_name.find("halide.ir.shalf") != std::string::npos) {
        // notation for second half of a vector
        internal_assert(params.size() == 1 && params[0].type().is_vector()) << "halide.ir.shalf requires a single vector argument.\n";
        const int lanes = params[0].type().lanes() / 2;
        internal_assert(lanes == return_type.lanes()) << "halide.ir.shalf should take only half of the vector arg, instead: "
          << return_type << " from " << params[0] << "\n";
        return Shuffle::make_slice(params[0], lanes, 1, lanes);
    } else if (func_name.find("llvm.aarch64.neon.widening_add") != std::string::npos) {
        internal_assert(params.size() == 2) << "neon.widening_add requires 2 args, received: " << params.size() << "\n";
        // LLVM has no AARCH64 widening add intrinsics call code in runtime/arm.ll
        if (return_type == UInt(16, 8)) {
            return make_half_wide_binary_call(UInt(16, 16), params[0], UInt(8, 16), params[1], UInt(8, 16), "rake.uaddl_u8x16");
        } else if (return_type == Int(32, 4)) {
            return make_half_wide_binary_call(Int(32, 8), params[0], Int(16, 8), params[1], Int(16, 8), "rake.saddl_i16x6");
        } else {
            internal_error << "unrecognized neon.widening_add: " << return_type << "\n";
        }
    } else if (func_name.find("llvm.aarch64.neon.wide_add") != std::string::npos) {
        // LLVM has no AARCH64 widening add intrinsics, hope LLVM pattern matches correctly.
        internal_assert(params.size() == 2 && params[0].type().is_vector() && params[1].type().is_vector()) << "llvm.aarch64.neon.wide_add requires two vector arguments.\n";
        Expr wadd = params[0] + cast(params[0].type(), params[1]);
        internal_assert(wadd.type() == return_type)
          << "llvm.aarch64.neon.wide_add failed to produce return type: " << return_type
          << "\nwith widening_add: " << wadd << "\n";
        return wadd;
    } else if (func_name.find("llvm.aarch64.neon.widening_sub") != std::string::npos) {
        // LLVM has no AARCH64 widening sub intrinsics, hope LLVM pattern matches correctly.
        internal_assert(params.size() == 2 && params[0].type().is_vector() && params[1].type().is_vector()) << "llvm.aarch64.neon.widening_sub requires two vector arguments.\n";
        if (return_type == UInt(16, 8)) {
            // rake.usubl_u8x16
            return make_half_wide_binary_call(UInt(16, 16), params[0], UInt(8, 16), params[1], UInt(8, 16), "rake.usubl_u8x16");
        } else {
            internal_error << "unrecognized neon.widening_sub: " << return_type << "\n";
        }
        Expr wsub = widening_sub(params[0], params[1]);
        internal_assert(wsub.type() == return_type)
          << "llvm.aarch64.neon.widening_sub failed to produce return type: " << return_type
          << "\nwith widening_sub: " << wsub << "\n";
        return wsub;
    } else if (func_name.find("halide.ir.mul") != std::string::npos) {
        // No LLVM representation, generate normal Halide code.
        internal_assert(params.size() == 2 && params[0].type().is_vector()) << "halide.ir.mul requires the first argument to be a vector.\n";
        Expr mul = params[0] * params[1];
        internal_assert(mul.type() == return_type)
          << "halide.ir.mul failed to produce return type: " << return_type
          << "\nwith mul: " << mul << "\n";
        return mul;
    } else if ((func_name.find("halide.ir.mla") != std::string::npos) ||
               (func_name.find("halide.ir.mls") != std::string::npos) ||
               (func_name.find("halide.ir.neg") != std::string::npos)) {
        internal_error << "AJ needs to implement: " << func_name << "\n";
    } else if (func_name.find("llvm.aarch64.neon.mlal") != std::string::npos) {
        // LLVM has no AARCH64 multiply-add long intrinsics, hope LLVM pattern matches correctly.
        internal_assert(params.size() == 3 && params[0].type().is_vector() && params[1].type().is_vector()) << "llvm.aarch64.neon.mlal requires two vector arguments and a multiply argument.\n";
        // TODO: rake should actually just codegen the correct thing.

        if (return_type == UInt(16, 8)) {
            return make_half_wide_ternary_call(UInt(16, 16), params[0], UInt(16, 16), params[1], UInt(8, 16), params[2], UInt(8, 16), "rake.umlal_u16x16");
        } else if (return_type == UInt(32, 4)) {
            return make_half_wide_ternary_call(UInt(32, 8), params[0], UInt(32, 8), params[1], UInt(16, 8), params[2], UInt(16, 8), "rake.umlal_u32x8");
        } else if (return_type == UInt(64, 2)) {
            return make_half_wide_ternary_call(UInt(64, 4), params[0], UInt(64, 4), params[1], UInt(32, 4), params[2], UInt(32, 4), "rake.umlal_u64x4");
        } else if (return_type == Int(16, 8)) {
            return make_half_wide_ternary_call(Int(16, 16), params[0], Int(16, 16), params[1], Int(8, 16), params[2], Int(8, 16), "rake.smlal_i16x16");
        } else if (return_type == Int(32, 4)) {
            return make_half_wide_ternary_call(Int(32, 8), params[0], Int(32, 8), params[1], Int(16, 8), params[2], Int(16, 8), "rake.smlal_i32x8");
        } else if (return_type == Int(64, 2)) {
            return make_half_wide_ternary_call(Int(64, 4), params[0], Int(64, 4), params[1], Int(32, 4), params[2], Int(32, 4), "rake.smlal_i64x4");
        } else {
            internal_error << "Need to implement more (s | u)mlal variants\n"
                           << params[0] << " and " << params[1] << " and " << params[2] << "\n";
        }
    } else if (func_name.find("llvm.aarch64.neon.sext") != std::string::npos) {
        internal_assert(params.size() == 1 && params[0].type().is_vector()) << "llvm.aarch64.neon.sext requires a single vector argument\n";
        Expr v0 = strip_first_half(params[0]);
        if (v0.defined()) {
            internal_assert(v0.type().is_int()) << "neon.sext on non-signed integer: " << v0 << "\n";
            Type wide = v0.type().widen();
            v0 = cast(wide, v0);
            return first_half(v0);
        } else {
            v0 = strip_second_half(params[0]);
            internal_assert(v0.defined()) << "neon.sext did not receive a sliced vector: " << params[0] << "\n";
            internal_assert(v0.type().is_int()) << "neon.sext on non-signed integer: " << v0 << "\n";
            Type wide = v0.type().widen();
            v0 = cast(wide, v0);
            return second_half(v0);
        }
    } else if (func_name.find("halide.ir.interleave_lo") != std::string::npos) {
        // zip1
        internal_assert(params.size() == 2 && params[0].type().is_vector() && params[1].type().is_vector())
            << "halide.ir.interleave_lo requires two vector arguments\nreceived: " << params.size() << "\n";
        Expr interleaven = Shuffle::make_interleave(params);
        const int lanes = interleaven.type().lanes();
        // return first half.
        return Shuffle::make_slice(interleaven, 0, 1, lanes / 2);
    } else if (func_name.find("halide.ir.interleave_hi") != std::string::npos) {
        // zip2
        internal_assert(params.size() == 2 && params[0].type().is_vector() && params[1].type().is_vector())
            << "halide.ir.interleave_hi requires two vector arguments\nreceived: " << params.size() << "\n";
        Expr interleaven = Shuffle::make_interleave(params);
        const int lanes = interleaven.type().lanes();
        // return second half.
        return Shuffle::make_slice(interleaven, lanes / 2, 1, lanes / 2);
    } else if (func_name.find("halide.ir.deinterleave_odd") != std::string::npos) {
        // uzp2
        internal_assert(params.size() == 2 && params[0].type().is_vector() && params[1].type().is_vector())
            << "halide.ir.deinterleave_odd requires two vector arguments\nreceived: " << params.size() << "\n";
        Expr a = params[0];
        Expr b = params[1];
        Expr a_odds = Shuffle::make_slice(a, 1, 2, a.type().lanes() / 2);
        Expr b_odds = Shuffle::make_slice(b, 1, 2, b.type().lanes() / 2);
        return Shuffle::make_concat({a_odds, b_odds});
    } else if (func_name.find("halide.ir.deinterleave_even") != std::string::npos) {
        // uzp1
        internal_assert(params.size() == 2 && params[0].type().is_vector() && params[1].type().is_vector())
            << "halide.ir.deinterleave_even requires two vector arguments\nreceived: " << params.size() << "\n";
        Expr a = params[0];
        Expr b = params[1];
        Expr a_evens = Shuffle::make_slice(a, 0, 2, a.type().lanes() / 2);
        Expr b_evens = Shuffle::make_slice(b, 0, 2, b.type().lanes() / 2);
        return Shuffle::make_concat({a_evens, b_evens});
    }

    // Not sure about the call type here
    return Call::make(return_type, func_name, params, Call::CallType::PureExtern);
}

Expr SExpParser::parse_cast(Token &tok, string &sexp) {
    if (getenv("RAKE_PARSER_DEBUG")) {
        debug(0) << "parsing cast " << tok.str << "\n";
    }

    // we'll create a Cast node here
    internal_assert(get_token(sexp, tok)) << "failed to get cast token\n" << sexp << "\n";
    internal_assert(tok.type == TokenType::Symbol) << "failed to get cast symbol\n" << sexp << "\n";
    auto return_type = parse_type(tok.str);

    auto params = parse_param_list(sexp);

    close_sexp(sexp);

    if (getenv("RAKE_PARSER_DEBUG")) {
        debug(0) << Cast::make(return_type, params[0]) << "\n";
    }
    
    return Cast::make(return_type, params[0]);
}

Expr SExpParser::parse(string &sexp, Type expected_type) {
    Token token;
    while (!sexp.empty()) {
        auto result = get_token(sexp, token);
        internal_assert(result) << "failed to get parsing token\n" << sexp << "\n";
        if (token.type == TokenType::LeftParen) {
            // next token dictates which kind of expression
            // we're parsing
            result = get_token(sexp, token);
            internal_assert(result && token.type == TokenType::Symbol) << "failed to get symbol in parsing\n" << sexp << "\n";
            if (starts_with(token.str, "llvm") || starts_with(token.str, "halide") || starts_with(token.str, "rake")) {
                // this is an intrinsic
                 return parse_intrinsic(token, sexp);
            } else if (token.str == "+"   ||
                       token.str == "-"   ||
                       token.str == "*"   ||
                       token.str == "/"   ||
                       token.str == "%" ||
                       token.str == "min" ||
                       token.str == "max" ) {
                return parse_binop(token, sexp, expected_type);
            } else if (is_cast_op(token.str)) {
                return parse_cast(token, sexp);
            }
            else {
                // For now, assume this must be a (type value) pair
                Token tp_token = token;
                internal_assert(tp_token.type == TokenType::Symbol) << "Token not a symbol\n" << sexp << "\n";
                Type t = parse_type(tp_token.str);
                Expr p = parse(sexp, t);
                close_sexp(sexp);
                return p;
            }
        } else if (token.type == TokenType::FloatNumber) {
            internal_assert(expected_type.is_float()) << "Expected float type at " << sexp << "\n";
            return FloatImm::make(expected_type, token.dbl);
        } else if (token.type == TokenType::Number) {
            internal_assert(expected_type.is_int() || expected_type.is_uint()) << "Expected int/uint type at " << sexp << "\n";
            if (expected_type.is_int()) {
                return IntImm::make(expected_type, token.num);
            } else {
                return UIntImm::make(expected_type, token.num);
            }
        } else if (token.type == TokenType::Symbol) {
            // For now, assume a Var
            internal_assert(expected_type != Type()) << "Unknown type for var (" << token.str << ") at " << sexp << "\n";
            return Variable::make(expected_type, token.str);
        } else {
            // error
            internal_error << "Unknown token at " << sexp << "\n";
            return Expr();
        }
    }
    internal_error << "Failed to parse: " << sexp << "\n";
    return Expr();
}

void sexp_parser_test() {
    SExpParser p;

    string s = R"((llvm.hexagon.V6.vread.128B
                       int32
                       (list (int32 mask) (int32 8)))))))";

    string gaussian3x3 = R"((llvm.hexagon.V6.vasrhubrndsat.128B
    int32x32
    (list
     (int32x32
      (llvm.hexagon.V6.vmpyihb.acc.128B
       int32x32
       (list
        (int32x32
         (llvm.hexagon.V6.vaddh.128B
          int32x32
          (list
           (int32x32 (llvm.hexagon.V6.vread.128B int32x32 (list (int32 buf) (int32 x))))
           (int32x32
            (llvm.hexagon.V6.vread.128B
             int32x32
             (list (int32 buf) (int32 (+ 2 x))))))))
        (int32x32
         (llvm.hexagon.V6.vread.128B int32x32 (list (int32 buf) (int32 (+ 1 x)))))
        (int32 2))))
     (int32x32
      (llvm.hexagon.V6.vmpyihb.acc.128B
       int32x32
       (list
        (int32x32
         (llvm.hexagon.V6.vaddh.128B
          int32x32
          (list
           (int32x32
            (llvm.hexagon.V6.vread.128B int32x32 (list (int32 buf) (int32 (+ 64 x)))))
           (int32x32
            (llvm.hexagon.V6.vread.128B
             int32x32
             (list (int32 buf) (int32 (+ 66 x))))))))
        (int32x32
         (llvm.hexagon.V6.vread.128B int32x32 (list (int32 buf) (int32 (+ 65 x)))))
        (int32 2))))
     (int32 4))))";
    
    string gaussian5x5 = R"((llvm.hexagon.V6.vshuffoh.128B
    int32x32
    (list
     (int32x32
      (llvm.hexagon.V6.vmpyihb.acc.128B
       int32x32
       (list
        (int32x32
         (llvm.hexagon.V6.vmpyihb.acc.128B
          int32x32
          (list
           (int32x32
            (llvm.hexagon.V6.vaddh.128B
             int32x32
             (list
              (int32x32
               (llvm.hexagon.V6.vread.128B
                int32x32
                (list (int32 buf) (int32 (+ 64 x)))))
              (int32x32
               (llvm.hexagon.V6.vread.128B
                int32x32
                (list (int32 buf) (int32 (+ 66 x))))))))
           (int32x32
            (llvm.hexagon.V6.vread.128B
             int32x32
             (list (int32 buf) (int32 (+ 65 x)))))
           (int32 6))))
        (int32x32
         (llvm.hexagon.V6.vaddh.128B
          int32x32
          (list
           (int32x32
            (llvm.hexagon.V6.vread.128B
             int32x32
             (list (int32 buf) (int32 (+ 64 x)))))
           (int32x32
            (llvm.hexagon.V6.vread.128B
             int32x32
             (list (int32 buf) (int32 (+ 66 x))))))))
        (int32 4))))
     (int32x32
      (llvm.hexagon.V6.vmpyihb.acc.128B
       int32x32
       (list
        (int32x32
         (llvm.hexagon.V6.vmpyihb.acc.128B
          int32x32
          (list
           (int32x32
            (llvm.hexagon.V6.vaddh.128B
             int32x32
             (list
              (int32x32
               (llvm.hexagon.V6.vread.128B
                int32x32
                (list (int32 buf) (int32 (+ 64 x)))))
              (int32x32
               (llvm.hexagon.V6.vread.128B
                int32x32
                (list (int32 buf) (int32 (+ 66 x))))))))
           (int32x32
            (llvm.hexagon.V6.vread.128B
             int32x32
             (list (int32 buf) (int32 (+ 65 x)))))
           (int32 6))))
        (int32x32
         (llvm.hexagon.V6.vaddh.128B
          int32x32
          (list
           (int32x32
            (llvm.hexagon.V6.vread.128B
             int32x32
             (list (int32 buf) (int32 (+ 64 x)))))
           (int32x32
            (llvm.hexagon.V6.vread.128B
             int32x32
             (list (int32 buf) (int32 (+ 66 x))))))))
        (int32 4)))))))";

    string gaussian7x7 = R"(`(llvm.hexagon.V6.vshuffeh.128B
    int32x32
    (list
     (int32x32
      (llvm.hexagon.V6.vasrw.128B
       int32x32
       (list
        (int32x32
         (llvm.hexagon.V6.hi.128B
          int32x32
          (list
           (int32x64
            (llvm.hexagon.V6.vmpahb.acc.128B
             int32x64
             (list
              (int32x64
               (llvm.hexagon.V6.vmpahb.128B
                int32x32
                (list
                 (int32x64
                  (llvm.hexagon.V6.vcombine.128B
                   int32x64
                   (list
                    (int32x32
                     (llvm.hexagon.V6.vaddh.128B
                      int32x32
                      (list
                       (int32x32
                        (llvm.hexagon.V6.vread.128B
                         int32x32
                         (list (int32 buf) (int32 (+ 64 x)))))
                       (int32x32
                        (llvm.hexagon.V6.vread.128B
                         int32x32
                         (list (int32 buf) (int32 (+ 66 x))))))))
                    (int32x32
                     (llvm.hexagon.V6.vaddh.128B
                      int32x32
                      (list
                       (int32x32
                        (llvm.hexagon.V6.vread.128B
                         int32x32
                         (list (int32 buf) (int32 (+ 65 x)))))
                       (int32x32
                        (llvm.hexagon.V6.vread.128B
                         int32x32
                         (list (int32 buf) (int32 (+ 67 x)))))))))))
                 (int32 0x140f140f))))
              (int32x64
               (llvm.hexagon.V6.vcombine.128B
                int32x64
                (list
                 (int32x32
                  (llvm.hexagon.V6.vaddh.128B
                   int32x32
                   (list
                    (int32x32
                     (llvm.hexagon.V6.vread.128B
                      int32x32
                      (list (int32 buf) (int32 (+ 64 x)))))
                    (int32x32
                     (llvm.hexagon.V6.vread.128B
                      int32x32
                      (list (int32 buf) (int32 (+ 66 x))))))))
                 (int32x32
                  (llvm.hexagon.V6.vaddh.128B
                   int32x32
                   (list
                    (int32x32
                     (llvm.hexagon.V6.vread.128B
                      int32x32
                      (list (int32 buf) (int32 (+ 64 x)))))
                    (int32x32
                     (llvm.hexagon.V6.vread.128B
                      int32x32
                      (list (int32 buf) (int32 (+ 66 x)))))))))))
              (int32 0x140f140f)))))))
        (int32x32
         (llvm.hexagon.V6.lo.128B
          int32x32
          (list
           (int32x64
            (llvm.hexagon.V6.vmpahb.acc.128B
             int32x64
             (list
              (int32x64
               (llvm.hexagon.V6.vmpahb.128B
                int32x32
                (list
                 (int32x64
                  (llvm.hexagon.V6.vcombine.128B
                   int32x64
                   (list
                    (int32x32
                     (llvm.hexagon.V6.vaddh.128B
                      int32x32
                      (list
                       (int32x32
                        (llvm.hexagon.V6.vread.128B
                         int32x32
                         (list (int32 buf) (int32 (+ 64 x)))))
                       (int32x32
                        (llvm.hexagon.V6.vread.128B
                         int32x32
                         (list (int32 buf) (int32 (+ 66 x))))))))
                    (int32x32
                     (llvm.hexagon.V6.vaddh.128B
                      int32x32
                      (list
                       (int32x32
                        (llvm.hexagon.V6.vread.128B
                         int32x32
                         (list (int32 buf) (int32 (+ 65 x)))))
                       (int32x32
                        (llvm.hexagon.V6.vread.128B
                         int32x32
                         (list (int32 buf) (int32 (+ 67 x)))))))))))
                 (int32 0x140f140f))))
              (int32x64
               (llvm.hexagon.V6.vcombine.128B
                int32x64
                (list
                 (int32x32
                  (llvm.hexagon.V6.vaddh.128B
                   int32x32
                   (list
                    (int32x32
                     (llvm.hexagon.V6.vread.128B
                      int32x32
                      (list (int32 buf) (int32 (+ 64 x)))))
                    (int32x32
                     (llvm.hexagon.V6.vread.128B
                      int32x32
                      (list (int32 buf) (int32 (+ 66 x))))))))
                 (int32x32
                  (llvm.hexagon.V6.vaddh.128B
                   int32x32
                   (list
                    (int32x32
                     (llvm.hexagon.V6.vread.128B
                      int32x32
                      (list (int32 buf) (int32 (+ 64 x)))))
                    (int32x32
                     (llvm.hexagon.V6.vread.128B
                      int32x32
                      (list (int32 buf) (int32 (+ 66 x)))))))))))
              (int32 0x140f140f)))))))
        (int32 12))))
     (int32x32
      (llvm.hexagon.V6.vasrw.128B
       int32x32
       (list
        (int32x32
         (llvm.hexagon.V6.hi.128B
          int32x32
          (list
           (int32x64
            (llvm.hexagon.V6.vmpahb.acc.128B
             int32x64
             (list
              (int32x64
               (llvm.hexagon.V6.vmpahb.128B
                int32x32
                (list
                 (int32x64
                  (llvm.hexagon.V6.vcombine.128B
                   int32x64
                   (list
                    (int32x32
                     (llvm.hexagon.V6.vaddh.128B
                      int32x32
                      (list
                       (int32x32
                        (llvm.hexagon.V6.vread.128B
                         int32x32
                         (list (int32 buf) (int32 (+ 64 x)))))
                       (int32x32
                        (llvm.hexagon.V6.vread.128B
                         int32x32
                         (list (int32 buf) (int32 (+ 66 x))))))))
                    (int32x32
                     (llvm.hexagon.V6.vaddh.128B
                      int32x32
                      (list
                       (int32x32
                        (llvm.hexagon.V6.vread.128B
                         int32x32
                         (list (int32 buf) (int32 (+ 65 x)))))
                       (int32x32
                        (llvm.hexagon.V6.vread.128B
                         int32x32
                         (list (int32 buf) (int32 (+ 67 x)))))))))))
                 (int32 0x140f140f))))
              (int32x64
               (llvm.hexagon.V6.vcombine.128B
                int32x64
                (list
                 (int32x32
                  (llvm.hexagon.V6.vaddh.128B
                   int32x32
                   (list
                    (int32x32
                     (llvm.hexagon.V6.vread.128B
                      int32x32
                      (list (int32 buf) (int32 (+ 64 x)))))
                    (int32x32
                     (llvm.hexagon.V6.vread.128B
                      int32x32
                      (list (int32 buf) (int32 (+ 66 x))))))))
                 (int32x32
                  (llvm.hexagon.V6.vaddh.128B
                   int32x32
                   (list
                    (int32x32
                     (llvm.hexagon.V6.vread.128B
                      int32x32
                      (list (int32 buf) (int32 (+ 64 x)))))
                    (int32x32
                     (llvm.hexagon.V6.vread.128B
                      int32x32
                      (list (int32 buf) (int32 (+ 66 x)))))))))))
              (int32 0x140f140f)))))))
        (int32x32
         (llvm.hexagon.V6.lo.128B
          int32x32
          (list
           (int32x64
            (llvm.hexagon.V6.vmpahb.acc.128B
             int32x64
             (list
              (int32x64
               (llvm.hexagon.V6.vmpahb.128B
                int32x32
                (list
                 (int32x64
                  (llvm.hexagon.V6.vcombine.128B
                   int32x64
                   (list
                    (int32x32
                     (llvm.hexagon.V6.vaddh.128B
                      int32x32
                      (list
                       (int32x32
                        (llvm.hexagon.V6.vread.128B
                         int32x32
                         (list (int32 buf) (int32 (+ 64 x)))))
                       (int32x32
                        (llvm.hexagon.V6.vread.128B
                         int32x32
                         (list (int32 buf) (int32 (+ 66 x))))))))
                    (int32x32
                     (llvm.hexagon.V6.vaddh.128B
                      int32x32
                      (list
                       (int32x32
                        (llvm.hexagon.V6.vread.128B
                         int32x32
                         (list (int32 buf) (int32 (+ 65 x)))))
                       (int32x32
                        (llvm.hexagon.V6.vread.128B
                         int32x32
                         (list (int32 buf) (int32 (+ 67 x)))))))))))
                 (int32 0x140f140f))))
              (int32x64
               (llvm.hexagon.V6.vcombine.128B
                int32x64
                (list
                 (int32x32
                  (llvm.hexagon.V6.vaddh.128B
                   int32x32
                   (list
                    (int32x32
                     (llvm.hexagon.V6.vread.128B
                      int32x32
                      (list (int32 buf) (int32 (+ 64 x)))))
                    (int32x32
                     (llvm.hexagon.V6.vread.128B
                      int32x32
                      (list (int32 buf) (int32 (+ 66 x))))))))
                 (int32x32
                  (llvm.hexagon.V6.vaddh.128B
                   int32x32
                   (list
                    (int32x32
                     (llvm.hexagon.V6.vread.128B
                      int32x32
                      (list (int32 buf) (int32 (+ 64 x)))))
                    (int32x32
                     (llvm.hexagon.V6.vread.128B
                      int32x32
                      (list (int32 buf) (int32 (+ 66 x)))))))))))
              (int32 0x140f140f)))))))
        (int32 12)))))))";

    string conv3x3a16 = R"((llvm.hexagon.V6.vasrhsat.128B
    int32x32
    (list
     (int32x32
      (llvm.hexagon.V6.hi.128B
       int32x32
       (list
        (int32x64
         (llvm.hexagon.V6.vmpybus.acc.128B
          int32x64
          (list
           (int32x64
            (llvm.hexagon.V6.vdmpybus.dv.acc.128B
             int32x64
             (list
              (int32x64
               (llvm.hexagon.V6.vmpabus.acc.128B
                int32x64
                (list
                 (int32x64
                  (llvm.hexagon.V6.vdmpybus.dv.acc.128B
                   int32x64
                   (list
                    (int32x64
                     (llvm.hexagon.V6.vdmpybus.dv.128B
                      int32x64
                      (list (int32x64 VuVu) (int32 0x02020202))))
                    (int32x64
                     (llvm.hexagon.V6.vcombine.128B
                      int32x64
                      (list (int32x32 Vu) (int32x32 Vu))))
                    (int32 0x02020202))))
                 (int32x64 VuVu)
                 (int32 0x02020202))))
              (int32x64 VuVu)
              (int32 0x02020202))))
           (int32x32 Vu)
           (int32 2)))))))
     (int32x32
      (llvm.hexagon.V6.lo.128B
       int32x32
       (list
        (int32x64
         (llvm.hexagon.V6.vmpybus.acc.128B
          int32x64
          (list
           (int32x64
            (llvm.hexagon.V6.vdmpybus.dv.acc.128B
             int32x64
             (list
              (int32x64
               (llvm.hexagon.V6.vmpabus.acc.128B
                int32x64
                (list
                 (int32x64
                  (llvm.hexagon.V6.vdmpybus.dv.acc.128B
                   int32x64
                   (list
                    (int32x64
                     (llvm.hexagon.V6.vdmpybus.dv.128B
                      int32x64
                      (list (int32x64 VuVu) (int32 0x02020202))))
                    (int32x64
                     (llvm.hexagon.V6.vcombine.128B
                      int32x64
                      (list (int32x32 Vu) (int32x32 Vu))))
                    (int32 0x02020202))))
                 (int32x64 VuVu)
                 (int32 0x02020202))))
              (int32x64 VuVu)
              (int32 0x02020202))))
           (int32x32 Vu)
           (int32 2)))))))
     (int32 4))))";

    string conv3x3a32 = R"((llvm.hexagon.V6.vlalignbi.128B
    int32x32
    (list
     (int32x32
      (llvm.hexagon.V6.vsathub.128B
       int32x32
       (list
        (int32x32
         (llvm.hexagon.V6.vasrwsat.128B
          int32x32
          (list
           (int32x32
            (llvm.hexagon.V6.hi.128B
             int32x32
             (list
              (int32x64
               (llvm.hexagon.V6.vrmpybusi.acc.128B
                int32x64
                (list
                 (int32x64
                  (llvm.hexagon.V6.vrmpybusi.128B
                   int32x64
                   (list (int32x64 VuVu) (int32 0x02020202) (int32 0))))
                 (int32x64 VuVu)
                 (int32 0x02020202)
                 (int32 0)))))))
           (int32x32
            (llvm.hexagon.V6.lo.128B
             int32x32
             (list
              (int32x64
               (llvm.hexagon.V6.vrmpybusi.acc.128B
                int32x64
                (list
                 (int32x64
                  (llvm.hexagon.V6.vrmpybusi.128B
                   int32x64
                   (list (int32x64 VuVu) (int32 0x02020202) (int32 0))))
                 (int32x64 VuVu)
                 (int32 0x02020202)
                 (int32 0)))))))
           (int32 4))))
        (int32x32
         (llvm.hexagon.V6.vasrwsat.128B
          int32x32
          (list
           (int32x32
            (llvm.hexagon.V6.hi.128B
             int32x32
             (list
              (int32x64
               (llvm.hexagon.V6.vrmpybusi.acc.128B
                int32x64
                (list
                 (int32x64
                  (llvm.hexagon.V6.vrmpybusi.128B
                   int32x64
                   (list (int32x64 VuVu) (int32 0x02020202) (int32 1))))
                 (int32x64 VuVu)
                 (int32 0x02020202)
                 (int32 1)))))))
           (int32x32
            (llvm.hexagon.V6.lo.128B
             int32x32
             (list
              (int32x64
               (llvm.hexagon.V6.vrmpybusi.acc.128B
                int32x64
                (list
                 (int32x64
                  (llvm.hexagon.V6.vrmpybusi.128B
                   int32x64
                   (list (int32x64 VuVu) (int32 0x02020202) (int32 1))))
                 (int32x64 VuVu)
                 (int32 0x02020202)
                 (int32 1)))))))
           (int32 4)))))))
     (int32x32 Vu)
     (int32 0x01010101))))";

    string sobel3x3 = R"((llvm.hexagon.V6.vsathub.128B
    int32x32
    (list
     (int32x32
      (llvm.hexagon.V6.vaddh.128B
       int32x32
       (list
        (int32x32
         (llvm.hexagon.V6.vabsh.128B
          int32x32
          (list
           (int32x32
            (llvm.hexagon.V6.hi.128B
             int32x32
             (list
              (int32x64
               (llvm.hexagon.V6.vmpabus.acc.128B
                int32x64
                (list (int32x64 VuVu) (int32x64 VuVu) (int32 0x02020202))))))))))
        (int32x32
         (llvm.hexagon.V6.vabsh.128B
          int32x32
          (list
           (int32x32
            (llvm.hexagon.V6.hi.128B
             int32x32
             (list
              (int32x64
               (llvm.hexagon.V6.vmpabus.acc.128B
                int32x64
                (list
                 (int32x64 VuVu)
                 (int32x64 VuVu)
                 (int32 0x02020202)))))))))))))
     (int32x32
      (llvm.hexagon.V6.vaddh.128B
       int32x32
       (list
        (int32x32
         (llvm.hexagon.V6.vabsh.128B
          int32x32
          (list
           (int32x32
            (llvm.hexagon.V6.lo.128B
             int32x32
             (list
              (int32x64
               (llvm.hexagon.V6.vmpabus.acc.128B
                int32x64
                (list (int32x64 VuVu) (int32x64 VuVu) (int32 0x02020202))))))))))
        (int32x32
         (llvm.hexagon.V6.vabsh.128B
          int32x32
          (list
           (int32x32
            (llvm.hexagon.V6.lo.128B
             int32x32
             (list
              (int32x64
               (llvm.hexagon.V6.vmpabus.acc.128B
                int32x64
                (list
                 (int32x64 VuVu)
                 (int32x64 VuVu)
                 (int32 0x02020202))))))))))))))))";

    string gaussian7x7p1 = R"((llvm.hexagon.V6.vmpahb.acc.128B int32x64 (list (int32x64 (llvm.hexagon.V6.vmpahb.128B int32x32 (list (int32x64 (llvm.hexagon.V6.vcombine.128B int32x64 (list (int32x32 (llvm.hexagon.V6.vaddh.128B int32x32 (list (int32x32 (llvm.hexagon.V6.vread.128B int32x32 (list (int32 buf) (int32 (+ 64 x))))) (int32x32 (llvm.hexagon.V6.vread.128B int32x32 (list (int32 buf) (int32 (+ 66 x)))))))) (int32x32 (llvm.hexagon.V6.vaddh.128B int32x32 (list (int32x32 (llvm.hexagon.V6.vread.128B int32x32 (list (int32 buf) (int32 (+ 65 x))))) (int32x32 (llvm.hexagon.V6.vread.128B int32x32 (list (int32 buf) (int32 (+ 67 x))))))))))) (int32 0x140f140f)))) (int32x64 (llvm.hexagon.V6.vcombine.128B int32x64 (list (int32x32 (llvm.hexagon.V6.vaddh.128B int32x32 (list (int32x32 (llvm.hexagon.V6.vread.128B int32x32 (list (int32 buf) (int32 (+ 64 x))))) (int32x32 (llvm.hexagon.V6.vread.128B int32x32 (list (int32 buf) (int32 (+ 66 x)))))))) (int32x32 (llvm.hexagon.V6.vaddh.128B int32x32 (list (int32x32 (llvm.hexagon.V6.vread.128B int32x32 (list (int32 buf) (int32 (+ 64 x))))) (int32x32 (llvm.hexagon.V6.vread.128B int32x32 (list (int32 buf) (int32 (+ 66 x))))))))))) (int32 0x140f140f))))";
    string gaussian7x7p2 = R"((llvm.hexagon.V6.vshuffeh.128B int32x32 (list (int32x32 (llvm.hexagon.V6.vasrw.128B int32x32 (list (int32x32 (llvm.hexagon.V6.hi.128B int32x32 (list (int32x64 VuVu)))) (int32x32 (llvm.hexagon.V6.lo.128B int32x32 (list (int32x64 VuVu)))) (int32 12)))) (int32x32 (llvm.hexagon.V6.vasrw.128B int32x32 (list (int32x32 (llvm.hexagon.V6.hi.128B int32x32 (list (int32x64 VuVu)))) (int32x32 (llvm.hexagon.V6.lo.128B int32x32 (list (int32x64 VuVu)))) (int32 12))))))";
    string vasrw = R"((llvm.hexagon.V6.vasrw.128B int32x32 (list (int32x32 (llvm.hexagon.V6.hi.128B int32x32 (list (int32x64 VuVu)))) (int32x32 (llvm.hexagon.V6.lo.128B int32x32 (list (int32x64 VuVu)))) (int32 12))))";
    string neg_number_issue = R"((llvm.hexagon.V6.vshuffvdd.128B
 int16x128
 (list
  (int32x32
   (llvm.hexagon.V6.hi.128B
    int16x64
    (list
     (int32x64
      (llvm.hexagon.V6.vmpabus.acc.128B
       int16x128
       (list
        (int32x64
         (llvm.hexagon.V6.vmpybus.acc.128B
          int32x64
          (list
           (int32x64
            (llvm.hexagon.V6.vmpabus.128B
             int32x32
             (list
              (int32x64
               (llvm.hexagon.V6.vcombine.128B
                uint8x256
                (list
                 (int32x32
                  (llvm.hexagon.V6.vread.128B
                   uint8x128
                   (list
                    (int32 input)
                    (int32
                     (+
                      -2
                      (+
                       (+
                        (* 128 rows.s0.x.x)
                        (+ (* output.s0.y.y input.stride.1) (- 0 t20)))
                       (- 0 input.stride.1)))))))
                 (int32x32
                  (llvm.hexagon.V6.vread.128B
                   uint8x128
                   (list
                    (int32 input)
                    (int32
                     (+
                      -2
                      (+
                       (+
                        (* 128 rows.s0.x.x)
                        (+ (* output.s0.y.y input.stride.1) (- 0 t20)))
                       (* 2 input.stride.1))))))))))
              (int32 0x01040104))))
           (int32x32
            (llvm.hexagon.V6.vread.128B
             uint8x128
             (list
              (int32 input)
              (int32
               (+
                -2
                (+
                 (+
                  (* 128 rows.s0.x.x)
                  (+ (* output.s0.y.y input.stride.1) (- 0 t20)))
                 (* -2 input.stride.1)))))))
           (int32 1))))
        (int32x64
         (llvm.hexagon.V6.vcombine.128B
          uint8x256
          (list
           (int32x32
            (llvm.hexagon.V6.vread.128B
             uint8x128
             (list
              (int32 input)
              (int32
               (+
                -2
                (+
                 (* 128 rows.s0.x.x)
                 (+ (* output.s0.y.y input.stride.1) (- 0 t20))))))))
           (int32x32
            (llvm.hexagon.V6.vread.128B
             uint8x128
             (list
              (int32 input)
              (int32
               (+
                -2
                (+
                 input.stride.1
                 (+
                  (* 128 rows.s0.x.x)
                  (+ (* output.s0.y.y input.stride.1) (- 0 t20))))))))))))
        (int32 0x04060406)))))))
  (int32x32
   (llvm.hexagon.V6.lo.128B
    int16x64
    (list
     (int32x64
      (llvm.hexagon.V6.vmpabus.acc.128B
       int16x128
       (list
        (int32x64
         (llvm.hexagon.V6.vmpybus.acc.128B
          int32x64
          (list
           (int32x64
            (llvm.hexagon.V6.vmpabus.128B
             int32x32
             (list
              (int32x64
               (llvm.hexagon.V6.vcombine.128B
                uint8x256
                (list
                 (int32x32
                  (llvm.hexagon.V6.vread.128B
                   uint8x128
                   (list
                    (int32 input)
                    (int32
                     (+
                      -2
                      (+
                       (+
                        (* 128 rows.s0.x.x)
                        (+ (* output.s0.y.y input.stride.1) (- 0 t20)))
                       (- 0 input.stride.1)))))))
                 (int32x32
                  (llvm.hexagon.V6.vread.128B
                   uint8x128
                   (list
                    (int32 input)
                    (int32
                     (+
                      -2
                      (+
                       (+
                        (* 128 rows.s0.x.x)
                        (+ (* output.s0.y.y input.stride.1) (- 0 t20)))
                       (* 2 input.stride.1))))))))))
              (int32 0x01040104))))
           (int32x32
            (llvm.hexagon.V6.vread.128B
             uint8x128
             (list
              (int32 input)
              (int32
               (+
                -2
                (+
                 (+
                  (* 128 rows.s0.x.x)
                  (+ (* output.s0.y.y input.stride.1) (- 0 t20)))
                 (* -2 input.stride.1)))))))
           (int32 1))))
        (int32x64
         (llvm.hexagon.V6.vcombine.128B
          uint8x256
          (list
           (int32x32
            (llvm.hexagon.V6.vread.128B
             uint8x128
             (list
              (int32 input)
              (int32
               (+
                -2
                (+
                 (* 128 rows.s0.x.x)
                 (+ (* output.s0.y.y input.stride.1) (- 0 t20))))))))
           (int32x32
            (llvm.hexagon.V6.vread.128B
             uint8x128
             (list
              (int32 input)
              (int32
               (+
                -2
                (+
                 input.stride.1
                 (+
                  (* 128 rows.s0.x.x)
                  (+ (* output.s0.y.y input.stride.1) (- 0 t20))))))))))))
        (int32 0x04060406)))))))
  (int32 Rt))))";
    debug(0) << p.parse(s) << "\n";
    debug(0) << p.parse(gaussian3x3) << "\n";
    debug(0) << p.parse(gaussian5x5) << "\n";
    //debug(0) << p.parse(gaussian7x7) << "\n";
    debug(0) << p.parse(conv3x3a16) << "\n";
    debug(0) << p.parse(conv3x3a32) << "\n";
    debug(0) << p.parse(sobel3x3) << "\n";
    debug(0) << p.parse(neg_number_issue) << "\n";

    //debug(0) << p.parse(gaussian7x7p1) << "\n";
    //debug(0) << p.parse(gaussian7x7p2) << "\n";
    //debug(0) << p.parse(vasrw) << "\n";
}

}
}