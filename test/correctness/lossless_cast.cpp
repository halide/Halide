#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

bool check_lossless_cast(const Type &t, const Expr &in, const Expr &correct) {
    Expr result = lossless_cast(t, in);
    if (!equal(result, correct)) {
        std::cout << "Incorrect lossless_cast result:\n"
                  << "lossless_cast(" << t << ", " << in << ") gave:\n"
                  << " " << result
                  << " but expected was:\n"
                  << " " << correct << "\n";
        return true;
    }
    return false;
}

int main(int argc, char **argv) {
    Expr x = Variable::make(Int(32), "x");
    Type u8 = UInt(8);
    Type u16 = UInt(16);
    Type u32 = UInt(32);
    // Type u64 = UInt(64);
    Type i8 = Int(8);
    Type i16 = Int(16);
    Type i32 = Int(32);
    Type i64 = Int(64);
    Type u8x = UInt(8, 4);
    Type u16x = UInt(16, 4);
    Type u32x = UInt(32, 4);
    Expr var_u8 = Variable::make(u8, "x");
    Expr var_u16 = Variable::make(u16, "x");
    Expr var_u8x = Variable::make(u8x, "x");

    bool found_error = false;

    Expr e = cast(u8, x);
    found_error |= check_lossless_cast(i32, e, cast(i32, e));

    e = cast(u8, x);
    found_error |= check_lossless_cast(i32, e, cast(i32, e));

    e = cast(i8, var_u16);
    found_error |= check_lossless_cast(u16, e, Expr());

    e = cast(i16, var_u16);
    found_error |= check_lossless_cast(u16, e, Expr());

    e = cast(u32, var_u8);
    found_error |= check_lossless_cast(u16, e, cast(u16, var_u8));

    e = VectorReduce::make(VectorReduce::Add, cast(u16x, var_u8x), 1);
    found_error |= check_lossless_cast(u16, e, cast(u16, e));

    e = VectorReduce::make(VectorReduce::Add, cast(u32x, var_u8x), 1);
    found_error |= check_lossless_cast(u16, e, VectorReduce::make(VectorReduce::Add, cast(u16x, var_u8x), 1));

    e = cast(u32, var_u8) - 16;
    found_error |= check_lossless_cast(u16, e, Expr());

    e = cast(u32, var_u8) + 16;
    found_error |= check_lossless_cast(u16, e, cast(u16, var_u8) + 16);

    e = 16 - cast(u32, var_u8);
    found_error |= check_lossless_cast(u16, e, Expr());

    e = 16 + cast(u32, var_u8);
    found_error |= check_lossless_cast(u16, e, 16 + cast(u16, var_u8));

    // Check one where the target type is unsigned but there's a signed addition
    // (that can't overflow)
    e = cast(i64, cast(u16, var_u8) + cast(i32, 17));
    found_error |= check_lossless_cast(u32, e, cast(u32, cast(u16, var_u8)) + cast(u32, 17));

    // Check one where the target type is unsigned but there's a signed subtract
    // (that can overflow). It's not safe to enter the i16 sub
    e = cast(i64, cast(i16, 10) - cast(i16, 17));
    found_error |= check_lossless_cast(u32, e, Expr());

    e = cast(i64, 1024) * cast(i64, 1024) * cast(i64, 1024);
    found_error |= check_lossless_cast(i32, e, (cast(i32, 1024) * 1024) * 1024);

    // Check narrowing a vector reduction of something narrowable to bool ...
    auto make_reduce = [&](Type t, VectorReduce::Operator op) {
        return VectorReduce::make(op,
                                  cast(t.with_lanes(4), Ramp::make(x, 1, 4) > 4), 2);
    };

    // It's OK to narrow it to 8-bit.
    e = make_reduce(UInt(16), VectorReduce::Add);
    found_error |= check_lossless_cast(UInt(8), e, make_reduce(UInt(8), VectorReduce::Add));

    // ... but we can't reduce it all the way to bool if the operator isn't
    // legal for bools (issue #9011)
    e = make_reduce(UInt(8), VectorReduce::Add);
    found_error |= check_lossless_cast(Bool(), e, Expr());

    // Min or Max, however, can just become And and Or
    e = make_reduce(UInt(8), VectorReduce::Min);
    found_error |= check_lossless_cast(Bool(), e, make_reduce(Bool(), VectorReduce::And));

    e = make_reduce(UInt(8), VectorReduce::Max);
    found_error |= check_lossless_cast(Bool(), e, make_reduce(Bool(), VectorReduce::Or));

    // Runtime test: verify that lossless_cast of a widening_sub expression
    // evaluates correctly when vectorized. This is a regression test for a bug
    // in lossless_negate where it incorrectly negated through an unsigned-to-signed
    // cast, causing FindIntrinsics to generate wrong code for the vectorized case.
    {
        Var x("x");
        Buffer<uint8_t> buf(1024, "buf");
        for (int i = 0; i < 1024; i++) {
            buf(i) = (uint8_t)i;
        }

        // A = int8(-16 + 32 / int8(buf(x)))
        Expr a = cast(Int(8), -16) + cast(Int(8), 32) / cast(Int(8), cast(UInt(8), buf(x)));
        // inner = (a / -33_i8) * -33_i8  (in int8 Euclidean arithmetic)
        Expr inner = (a / cast(Int(8), -33)) * cast(Int(8), -33);
        // b = 223_u8 * uint8(inner)
        Expr b = cast(UInt(8), 223) * cast(UInt(8), inner);

        // e1: (int64)widening_sub(int32(int16(a)), int32(uint16(b)))
        Expr e1 = cast(Int(64), widening_sub(cast(Int(32), cast(Int(16), a)),
                                             cast(Int(32), cast(UInt(16), b))));

        // lossless_cast to int16 - the returned expression should evaluate
        // identically to e1 when vectorized.
        Expr e2 = lossless_cast(Int(16), e1);
        if (!e2.defined()) {
            std::cerr << "Runtime regression test: lossless_cast unexpectedly returned undefined\n";
            found_error = true;
        } else {
            Func f;
            f(x) = {e1, cast(Int(64), e2)};
            f.vectorize(x, 4, TailStrategy::RoundUp);

            Buffer<int64_t> out1(1024), out2(1024);
            Pipeline p(f);
            p.realize({out1, out2});

            for (int i = 0; i < 1024; i++) {
                if (out1(i) != out2(i)) {
                    std::cerr << "Runtime regression test: mismatch at x=" << i
                              << ": original=" << out1(i)
                              << " lossless_cast=" << out2(i) << "\n";
                    found_error = true;
                    break;
                }
            }
        }
    }

    // Static tests for the Cast case in lossless_negate with signed integer inner types.
    // We use Cast::make (not the cast() function) to prevent constant folding of integer
    // literals, so that lossless_negate actually sees a Cast node.
    //
    // The invariant: lossless_negate(Cast(outer, inner)) must return Expr() when the
    // inner expression's bounds include INT_TYPE_MIN, because:
    //   cast(outer, -int8(-128)) = cast(outer, -128)  [wraps in int8]
    //     ≠ -(cast(outer, -128)) = -(outer)(-128)     [exact in outer type]
    {
        // Inner = Int(8)(-128) = INT8_MIN. Pushing negation through would give
        // cast(int16, -int8(-128)) = cast(int16, -128) = -128, not the correct 128.
        Expr neg_i8_min = lossless_negate(Cast::make(Int(16), IntImm::make(Int(8), -128)));
        if (neg_i8_min.defined()) {
            std::cerr << "Int(8) INT_MIN cast-negate test: expected Expr(), got " << neg_i8_min << "\n";
            found_error = true;
        }

        // Inner = Int(16)(-32768) = INT16_MIN. Same reasoning.
        Expr neg_i16_min = lossless_negate(Cast::make(Int(32), IntImm::make(Int(16), -32768)));
        if (neg_i16_min.defined()) {
            std::cerr << "Int(16) INT_MIN cast-negate test: expected Expr(), got " << neg_i16_min << "\n";
            found_error = true;
        }

        // Non-INT_MIN: lossless_negate(Cast(Int(16), Int(8)(-127))) must return
        // 127_i16 since -127 ≠ INT8_MIN so negation is exact.
        // lossless_negate(int8(-127)) = int8(127); lossless_cast(int16, int8(127))
        // constant-folds to the literal 127_i16.
        Expr neg_i8_ok = lossless_negate(Cast::make(Int(16), IntImm::make(Int(8), -127)));
        Expr expected_ok = IntImm::make(Int(16), 127);
        if (!neg_i8_ok.defined() || !equal(neg_i8_ok, expected_ok)) {
            std::cerr << "Int(8) non-INT_MIN cast-negate test: expected " << expected_ok
                      << ", got " << neg_i8_ok << "\n";
            found_error = true;
        }
    }

    // Runtime tests: verify that widening_sub expressions containing values that
    // include INT8_MIN / INT16_MIN evaluate correctly when lossless_cast narrows
    // them. The Cast case in lossless_negate must reject the inner signed int when
    // its bounds include INT_TYPE_MIN and let the code fall back to widening_sub.
    {
        Var x("x");
        Buffer<uint8_t> buf(16, "buf");
        for (int i = 0; i < 16; i++) {
            buf(i) = (uint8_t)(i * 16);  // buf(8)=128 → cast(int8, 128) = -128 = INT8_MIN
        }

        Expr inner_i8 = cast(Int(8), cast(UInt(8), buf(x)));
        Expr a_i16 = cast(Int(16), inner_i8);
        Expr b_i16 = cast(Int(16), inner_i8);

        // e1: widening_sub as the reference — correct regardless of INT_MIN
        Expr e1 = widening_sub(a_i16, b_i16);

        // e2: lossless_cast(Int(16), e1) = a_i16 - b_i16.  When vectorized,
        // FindIntrinsics calls lossless_negate(cast(Int(16), inner_i8)) on the
        // subtrahend. Because inner_i8 can be INT8_MIN, the Cast case must reject
        // it and fall through to widening_sub.
        Expr e2 = lossless_cast(Int(16), e1);
        if (!e2.defined()) {
            std::cerr << "Signed Int(8) runtime test: lossless_cast unexpectedly returned undefined\n";
            found_error = true;
        } else {
            Func f;
            f(x) = {e1, cast(Int(32), e2)};
            f.vectorize(x, 4, TailStrategy::RoundUp);

            Buffer<int32_t> out1(16), out2(16);
            Pipeline p(f);
            p.realize({out1, out2});

            for (int i = 0; i < 16; i++) {
                if (out1(i) != out2(i)) {
                    std::cerr << "Signed Int(8) runtime test: mismatch at x=" << i
                              << ": widening_sub=" << out1(i)
                              << " lossless_cast=" << out2(i) << "\n";
                    found_error = true;
                    break;
                }
            }
        }
    }

    // Same test for Int(16), where INT16_MIN = -32768.
    {
        Var x("x");
        Buffer<int16_t> buf(16, "buf");
        for (int i = 0; i < 16; i++) {
            buf(i) = (int16_t)(i * 4096 - 32768);  // -32768, -28672, ..., 28672 (includes INT16_MIN)
        }

        Expr inner_i16 = cast(Int(16), buf(x));
        Expr a_i32 = cast(Int(32), inner_i16);
        Expr b_i32 = cast(Int(32), inner_i16);

        Expr e1 = widening_sub(a_i32, b_i32);
        Expr e2 = lossless_cast(Int(32), e1);
        if (!e2.defined()) {
            std::cerr << "Signed Int(16) runtime test: lossless_cast unexpectedly returned undefined\n";
            found_error = true;
        } else {
            Func f;
            f(x) = {e1, cast(Int(64), e2)};
            f.vectorize(x, 4, TailStrategy::RoundUp);

            Buffer<int64_t> out1(16), out2(16);
            Pipeline p(f);
            p.realize({out1, out2});

            for (int i = 0; i < 16; i++) {
                if (out1(i) != out2(i)) {
                    std::cerr << "Signed Int(16) runtime test: mismatch at x=" << i
                              << ": widening_sub=" << out1(i)
                              << " lossless_cast=" << out2(i) << "\n";
                    found_error = true;
                    break;
                }
            }
        }
    }

    // Runtime test: a widening_mul whose operand is a narrowing cast
    // (int32 -> int16) of a widening_add used to send FindIntrinsics into
    // infinite recursion. strip_widening_cast could return an expression wider
    // than its input, which the widening_mul narrowing rewrite would re-widen,
    // ping-ponging the multiply between its int32 and int64 forms forever. This
    // must simply compile, and the vectorized result must match a scalar
    // reference.
    {
        Var x("x");
        Buffer<int8_t> buf(64, "buf");
        for (int i = 0; i < 64; i++) {
            buf(i) = (int8_t)(i - 32);
        }

        Expr t = buf(x);
        Expr inner = cast(Int(16), cast(UInt(8), t) + cast(UInt(8), 68));
        Expr a = widen_right_add(inner, t);
        Expr b = widen_right_mul(a, cast(Int(8), a) * cast(Int(8), 39));
        Expr wide_add = widening_add(a, b);
        Expr e = cast(Int(64), widening_mul(cast(Int(16), 73), cast(Int(16), wide_add)));

        Func f_vec, f_ref;
        f_vec(x) = e;
        f_ref(x) = e;
        f_vec.vectorize(x, 4, TailStrategy::RoundUp);

        Buffer<int64_t> out_vec = f_vec.realize({64});
        Buffer<int64_t> out_ref = f_ref.realize({64});

        for (int i = 0; i < 64; i++) {
            if (out_vec(i) != out_ref(i)) {
                std::cerr << "Recursion regression test: mismatch at x=" << i
                          << ": ref=" << out_ref(i) << " vectorized=" << out_vec(i) << "\n";
                found_error = true;
                break;
            }
        }
    }

    if (found_error) {
        return 1;
    }

    std::cout << "Success!\n";
    return 0;
}
