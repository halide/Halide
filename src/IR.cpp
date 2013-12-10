#include "IR.h"

namespace Halide {
namespace Internal {

namespace {

IntImm make_immortal_int(int x) {
    IntImm i;
    i.ref_count.increment();
    i.type = Int(32);
    i.value = x;
    return i;
}

}

IntImm IntImm::small_int_cache[] = {make_immortal_int(-8),
                                    make_immortal_int(-7),
                                    make_immortal_int(-6),
                                    make_immortal_int(-5),
                                    make_immortal_int(-4),
                                    make_immortal_int(-3),
                                    make_immortal_int(-2),
                                    make_immortal_int(-1),
                                    make_immortal_int(0),
                                    make_immortal_int(1),
                                    make_immortal_int(2),
                                    make_immortal_int(3),
                                    make_immortal_int(4),
                                    make_immortal_int(5),
                                    make_immortal_int(6),
                                    make_immortal_int(7),
                                    make_immortal_int(8)};



template<> EXPORT IRNodeType ExprNode<IntImm>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<FloatImm>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<StringImm>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<Cast>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<Variable>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<Add>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<Sub>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<Mul>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<Div>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<Mod>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<Min>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<Max>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<EQ>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<NE>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<LT>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<LE>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<GT>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<GE>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<And>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<Or>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<Not>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<Select>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<Load>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<Ramp>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<Broadcast>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<Call>::_type_info = {};
template<> EXPORT IRNodeType ExprNode<Let>::_type_info = {};
template<> EXPORT IRNodeType StmtNode<LetStmt>::_type_info = {};
template<> EXPORT IRNodeType StmtNode<AssertStmt>::_type_info = {};
template<> EXPORT IRNodeType StmtNode<Pipeline>::_type_info = {};
template<> EXPORT IRNodeType StmtNode<For>::_type_info = {};
template<> EXPORT IRNodeType StmtNode<Store>::_type_info = {};
template<> EXPORT IRNodeType StmtNode<Provide>::_type_info = {};
template<> EXPORT IRNodeType StmtNode<Allocate>::_type_info = {};
template<> EXPORT IRNodeType StmtNode<Free>::_type_info = {};
template<> EXPORT IRNodeType StmtNode<Realize>::_type_info = {};
template<> EXPORT IRNodeType StmtNode<Block>::_type_info = {};
template<> EXPORT IRNodeType StmtNode<IfThenElse>::_type_info = {};
template<> EXPORT IRNodeType StmtNode<Evaluate>::_type_info = {};

using std::string;
const string Call::debug_to_file = "debug_to_file";
const string Call::shuffle_vector = "shuffle_vector";
const string Call::interleave_vectors = "interleave_vectors";
const string Call::reinterpret = "reinterpret";
const string Call::bitwise_and = "bitwise_and";
const string Call::bitwise_not = "bitwise_not";
const string Call::bitwise_xor = "bitwise_xor";
const string Call::bitwise_or = "bitwise_or";
const string Call::shift_left = "shift_left";
const string Call::shift_right = "shift_right";
const string Call::abs = "abs";
const string Call::lerp = "lerp";
const string Call::rewrite_buffer = "rewrite_buffer";
const string Call::profiling_timer = "profiling_timer";
const string Call::create_buffer_t = "create_buffer_t";
const string Call::extract_buffer_min = "extract_buffer_min";
const string Call::extract_buffer_extent = "extract_buffer_extent";
const string Call::popcount = "popcount";
const string Call::count_leading_zeros = "count_leading_zeros";
const string Call::count_trailing_zeros = "count_trailing_zeros";
const string Call::undef = "undef";
const string Call::trace = "trace";

}
}
