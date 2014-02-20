#include <sstream>

#include "CodeGen_GPU_Host.h"
#include "CodeGen_PTX_Dev.h"
#include "CodeGen_OpenCL_Dev.h"
#include "CodeGen_SPIR_Dev.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Debug.h"
#include "CodeGen_Internal.h"
#include "Util.h"
#include "Bounds.h"
#include "Simplify.h"

#ifdef _MSC_VER
// TODO: This is untested
#define NOMINMAX
#include <windows.h>
static bool have_symbol(const char *s) {
    return GetProcAddress(GetModuleHandle(NULL), s) != NULL;
}
#else
#include <dlfcn.h>
static bool have_symbol(const char *s) {
    return dlsym(NULL, s) != NULL;
}
#endif

namespace Halide {
namespace Internal {

extern "C" { typedef struct CUctx_st *CUcontext; }

// A single global cuda context to share between jitted functions
int (*cuCtxDestroy)(CUctx_st *) = 0;
struct SharedCudaContext {
    CUctx_st *ptr;

    // Will be created on first use by a jitted kernel that uses it
    SharedCudaContext() : ptr(0) {
    }

    // Freed on program exit
    ~SharedCudaContext() {
        debug(1) << "Cleaning up cuda context: " << ptr << ", " << cuCtxDestroy << "\n";
        if (ptr && cuCtxDestroy) {
            (*cuCtxDestroy)(ptr);
            ptr = 0;
        }
    }
} cuda_ctx;

// A single global OpenCL context and command queue to share among modules.
void * cl_ctx = 0;
void * cl_q = 0;

using std::vector;
using std::string;
using std::map;

using namespace llvm;


// Sniff the contents of a kernel to extracts the bounds of all the
// thread indices (so we know how many threads to launch), and the max
// size of each allocation (so we know how much local and shared
// memory to allocate).
class ExtractBounds : public IRVisitor {
public:

    Expr thread_extent[4];
    Expr block_extent[4];
    map<string, Expr> shared_allocations;
    map<string, Expr> local_allocations;

    ExtractBounds(Stmt s) : inside_thread(false) {
        s.accept(this);
        for (int i = 0; i < 4; i++) {
            if (!thread_extent[i].defined()) {
                thread_extent[i] = 1;
            } else {
                thread_extent[i] = simplify(thread_extent[i]);
            }
            if (!block_extent[i].defined()) {
                block_extent[i] = 1;
            } else {
                block_extent[i] = simplify(block_extent[i]);
            }
        }
    }

private:

    bool inside_thread;
    Scope<Interval> scope;

    using IRVisitor::visit;

    Expr unify(Expr a, Expr b) {
        if (!a.defined()) return b;
        if (!b.defined()) return a;
        return Halide::max(a, b);
    }

    void visit(const For *loop) {
        // What's the largest the extent could be?
        Expr max_extent = bounds_of_expr_in_scope(loop->extent, scope).max;

        bool old_inside_thread = inside_thread;

        if (ends_with(loop->name, ".threadidx")) {
            thread_extent[0] = unify(thread_extent[0], max_extent);
            inside_thread = true;
        } else if (ends_with(loop->name, ".threadidy")) {
            thread_extent[1] = unify(thread_extent[1], max_extent);
            inside_thread = true;
        } else if (ends_with(loop->name, ".threadidz")) {
            thread_extent[2] = unify(thread_extent[2], max_extent);
            inside_thread = true;
        } else if (ends_with(loop->name, ".threadidw")) {
            thread_extent[3] = unify(thread_extent[3], max_extent);
            inside_thread = true;
        } else if (ends_with(loop->name, ".blockidx")) {
            block_extent[0] = unify(block_extent[0], max_extent);
        } else if (ends_with(loop->name, ".blockidy")) {
            block_extent[1] = unify(block_extent[1], max_extent);
        } else if (ends_with(loop->name, ".blockidz")) {
            block_extent[2] = unify(block_extent[2], max_extent);
        } else if (ends_with(loop->name, ".blockidw")) {
            block_extent[3] = unify(block_extent[3], max_extent);
        }

        // What's the largest the loop variable could be?
        Expr max_loop = bounds_of_expr_in_scope(loop->min + loop->extent - 1, scope).max;
        Expr min_loop = bounds_of_expr_in_scope(loop->min, scope).min;

        scope.push(loop->name, Interval(min_loop, max_loop));

        // Recurse into the loop body
        loop->body.accept(this);

        scope.pop(loop->name);

        inside_thread = old_inside_thread;
    }

    void visit(const LetStmt *let) {
        Interval bounds = bounds_of_expr_in_scope(let->value, scope);
        scope.push(let->name, bounds);
        let->body.accept(this);
        scope.pop(let->name);
    }

    void visit(const Allocate *allocate) {
        map<string, Expr> &table = inside_thread ? local_allocations : shared_allocations;

        // We should only encounter each allocate once
        assert(table.find(allocate->name) == table.end());

        // What's the largest this allocation could be (in bytes)?
        Expr elements = bounds_of_expr_in_scope(allocate->size, scope).max;
        int bytes_per_element = allocate->type.bits/8;
        table[allocate->name] = simplify(elements * bytes_per_element);

        allocate->body.accept(this);

    }
};

// Is a buffer ever used on the host? Used on the device? Determines
// whether we need to allocate memory for each. (TODO: consider
// debug_to_file on host of a buffer only used on device)
class WhereIsBufferUsed : public IRVisitor {
public:
    string buf;
    bool used_on_host, used_on_device,
        written_on_host, read_on_host,
        written_on_device, read_on_device;
    WhereIsBufferUsed(string b) : buf(b),
                                  used_on_host(false),
                                  used_on_device(false),
                                  written_on_host(false),
                                  read_on_host(false),
                                  written_on_device(false),
                                  read_on_device(false),
                                  in_device_code(false) {}

private:
    bool in_device_code;

    using IRVisitor::visit;

    void visit(const For *op) {
        if (CodeGen_GPU_Dev::is_gpu_var(op->name)) {
            op->min.accept(this);
            op->extent.accept(this);
            bool old_in_device = in_device_code;
            in_device_code = true;
            op->body.accept(this);
            in_device_code = old_in_device;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const Load *op) {
        if (op->name == buf) {
            if (in_device_code) {
                used_on_device = true;
                read_on_device = true;
            } else {
                used_on_host = true;
                read_on_host = true;
            }
        }
        IRVisitor::visit(op);
    }

    void visit(const Store *op) {
        if (op->name == buf) {
            if (in_device_code) {
                used_on_device = true;
                written_on_device = true;
            } else {
                used_on_host = true;
                written_on_host = true;
            }
        }
        IRVisitor::visit(op);
    }
};


// Generate class definitions for each host target
#define GPU_HOST_TARGET X86
#include "CodeGen_GPU_Host_Template.cpp"
#undef GPU_HOST_TARGET

#define GPU_HOST_TARGET ARM
#include "CodeGen_GPU_Host_Template.cpp"
#undef GPU_HOST_TARGET


}}
