#include <stdio.h>
#include <Halide.h>

// Test the simplifier in Halide by testing for equivalence of randomly generated expressions.

using namespace std;
using namespace Halide;
using namespace Halide::Internal;

Var a("a"), b("b"), c("c"), d("d"), e("e");
Var random_vars[] = { a, b, c, d, e };
const int random_var_count = sizeof(random_vars)/sizeof(random_vars[0]);

Expr random_leaf(Type T, bool imm_only = false) {
    if (T.is_scalar()) {
        int var = rand()%random_var_count + 1;
        if (!imm_only && var < random_var_count) {
            return cast(T, random_vars[var]);
        } else {
            if (T == Int(32)) {
                // For Int(32), we don't care about correctness during
                // overflow, so just use numbers that are unlikely to
                // overflow.
                return cast(T, rand()%256 - 128);
            } else {
                return cast(T, rand() - RAND_MAX/2);
            }
        }
    } else {
        if (rand() % 2 == 0) {
            return Ramp::make(random_leaf(T.element_of()), random_leaf(T.element_of()), T.width);
        } else {
            return Broadcast::make(random_leaf(T.element_of()), T.width);
        }
    }
}

Expr random_expr(Type T, int depth);

Expr random_condition(Type T, int depth) {
    typedef Expr (*make_bin_op_fn)(Expr, Expr);
    static make_bin_op_fn make_bin_op[] = {
        EQ::make,
        NE::make,
        LT::make,
        LE::make,
        GT::make,
        GE::make,
    };
    const int op_count = sizeof(make_bin_op)/sizeof(make_bin_op[0]);

    Expr a = random_expr(T, depth);
    Expr b = random_expr(T, depth);
    int op = rand()%op_count;
    return make_bin_op[op](a, b);
}

Expr random_expr(Type T, int depth) {
    typedef Expr (*make_bin_op_fn)(Expr, Expr);
    static make_bin_op_fn make_bin_op[] = {
        Add::make,
        Sub::make,
        Mul::make,
        Min::make,
        Max::make,
        Div::make,
        Mod::make,
     };

    static make_bin_op_fn make_bool_bin_op[] = {
        And::make,
        Or::make,
    };

    if (depth-- <= 0) {
        return random_leaf(T);
    }

    const int bin_op_count = sizeof(make_bin_op) / sizeof(make_bin_op[0]);
    const int bool_bin_op_count = sizeof(make_bool_bin_op) / sizeof(make_bool_bin_op[0]);
    const int op_count = bin_op_count + bool_bin_op_count + 5;

    int op = rand() % op_count;
    switch(op) {
    case 0: return random_leaf(T);
    case 1: return Select::make(random_condition(T, depth),
                                random_expr(T, depth),
                                random_expr(T, depth));

        // Ramp/Broadcast
    case 2:
    case 3:
        if (T.width != 1) {
            if (op == 3) {
                return Ramp::make(random_expr(T.element_of(), depth),
                                  random_expr(T.element_of(), depth),
                                  T.width);
            } else {
                return Broadcast::make(random_expr(T.element_of(), depth),
                                       T.width);
            }
        }
        return random_expr(T, depth);

    case 4:
        if (T.is_bool()) {
            return Not::make(random_expr(T, depth));
        } else {
            return random_expr(T, depth);
        }
    case 5:
        if (T.is_bool()) {
            return random_condition(T, depth);
        } else {
            return random_expr(T, depth);
        }

    default:
        make_bin_op_fn maker;
        if (T.is_bool()) {
            maker = make_bool_bin_op[op%bool_bin_op_count];
        } else {
            maker = make_bin_op[op%bin_op_count];
        }
        Expr a = random_expr(T, depth);
        Expr b = random_expr(T, depth);
        return maker(a, b);
    }
}

bool test_simplification(Expr a, Expr b, Type T, const map<string, Expr> &vars) {
    for (int j = 0; j < T.width; j++) {
        Expr a_j = a;
        Expr b_j = b;
        if (T.width != 1) {
            a_j = extract_lane(a, j);
            b_j = extract_lane(b, j);
        }

        Expr a_j_v = simplify(substitute(vars, a_j));
        Expr b_j_v = simplify(substitute(vars, b_j));
        // If the simplifier didn't produce constants, there must be
        // undefined behavior in this expression. Ignore it.
        if (!is_const(a_j_v) || !is_const(b_j_v)) {
            continue;
        }
        if (!equal(a_j_v, b_j_v)) {
            for(map<string, Expr>::const_iterator i = vars.begin(); i != vars.end(); i++) {
                std::cout << i->first << " = " << i->second << '\n';
            }

            std::cout << a << '\n';
            std::cout << b << '\n';
            std::cout << "In vector lane " << j << ":\n";
            std::cout << a_j << " -> " << a_j_v << '\n';
            std::cout << b_j << " -> " << b_j_v << '\n';
            return false;
        }
    }
    return true;
}

bool test_expression(Expr test, int samples) {
    Expr simplified = simplify(test);

    map<string, Expr> vars;
    for (int v = 0; v < random_var_count; ++v) {
        vars[random_vars[v].name()] = Expr();
    }

    for (int i = 0; i < samples; i++) {
        for (std::map<string, Expr>::iterator v = vars.begin(); v != vars.end(); v++) {
            v->second = random_leaf(test.type().element_of(), true);
        }

        if (!test_simplification(test, simplified, test.type(), vars)) {
            return false;
        }
    }
    return true;
}

int main(int argc, char **argv) {
    // Number of random expressions to test.
    const int count = 1000;
    // Depth of the randomly generated expression trees.
    const int depth = 5;
    // Number of samples to test the generated expressions for.
    const int samples = 3;

    // We want different fuzz tests every time, to increase coverage.
    // We also report the seed to enable reproducing failures.
    int fuzz_seed = time(NULL);
    srand(fuzz_seed);
    std::cout << "Simplify fuzz test seed: " << fuzz_seed << '\n';

    Type fuzz_types[] = {
        Int(8),
        Int(16),
        Int(32),
        UInt(1),
        UInt(8),
        UInt(16),
        UInt(32),
    };

    int max_fuzz_vector_width = 4;

    for (size_t i = 0; i < sizeof(fuzz_types)/sizeof(fuzz_types[0]); i++) {
        Type T = fuzz_types[i];
        for (int w = 1; w < max_fuzz_vector_width; w *= 2) {
            Type VT = T.vector_of(w);
            for (int n = 0; n < count; n++) {
                // Generate a random expr...
                Expr test = random_expr(VT, depth);
                test_expression(test, samples);
            }
        }
    }
    std::cout << "Success!" << std::endl;
    return 0;
}

