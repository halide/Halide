#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    Type i32 = Int(32);
    Type f32 = Float(32);
    Expr x = Variable::make(Int(32), "x");
    Expr y = Variable::make(Int(32), "y");
    std::ostringstream expr_source;
    expr_source << (x + 3) * (y / 2 + 17);
    internal_assert(expr_source.str() == "((x + 3)*((y/2) + 17))");

    Stmt store = Store::make("buf", (x * 17) / (x - 3), y - 1);
    Stmt for_loop = For::make("x", -2, y + 2, ForType::Parallel, Partition::Auto, DeviceAPI::Host, store);
    std::vector<Expr> args(1);
    args[0] = x % 3;
    Expr call = Call::make(i32, "buf", args, Call::Extern);
    Stmt store2 = Store::make("out", call + 1, x, Parameter(), const_true(), ModulusRemainder(3, 5), false);
    Stmt for_loop2 = For::make("x", 0, y, ForType::Vectorized, Partition::Auto, DeviceAPI::Host, store2);

    Stmt producer = ProducerConsumer::make_produce("buf", for_loop);
    Stmt consumer = ProducerConsumer::make_consume("buf", for_loop2);
    Stmt pipeline = Block::make(producer, consumer);

    Stmt assertion = AssertStmt::make(y >= 3, Call::make(Int(32), "halide_error_param_too_small_i64",
                                                         {std::string("y"), y, 3}, Call::Extern));
    Stmt block = Block::make(assertion, pipeline);
    Stmt let_stmt = LetStmt::make("y", 17, block);
    Stmt allocate = Allocate::make("buf", f32, MemoryType::Stack, {1023}, const_true(), let_stmt);

    std::ostringstream source;
    source << allocate;
    std::string correct_source =
        "allocate buf[float32 * 1023] in Stack\n"
        "let y = 17\n"
        "assert(y >= 3, halide_error_param_too_small_i64(\"y\", y, 3))\n"
        "produce buf {\n"
        " parallel (x, -2, y + 2) {\n"
        "  buf[y - 1] = (x*17)/(x - 3)\n"
        " }\n"
        "}\n"
        "consume buf {\n"
        " vectorized (x, 0, y) {\n"
        "  out[x] = buf(x % 3) + 1\n"
        " }\n"
        "}\n";

    if (source.str() != correct_source) {
        internal_error << "Correct output:\n"
                       << correct_source
                       << "Actual output:\n"
                       << source.str();
    }

    printf("Success!\n");
    return 0;
}
