#ifndef HALIDE_FUNCTION_H
#define HALIDE_FUNCTION_H

#include "IntrusivePtr.h"
#include <string>
#include <vector>


namespace Halide { 
namespace Internal {

using std::vector;
using std::string;
        
struct Schedule {
    string store_level, compute_level;
        
    struct Split {
        string old_var, outer, inner;
        Expr factor;
    };
    vector<Split> splits;
        
    struct Dim {
        string var;
        For::ForType for_type;
    };
    vector<Dim> dims;

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
    // TODO: reduction step lhs, rhs, and schedule
};        

class Function {
private:
    IntrusivePtr<FunctionContents> contents;
public:
    Function() : contents(NULL) {}

    void define(const vector<string> &args, Expr value);   

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

    bool defined() const {
        return contents.defined();
    }

    bool same_as(const Function &other) {
        return contents.same_as(other.contents);
    }

};
        
}}

#endif
