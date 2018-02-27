#ifndef HALIDE_INTERNAL_AUTO_SCHEDULE_H
#define HALIDE_INTERNAL_AUTO_SCHEDULE_H

/** \file
 *
 * Defines the method that does automatic scheduling of Funcs within a pipeline.
 */

#include "Function.h"
#include "Target.h"

namespace Halide {

/** A struct representing the machine parameters to generate the auto-scheduled
 * code for. */
struct MachineParams {
    /** Maximum level of parallelism avalaible. */
    Expr parallelism;
    /** Size of the last-level cache (in KB). */
    Expr last_level_cache_size;
    /** Indicates how much more expensive is the cost of a load compared to
     * the cost of an arithmetic operation at last level cache. */
    Expr balance;
    int32_t max_inline_fusion;
    int32_t max_fast_mem_fusion;
    // If max_total_fusion is set (not -1), it will override both max_inline_fusion
    // and  max_fast_mem_fusion.
    int32_t max_total_fusion;

    explicit MachineParams(int32_t parallelism, int32_t llc, int32_t balance,
                           int32_t max_inline_fusion, int32_t max_fast_mem_fusion,
                           int32_t max_total_fusion)
        : parallelism(parallelism), last_level_cache_size(llc), balance(balance)
        , max_inline_fusion(max_inline_fusion), max_fast_mem_fusion(max_fast_mem_fusion)
        , max_total_fusion (max_total_fusion) {
            user_assert((max_inline_fusion == -1 || max_inline_fusion >= 0));
            user_assert((max_fast_mem_fusion == -1 || max_fast_mem_fusion >= 0));
            user_assert((max_total_fusion == -1 || max_total_fusion >= 0));
        }

    /** Default machine parameters for generic CPU architecture. */
    static MachineParams generic();

    /** Convert the MachineParams into canonical string form. */
    std::string to_string() const;

    /** Reconstruct a MachineParams from canonical string form. */
    explicit MachineParams(const std::string &s);
};

namespace Internal {

/** Generate schedules for Funcs within a pipeline. The Funcs should not already
 * have specializations or schedules as the current auto-scheduler does not take
 * into account user-defined schedules or specializations. This applies the
 * schedules and returns a string representation of the schedules. The target
 * architecture is specified by 'target'. */
std::string generate_schedules(const std::vector<Function> &outputs,
                               const Target &target,
                               const MachineParams &arch_params);

}
}

#endif
