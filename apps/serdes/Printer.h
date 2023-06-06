#ifndef HALIDE_PRINTER_H
#define HALIDE_PRINTER_H

#include <string>
#include <vector>

#include "Halide.h"

class Printer {
public:
    Printer() = default;

    void print_pipeline(const Halide::Pipeline &pipeline);

    void print_function(const Halide::Internal::Function &function);

    void print_type(const Halide::Type &type);

    void print_stmt(const Halide::Internal::Stmt &stmt);

    void print_expr(const Halide::Expr &expr);

    void print_range(const Halide::Range &range);

    void print_bound(const Halide::Internal::Bound &bound);

    void print_storage_dim(const Halide::Internal::StorageDim &storage_dim);

    void print_loop_level(const Halide::LoopLevel &loop_level);

    void print_func_schedule(const Halide::Internal::FuncSchedule &func_schedule);
};

#endif
