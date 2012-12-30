#ifndef HALIDE_FUNCTION_H
#define HALIDE_FUNCTION_H

#include "IntrusivePtr.h"
#include "Reduction.h"
#include <string>
#include <vector>

namespace Halide { 
namespace Internal {

using std::vector;
using std::string;
        
struct Schedule {
    struct LoopLevel {
        string func, var;

        // Some variable of some function
        LoopLevel(string f, string v) : func(f), var(v) {}

        // Represents inline
        LoopLevel() {} 
        bool is_inline() const {return var.empty();}

        // Represents outside the outermost loop
        static LoopLevel root() {
            return LoopLevel("", "<root>");
        }
        bool is_root() const {return var == "<root>";}

        // Does this loop level refer to a particular for loop variable?
        bool match(const string &loop) const {
            return starts_with(loop, func + ".") && ends_with(loop, "." + var);
        }

    };

    // At what granularity do we store and compute values of this function?
    LoopLevel store_level, compute_level;
      
    // Which dimensions have been split into inner and outer sub-dimensions
    struct Split {
        string old_var, outer, inner;
        Expr factor;
    };
    vector<Split> splits;
        
    // The list of dimensions after splits, and how to traverse each dimension
    struct Dim {
        string var;
        For::ForType for_type;
    };
    vector<Dim> dims;

    // Any (optional) explicit bounds on dimensions
    struct Bound {
        string var;
        Expr min, extent;
    };
    vector<Bound> bounds;
};
        
struct FunctionContents {
    mutable int ref_count;
    string name;
    vector<string> args;
    Expr value;
    Schedule schedule;

    Expr reduction_value;
    vector<Expr> reduction_args;
    Schedule reduction_schedule;
    ReductionDomain reduction_domain;
};        

class Function {
private:
    IntrusivePtr<FunctionContents> contents;
public:
    Function() : contents(NULL) {}

    void define(const vector<string> &args, Expr value);   
    void define_reduction(const vector<Expr> &args, Expr value);

    Function(const string &n) : contents(new FunctionContents) {
        contents.ptr->name = n;
    }

    const string &name() const {
        return contents.ptr->name;
    }

    const vector<string> &args() const {
        return contents.ptr->args;
    }

    Expr value() const {
        return contents.ptr->value;
    }

    Schedule &schedule() {
        return contents.ptr->schedule;
    }   

    const Schedule &schedule() const {
        return contents.ptr->schedule;
    }   

    Schedule &reduction_schedule() {
        return contents.ptr->reduction_schedule;
    }

    const Schedule &reduction_schedule() const {
        return contents.ptr->reduction_schedule;
    }

    Expr reduction_value() const {
        return contents.ptr->reduction_value;
    }

    const vector<Expr> &reduction_args() const {
        return contents.ptr->reduction_args;        
    }

    ReductionDomain reduction_domain() const {
        return contents.ptr->reduction_domain;
    }

    bool is_reduction() const {
        return reduction_value().defined();
    }

    bool defined() const {
        return contents.defined();
    }

    bool same_as(const Function &other) {
        return contents.same_as(other.contents);
    }

};

struct JITCompiledModule {
    void *function;
    void (*wrapped_function)(const void **);
    typedef void (*ErrorHandler)(char *);
    void (*set_error_handler)(ErrorHandler);
    JITCompiledModule() : function(NULL), wrapped_function(NULL), set_error_handler(NULL) {}
};
        
}}

#endif
