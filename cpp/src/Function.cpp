#include "IR.h"
#include "Function.h"

namespace Halide {
namespace Internal {

template<>
int &ref_count<FunctionContents>(const FunctionContents *f) {return f->ref_count;}

template<>
void destroy<FunctionContents>(const FunctionContents *f) {delete f;}

void Function::define(const vector<string> &args, Expr value) {
    if (!contents.defined()) {
        contents = new FunctionContents;
        contents.ptr->name = unique_name('f');
    }

    assert(!contents.ptr->value.defined() && "Function is already defined");
    contents.ptr->value = value;
    contents.ptr->args = args;
        
    for (size_t i = 0; i < args.size(); i++) {
        Schedule::Dim d = {args[i], For::Serial};
        contents.ptr->schedule.dims.push_back(d);
    }        
}

}
}
