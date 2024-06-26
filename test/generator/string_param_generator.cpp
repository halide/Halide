#include "Halide.h"

using namespace Halide;

namespace {
class StringParam : public Halide::Generator<StringParam> {
public:
    GeneratorParam<std::string> rpn{"rpn_expr", ""};

    Output<Buffer<int, 2>> output{"output"};

    void generate() {
        // Remove cmake extra skip characters if any exist.
        const std::string value = Halide::Internal::replace_all(rpn.value(), "\\ ", " ");
        std::vector<std::string> tokens = Halide::Internal::split_string(value, " ");
        std::stack<Halide::Expr> exprs;
        // Assume input is a valid RPN expression no checks for simplicity.
        for (const std::string &token : tokens) {
            bool is_op = (token == "+" || token == "-" || token == "*" || token == "/");
            bool is_var = (token == "x" || token == "y");
            if (is_var) {
                if (token == "x") {
                    exprs.push(x);
                } else {
                    exprs.push(y);
                }
            } else if (is_op) {
                Halide::Expr a = exprs.top();
                exprs.pop();
                Halide::Expr b = exprs.top();
                exprs.pop();
                if (token == "+") {
                    exprs.push(a + b);
                } else if (token == "-") {
                    exprs.push(a - b);
                } else if (token == "*") {
                    exprs.push(a * b);
                } else {
                    exprs.push(a / b);
                }
            } else {
                // Numerical constant.
                exprs.push(std::stoi(token));
            }
        }

        output(x, y) = exprs.top();
    }

    Var x, y;
};
}  // namespace

HALIDE_REGISTER_GENERATOR(StringParam, string_param);
