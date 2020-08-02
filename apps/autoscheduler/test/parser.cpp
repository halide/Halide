#include "test.h"
#include "LoopNestParser.h"

using namespace Halide;
using namespace Halide::Internal;
using namespace Halide::Internal::Autoscheduler;

void test_parser() {
    {
        std::unique_ptr<LoopNestParser> complete = LoopNestParser::from_file("test/test_complete_loop_nest");
        complete->dump();
        std::unique_ptr<LoopNestParser> partial = LoopNestParser::from_file("test/test_partial_loop_nest");
        partial->dump();
        EXPECT(complete->contains_sub_loop_nest(*partial));
    }
}

int main(int argc, char **argv) {
    test_parser();
    printf("All tests passed.\n");
    return 0;
}
