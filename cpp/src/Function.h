#ifndef HALIDE_FUNCTION_H
#define HALIDE_FUNCTION_H

#include "IntrusivePtr.h"
#include "Reduction.h"
#include "Schedule.h"
#include <string>
#include <vector>

namespace Halide { 
namespace Internal {

using std::vector;
using std::string;

        
struct FunctionContents {
    mutable RefCount ref_count;
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

    void rename(const std::string &n) {
        contents.ptr->name = n;
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
    void (*set_custom_allocator)(void *(*malloc)(size_t), void (*free)(void *));
    JITCompiledModule() : function(NULL), wrapped_function(NULL), set_error_handler(NULL), set_custom_allocator(NULL) {}
};
        
}}

#endif
