#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

int check_lossless_cast(const Type &t, const Expr &in, const Expr &correct) {
    Expr result = lossless_cast(t, in);
    if (!equal(result, correct)) {
        std::cout << "Incorrect lossless_cast result:\nlossless_cast("
                  << t << ", " << in << ") gave:\n " << result
                  << " but expected was:\n " << correct << "\n";
        return 1;
    }
    return 0;
}

int lossless_cast_test() {
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

    int res = 0;

    Expr e = cast(u8, x);
    res |= check_lossless_cast(i32, e, cast(i32, e));

    e = cast(u8, x);
    res |= check_lossless_cast(i32, e, cast(i32, e));

    e = cast(i8, var_u16);
    res |= check_lossless_cast(u16, e, Expr());

    e = cast(i16, var_u16);
    res |= check_lossless_cast(u16, e, Expr());

    e = cast(u32, var_u8);
    res |= check_lossless_cast(u16, e, cast(u16, var_u8));

    e = VectorReduce::make(VectorReduce::Add, cast(u16x, var_u8x), 1);
    res |= check_lossless_cast(u16, e, cast(u16, e));

    e = VectorReduce::make(VectorReduce::Add, cast(u32x, var_u8x), 1);
    res |= check_lossless_cast(u16, e, VectorReduce::make(VectorReduce::Add, cast(u16x, var_u8x), 1));

    e = cast(u32, var_u8) - 16;
    res |= check_lossless_cast(u16, e, Expr());

    e = cast(u32, var_u8) + 16;
    res |= check_lossless_cast(u16, e, cast(u16, var_u8) + 16);

    e = 16 - cast(u32, var_u8);
    res |= check_lossless_cast(u16, e, Expr());

    e = 16 + cast(u32, var_u8);
    res |= check_lossless_cast(u16, e, 16 + cast(u16, var_u8));

    // Check one where the target type is unsigned but there's a signed addition
    // (that can't overflow)
    e = cast(i64, cast(u16, var_u8) + cast(i32, 17));
    res |= check_lossless_cast(u32, e, cast(u32, cast(u16, var_u8)) + cast(u32, 17));

    // Check one where the target type is unsigned but there's a signed subtract
    // (that can overflow). It's not safe to enter the i16 sub
    e = cast(i64, cast(i16, 10) - cast(i16, 17));
    res |= check_lossless_cast(u32, e, Expr());

    e = cast(i64, 1024) * cast(i64, 1024) * cast(i64, 1024);
    res |= check_lossless_cast(i32, e, (cast(i32, 1024) * 1024) * 1024);

    return 0;

    // return res;
}

constexpr int size = 1024;
Buffer<uint8_t> buf_u8(size, "buf_u8");
Buffer<int8_t> buf_i8(size, "buf_i8");
Var x{"x"};

Expr random_expr(std::mt19937 &rng) {
    std::vector<Expr> exprs;
    // Add some atoms
    exprs.push_back(cast<uint8_t>((uint8_t)rng()));
    exprs.push_back(cast<int8_t>((int8_t)rng()));
    exprs.push_back(cast<uint8_t>((uint8_t)rng()));
    exprs.push_back(cast<int8_t>((int8_t)rng()));
    exprs.push_back(buf_u8(x));
    exprs.push_back(buf_i8(x));

    // Make random combinations of them
    while (true) {
        Expr e;
        int i1 = rng() % exprs.size();
        int i2 = rng() % exprs.size();
        int op = rng() % 7;
        Expr e1 = exprs[i1];
        Expr e2 = cast(e1.type(), exprs[i2]);
        bool may_widen = e1.type().bits() < 64;
        switch (op) {
        case 0:
            if (may_widen) {
                e = cast(e1.type().widen(), e1);
            }
            break;
        case 1:
            if (may_widen) {
                e = cast(Int(e1.type().bits() * 2), e1);
            }
            break;
        case 2:
            e = e1 + e2;
            break;
        case 3:
            e = e1 - e2;
            break;
        case 4:
            e = e1 * e2;
            break;
        case 5:
            e = e1 / e2;
            break;
        case 6:
            switch (rng() % 10) {
            case 0:
                if (may_widen) {
                    e = widening_add(e1, e2);
                }
                break;
            case 1:
                if (may_widen) {
                    e = widening_sub(e1, e2);
                }
                break;
            case 2:
                if (may_widen) {
                    e = widening_mul(e1, e2);
                }
                break;
            case 3:
                e = halving_add(e1, e2);
                break;
            case 4:
                e = rounding_halving_add(e1, e2);
                break;
            case 5:
                e = halving_sub(e1, e2);
                break;
            case 6:
                e = saturating_add(e1, e2);
                break;
            case 7:
                e = saturating_sub(e1, e2);
                break;
            case 8:
                e = count_leading_zeros(e1);
                break;
            case 9:
                e = count_trailing_zeros(e1);
                break;
            }
        }

        if (!e.defined()) {
            continue;
        }

        // Stop when we get to 64 bits, but probably don't stop on a widening
        // cast, because that'll just get trivially stripped.
        if (e.type().bits() == 64 && (op > 1 || ((rng() & 7) == 0))) {
            return e;
        }

        exprs.push_back(e);
    }
}

class CheckForIntOverflow : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Call *op) override {
        if (op->is_intrinsic(Call::signed_integer_overflow)) {
            found_overflow = true;
            return make_zero(op->type);
        } else {
            return IRMutator::visit(op);
        }
    }

public:
    bool found_overflow = false;
};

bool found_error = false;

int test_one(uint32_t seed) {
    std::mt19937 rng{seed};

    buf_u8.fill(rng);
    buf_i8.fill(rng);

    Expr e1 = random_expr(rng);
    Type target;
    std::vector<Type> target_types = {UInt(32), Int(32), UInt(16), Int(16)};
    target = target_types[rng() % target_types.size()];
    Expr e2 = lossless_cast(target, e1);
    if (!e2.defined()) {
        return 0;
    }

    Func f;
    f(x) = {cast<int64_t>(e1), cast<int64_t>(e2)};
    f.vectorize(x, 4, TailStrategy::RoundUp);

    // std::cout << e1 << " to " << target << "\n  -> " << e2 << "\n  -> " << simplify(e2) << "\n";
    // std::cout << "\n\n\n--------------------\n\n\n";
    Buffer<int64_t> out1(size), out2(size);
    Pipeline p(f);
    CheckForIntOverflow checker;
    p.add_custom_lowering_pass(&checker, nullptr);
    p.realize({out1, out2});

    if (checker.found_overflow) {
        // We don't do anything in the expression generator to avoid signed
        // integer overflow, so just skip anything with signed integer overflow.
        return 0;
    }

    for (int x = 0; x < size; x++) {
        if (out1(x) != out2(x)) {
            std::cout
                << "seed = " << seed << "\n"
                << "x = " << x << "\n"
                << "buf_u8 = " << (int)buf_u8(x) << "\n"
                << "buf_i8 = " << (int)buf_i8(x) << "\n"
                << "out1 = " << out1(x) << "\n"
                << "out2 = " << out2(x) << "\n"
                << "Original: " << e1 << "\n"
                << "Lossless cast: " << e2 << "\n";
            return 1;
        }
    }

    return 0;
}

int fuzz_test(uint32_t root_seed) {
    std::mt19937 seed_generator(root_seed);

    std::cout << "Fuzz testing with root seed " << root_seed << "\n";
    for (int i = 0; i < 1000; i++) {
        if (test_one(seed_generator())) {
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2) {
        return test_one(atoi(argv[1]));
    }
    if (lossless_cast_test()) {
        std::cout << "lossless_cast test failed!\n";
        return 1;
    }
    if (fuzz_test(time(NULL))) {
        std::cout << "lossless_cast fuzz test failed!\n";
        return 1;
    }
    std::cout << "Success!\n";
    return 0;
}
