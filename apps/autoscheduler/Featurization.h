#ifndef FEATURIZATION_H
#define FEATURIZATION_H

#include <cstring>
#include <iostream>
#include <stdint.h>

#include "ASLog.h"

namespace Halide {
namespace Internal {

// The algorithm-specific features. For legacy reasons these are
// called PipelineFeatures in the code.
struct PipelineFeatures {
    static constexpr size_t num_features() {
        return sizeof(PipelineFeatures) / sizeof(int);
    }

    static constexpr uint32_t version() {
        return 3;
    }

    // Access them by index.
    int &operator[](int idx) {
        return ((int *)(this))[idx];
    }

    int operator[](int idx) const {
        return ((const int *)(this))[idx];
    }

    enum class OpType {
        Const,
        Cast,
        Variable,
        Param,
        Add,
        Sub,
        Mod,
        Mul,
        Div,
        Min,
        Max,
        EQ,
        NE,
        LT,
        LE,
        And,
        Or,
        Not,
        Select,
        ImageCall,   // Loads to an input buffer
        FuncCall,    // Calls to another pipeline stage
        SelfCall,    // Recursive calls from a Func to itself
        ExternCall,  // Math intrinsics, typically
        Let,
        NumOpTypes
    };

    enum class ScalarType {
        Bool,
        UInt8,   // or Int8
        UInt16,  // or Int16
        UInt32,  // or Int32
        UInt64,  // or Int64
        Float,
        Double,
        NumScalarTypes
    };

    // Not fed into the network, but helps avoid printing huge numbers of zeros while debugging things
    int types_in_use[(int)ScalarType::NumScalarTypes] = {};

    int op_histogram[(int)OpType::NumOpTypes][(int)ScalarType::NumScalarTypes] = {};

    enum class AccessType {
        LoadFunc,
        LoadSelf,
        LoadImage,
        Store,
        NumAccessTypes
    };

    // Finer granularity call/store node properties. These are a
    // function of the matrix of derivatives of each arg to a
    // call w.r.t the loop variables of the Stage. Each row of
    // the matrix corresponds to one of the call arguments. In
    // each case we illustrate such a call, assuming that the
    // variables of this Func are x, y, z, and that the
    // dimension vectorized over is the first (x).

    // Square identity matrix. f(x - 2, y + 8, z + param)
    int pointwise_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes] = {};
    // Square permutation matrix. f(y + 1, z - 3, x)
    int transpose_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes] = {};
    // Each row sums to 1. Each column sums to 1 or 0. f(y, x)
    int broadcast_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes] = {};
    // Each row sums to 1 or 0. Each column sums to 1. f(z, y, x, 4)
    int slice_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes] = {};

    template<typename OS>
    void dump(OS &os) const {
        for (int i = 0; i < (int)ScalarType::NumScalarTypes; i++) {
            const char *type_names[] = {"Bool", "UInt8", "UInt16", "UInt32", "UInt64", "Float", "Double"};
            // Skip printing for types not used
            if (!types_in_use[i]) continue;

            os << "    Featurization for type " << type_names[i] << "\n"
               << "     Op histogram:\n"
               << "      Constant:   " << op_histogram[(int)OpType::Const][i] << "\n"
               << "      Cast:       " << op_histogram[(int)OpType::Cast][i] << "\n"
               << "      Variable:   " << op_histogram[(int)OpType::Variable][i] << "\n"
               << "      Param:      " << op_histogram[(int)OpType::Param][i] << "\n"
               << "      Add:        " << op_histogram[(int)OpType::Add][i] << "\n"
               << "      Sub:        " << op_histogram[(int)OpType::Sub][i] << "\n"
               << "      Mod:        " << op_histogram[(int)OpType::Mod][i] << "\n"
               << "      Mul:        " << op_histogram[(int)OpType::Mul][i] << "\n"
               << "      Div:        " << op_histogram[(int)OpType::Div][i] << "\n"
               << "      Min:        " << op_histogram[(int)OpType::Min][i] << "\n"
               << "      Max:        " << op_histogram[(int)OpType::Max][i] << "\n"
               << "      EQ:         " << op_histogram[(int)OpType::EQ][i] << "\n"
               << "      NE:         " << op_histogram[(int)OpType::NE][i] << "\n"
               << "      LT:         " << op_histogram[(int)OpType::LT][i] << "\n"
               << "      LE:         " << op_histogram[(int)OpType::LE][i] << "\n"
               << "      And:        " << op_histogram[(int)OpType::And][i] << "\n"
               << "      Or:         " << op_histogram[(int)OpType::Or][i] << "\n"
               << "      Not:        " << op_histogram[(int)OpType::Not][i] << "\n"
               << "      Select:     " << op_histogram[(int)OpType::Select][i] << "\n"
               << "      ImageCall:  " << op_histogram[(int)OpType::ImageCall][i] << "\n"
               << "      FuncCall:   " << op_histogram[(int)OpType::FuncCall][i] << "\n"
               << "      SelfCall:   " << op_histogram[(int)OpType::SelfCall][i] << "\n"
               << "      ExternCall: " << op_histogram[(int)OpType::ExternCall][i] << "\n"
               << "      Let:        " << op_histogram[(int)OpType::Let][i] << "\n"
               << "     Memory access patterns. Columns are calls to other Funcs, self-calls, input image access, and stores\n"
               << "      Pointwise:      "
               << pointwise_accesses[0][i] << " "
               << pointwise_accesses[1][i] << " "
               << pointwise_accesses[2][i] << " "
               << pointwise_accesses[3][i] << "\n"
               << "      Transpose:      "
               << transpose_accesses[0][i] << " "
               << transpose_accesses[1][i] << " "
               << transpose_accesses[2][i] << " "
               << transpose_accesses[3][i] << "\n"
               << "      Broadcast:      "
               << broadcast_accesses[0][i] << " "
               << broadcast_accesses[1][i] << " "
               << broadcast_accesses[2][i] << " "
               << broadcast_accesses[3][i] << "\n"
               << "      Slice:          "
               << slice_accesses[0][i] << " "
               << slice_accesses[1][i] << " "
               << slice_accesses[2][i] << " "
               << slice_accesses[3][i] << "\n";
        }
    }
    void dump() const {
        auto os = aslog(0);
        dump(os);
    }
};

// The schedule-dependent portion of the featurization of a stage
struct ScheduleFeatures {
    static constexpr size_t num_features() {
        return sizeof(ScheduleFeatures) / sizeof(double);
    }

    static constexpr uint32_t version() {
        return 3;
    }

    double &operator[](int idx) {
        return ((double *)(this))[idx];
    }

    double operator[](int idx) const {
        return ((const double *)(this))[idx];
    }

    // The number of times storage for this stage is allocated. The
    // product of outer loops at store_at site
    double num_realizations = 0;

    // The number of times a tile of the stage is computed. The
    // pProduct of outer loops at compute_at site. Always at least as
    // large as num_realizations.
    double num_productions = 0;

    // Number of times the innermost loop happens per allocation.
    double points_computed_per_realization = 0;

    // Number of times the innermost stmt happens per tile computed.
    double points_computed_per_production = 0;

    // The total trip count of the innermost loop over the entire program.
    //  == num_realizations * points_computed_per_realization
    //  ~= num_productions * points_computed_per_production
    // Only approximately equal because of the simplifications made
    // regarding the modeling of sliding window
    double points_computed_total = 0;

    // The minimum number of points that are actually required to be
    // computed to produce a correct output. Not actually a function
    // of the schedule, but a useful reference point to see if a
    // schedule has gone off the rails.
    double points_computed_minimum = 0;

    // Trip count of innermost loop nest.
    double innermost_loop_extent = 0;

    // Trip count of just the pure loops in the innermost loop
    // (i.e. excludes loops representing reductions).
    double innermost_pure_loop_extent = 0;

    // If this is to be unrolled, what is the product of the unrolling
    // factors.
    double unrolled_loop_extent = 0;

    // The number of parallel jobs launched in the production of this
    // stage. Always 1 unless the Func is compute_root, because we
    // place all parallelism at the outermost level.
    double inner_parallelism = 0;

    // The number of times this Func could be realized in parallel. 1
    // when the Func is compute_root. Product of the containing
    // parallel loops for other stages.
    double outer_parallelism = 0;

    // Size of the region computed at the store_at site, measured in
    // bytes. Does not take storage-folding optimizations into account.
    double bytes_at_realization = 0;

    // Size of the region computed per tile (at the compute_at site),
    // measured in bytes. This includes the effect of storage-folding,
    // so it's a better number to look at to estimate memory usage.
    double bytes_at_production = 0;

    // If the stage were hypothetically scheduled at root, how much
    // memory would it consumed. Doesn't vary w.r.t. the schedule, but
    // a useful reference.
    double bytes_at_root = 0;

    // Same as the above, but only measuring the extent along the
    // innermost dimension, so that we can reason about spatial
    // locality, cache lines, prefetchers, etc.
    double innermost_bytes_at_realization = 0;
    double innermost_bytes_at_production = 0;
    double innermost_bytes_at_root = 0;

    // For inlined Funcs, how many calls are made to this Func total.
    double inlined_calls = 0;

    // Number of unique bytes and unique continguous segments of
    // memory loaded from all inputs over a single trip of the loop
    // containing the allocation site.
    double unique_bytes_read_per_realization = 0;
    double unique_lines_read_per_realization = 0;

    // The sum of the sizes of the allocations accessed at this
    // site. Gives a hint as to the likely locality of it.
    double allocation_bytes_read_per_realization = 0;

    // The sum of the sizes of the temporary allocations while
    // computing one tile of this Func. Probably a good thing if it
    // fits in cache.
    double working_set = 0;

    // The vectorization factor (#simd lanes) to be used to compute
    // this stage. Wasted work if it's smaller than the stage's native
    // vector size.
    double vector_size = 0;

    // The native vector size for the narrowest type used. Does not
    // vary with the schedule, but a useful reference point.
    double native_vector_size = 0;

    // Number of SIMD vectors computed
    double num_vectors = 0;

    // Number of scalars computed (e.g. from tails of loops)
    double num_scalars = 0;

    // The number of loads done per vector or scalar computed. Vector
    // gathers count as a batch of scalar loads. These get amortized
    // across unrolled blocks if some loads can be reused across the
    // unrolled dimension.
    double scalar_loads_per_vector = 0;
    double vector_loads_per_vector = 0;
    double scalar_loads_per_scalar = 0;

    // The memory footprint written over one per parallel task. The
    // union of the regions if the stage is computed at finer
    // granularity that one parallel task of some consumer.
    double bytes_at_task = 0;
    double innermost_bytes_at_task = 0;

    // The memory footprint accessed while computing a single vector.
    double unique_bytes_read_per_vector = 0;
    double unique_lines_read_per_vector = 0;

    // The memory footprint accessed per parallel task. Only counts
    // loads from things computed outside of that parallel task (to
    // measure the amount of traffic coming from another core).
    double unique_bytes_read_per_task = 0;
    double unique_lines_read_per_task = 0;

    // The sum of the sizes of all live allocations at various sites.
    double working_set_at_task = 0;
    double working_set_at_production = 0;
    double working_set_at_realization = 0;
    double working_set_at_root = 0;

    template<typename OS>
    void dump(OS &os) const {
        os << "    num_realizations:                      " << num_realizations << "\n"
           << "    num_productions:                       " << num_productions << "\n"
           << "    points_computed_per_realization:       " << points_computed_per_realization << "\n"
           << "    points_computed_per_production:        " << points_computed_per_production << "\n"
           << "    points_computed_total:                 " << points_computed_total << "\n"
           << "    points_computed_minimum:               " << points_computed_minimum << "\n"
           << "    innermost_loop_extent:                 " << innermost_loop_extent << "\n"
           << "    innermost_pure_loop_extent:            " << innermost_pure_loop_extent << "\n"
           << "    unrolled_loop_extent:                  " << unrolled_loop_extent << "\n"
           << "    inner_parallelism:                     " << inner_parallelism << "\n"
           << "    outer_parallelism:                     " << outer_parallelism << "\n"
           << "    bytes_at_realization:                  " << bytes_at_realization << "\n"
           << "    bytes_at_production:                   " << bytes_at_production << "\n"
           << "    bytes_at_root:                         " << bytes_at_root << "\n"
           << "    innermost_bytes_at_realization:        " << innermost_bytes_at_realization << "\n"
           << "    innermost_bytes_at_production:         " << innermost_bytes_at_production << "\n"
           << "    innermost_bytes_at_root:               " << innermost_bytes_at_root << "\n"
           << "    inlined_calls:                         " << inlined_calls << "\n"
           << "    unique_bytes_read_per_realization:     " << unique_bytes_read_per_realization << "\n"
           << "    unique_lines_read_per_realization:     " << unique_lines_read_per_realization << "\n"
           << "    allocation_bytes_read_per_realization: " << allocation_bytes_read_per_realization << "\n"
           << "    working_set:                           " << working_set << "\n"
           << "    vector_size:                           " << vector_size << "\n"
           << "    native_vector_size:                    " << native_vector_size << "\n"
           << "    num_vectors:                           " << num_vectors << "\n"
           << "    num_scalars:                           " << num_scalars << "\n"
           << "    scalar_loads_per_vector:               " << scalar_loads_per_vector << "\n"
           << "    vector_loads_per_vector:               " << vector_loads_per_vector << "\n"
           << "    scalar_loads_per_scalar:               " << scalar_loads_per_scalar << "\n"
           << "    bytes_at_task:                         " << bytes_at_task << "\n"
           << "    innermost_bytes_at_task:               " << innermost_bytes_at_task << "\n"
           << "    unique_bytes_read_per_vector:          " << unique_bytes_read_per_vector << "\n"
           << "    unique_lines_read_per_vector:          " << unique_lines_read_per_vector << "\n"
           << "    unique_bytes_read_per_task:            " << unique_bytes_read_per_task << "\n"
           << "    unique_lines_read_per_task:            " << unique_lines_read_per_task << "\n"
           << "    working_set_at_task:                   " << working_set_at_task << "\n"
           << "    working_set_at_production:             " << working_set_at_production << "\n"
           << "    working_set_at_realization:            " << working_set_at_realization << "\n"
           << "    working_set_at_root:                   " << working_set_at_root << "\n";
    }
    void dump() const {
        auto os = aslog(0);
        dump(os);
    }
};

}  // namespace Internal
}  // namespace Halide

#endif
