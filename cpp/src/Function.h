#ifndef HALIDE_FUNCTION_H
#define HALIDE_FUNCTION_H

#include "IntrusivePtr.h"
#include "Reduction.h"
#include "Schedule.h"
#include <string>
#include <vector>

namespace Halide { 
namespace Internal {
        
struct FunctionContents {
    mutable RefCount ref_count;
    std::string name;
    std::vector<std::string> args;
    Expr value;
    Schedule schedule;

    Expr reduction_value;
    std::vector<Expr> reduction_args;
    Schedule reduction_schedule;
    ReductionDomain reduction_domain;
};        

class Function {
private:
    IntrusivePtr<FunctionContents> contents;
public:
    Function() : contents(NULL) {}

    void define(const std::vector<std::string> &args, Expr value);   
    void define_reduction(const std::vector<Expr> &args, Expr value);

    Function(const std::string &n) : contents(new FunctionContents) {
        contents.ptr->name = n;
    }

    const std::string &name() const {
        return contents.ptr->name;
    }

    const std::vector<std::string> &args() const {
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

    const std::vector<Expr> &reduction_args() const {
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

/** Function pointers into a compiled halide module. */
struct JITCompiledModule {
    /** A pointer to the raw halide function. It's true type depends
     * on the Argument vector passed to CodeGen::compile. Image
     * parameters become (buffer_t *), and scalar parameters become
     * pointers to the appropriate values. The final argument is a
     * pointer to the buffer_t defining the output. */
    void *function;

    /** A slightly more type-safe wrapper around the raw halide
     * module. Takes it arguments as an array of pointers that
     * correspond to the arguments to \ref function */
    void (*wrapped_function)(const void **);

    /** The type of a halide runtime error handler function */
    typedef void (*ErrorHandler)(char *);

    /** Set the runtime error handler for this module */
    void (*set_error_handler)(ErrorHandler);

    /** Set a custom malloc and free for this module to use. Malloc
     * should return 32-byte aligned chunks of memory, with 32-bytes
     * extra allocated on the start and end so that vector loads can
     * spill off the end slightly. Metadata (e.g. the base address of
     * the region allocated) can go in this margin - it is only read,
     * not written. */
    void (*set_custom_allocator)(void *(*malloc)(size_t), void (*free)(void *));

    JITCompiledModule() : function(NULL), wrapped_function(NULL), set_error_handler(NULL), set_custom_allocator(NULL) {}
};
        
}}

#endif
