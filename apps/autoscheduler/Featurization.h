#ifndef FEATURIZATION_H
#define FEATURIZATION_H

#include <stdint.h>
#include <iostream>

struct PipelineFeatures {
    // A featurization of the compute done by a Func, to
    // feed the neural network.

    enum class OpType {
        Const,
        Cast,
        Variable,
        Param,
        Add, Sub, Mod, Mul, Div, Min, Max,
        EQ, NE, LT, LE,
        And, Or, Not,
        Select,
        ImageCall,
        FuncCall,
        SelfCall,   // Recursive calls from a Func to itself
        ExternCall, // Math intrinsics, typically
        Let,        // Depends on what CSE has decided to do, but a good indication of register pressure
        NumOpTypes,
    };

    enum class ScalarType {
        Bool,
        UInt8,  // includes Int8
        UInt16, // includes Int16
        UInt32, // includes Int32 (TODO: is this a good idea? index math is a different sort of beast)
        UInt64, // Includes Int64
        Float,
        Double,
        NumScalarTypes
    };

    // Not a super-useful feature, but helps avoid printing huge numbers of zeros while debugging things
    int types_in_use[(int)ScalarType::NumScalarTypes];

    int op_histogram[(int)OpType::NumOpTypes][(int)ScalarType::NumScalarTypes];

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
    int pointwise_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes],  // Square identity matrix. f(x - 2, y + 8, z + param)
        transpose_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes],         // Square permutation matrix. f(y + 1, z - 3, x)
        broadcast_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes],         // Each row sums to 1. Each column sums to 1 or 0. f(y, x)
        slice_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes],             // Each row sums to 1 or 0. Each column sums to 1. f(z, y, x, 4)


    // The following four features are dead (always zero), now that we
    // actually search over which dimension to vectorize over:
        vectorizable_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes], // First (vectorized) col is 1, 0, 0, ... f(x+y, z*y, y/z)
        strided_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes],      // First col is [(int)2,3,4], 0, 0, ...        f(3*x + 1, z/8, y/z)
        scalar_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes],       // First col is all zero                  f(y, 2, z*8)
        gather_scatter_accesses[(int)AccessType::NumAccessTypes][(int)ScalarType::NumScalarTypes];            // Not one of the three categories above  f(x, x, sqrt(y))

    // TODO: We should possibly feed these Jacobians directly
    // to the net rather than computing the properties above.

    // TODO: strided captures downsamples. What about upsamples?

    void dump() const {
        for (int i = 0; i < (int)ScalarType::NumScalarTypes; i++) {
            const char *type_names[] = {"Bool", "UInt8", "UInt16", "UInt32", "UInt64", "Float", "Double"};
            // Skip printing for types not used
            if (!types_in_use[i]) continue;


            std::cerr << "    Featurization for type " << type_names[i] << '\n'
                     << "     Op histogram:\n"
                     << "      Constant:   " << op_histogram[(int)OpType::Const][i] << '\n'
                     << "      Cast:       " << op_histogram[(int)OpType::Cast][i] << '\n'
                     << "      Variable:   " << op_histogram[(int)OpType::Variable][i] << '\n'
                     << "      Param:      " << op_histogram[(int)OpType::Param][i] << '\n'
                     << "      Add:        " << op_histogram[(int)OpType::Add][i] << '\n'
                     << "      Sub:        " << op_histogram[(int)OpType::Sub][i] << '\n'
                     << "      Mod:        " << op_histogram[(int)OpType::Mod][i] << '\n'
                     << "      Mul:        " << op_histogram[(int)OpType::Mul][i] << '\n'
                     << "      Div:        " << op_histogram[(int)OpType::Div][i] << '\n'
                     << "      Min:        " << op_histogram[(int)OpType::Min][i] << '\n'
                     << "      Max:        " << op_histogram[(int)OpType::Max][i] << '\n'
                     << "      EQ:         " << op_histogram[(int)OpType::EQ][i] << '\n'
                     << "      NE:         " << op_histogram[(int)OpType::NE][i] << '\n'
                     << "      LT:         " << op_histogram[(int)OpType::LT][i] << '\n'
                     << "      LE:         " << op_histogram[(int)OpType::LE][i] << '\n'
                     << "      And:        " << op_histogram[(int)OpType::And][i] << '\n'
                     << "      Or:         " << op_histogram[(int)OpType::Or][i] << '\n'
                     << "      Not:        " << op_histogram[(int)OpType::Not][i] << '\n'
                     << "      Select:     " << op_histogram[(int)OpType::Select][i] << '\n'
                     << "      ImageCall:  " << op_histogram[(int)OpType::ImageCall][i] << '\n'
                     << "      FuncCall:   " << op_histogram[(int)OpType::FuncCall][i] << '\n'
                     << "      SelfCall:   " << op_histogram[(int)OpType::SelfCall][i] << '\n'
                     << "      ExternCall: " << op_histogram[(int)OpType::ExternCall][i] << '\n'
                     << "      Let:        " << op_histogram[(int)OpType::Let][i] << '\n'
                     << "     Memory access patterns. Columns are calls to other Funcs, self-calls, input image access, and stores\n"
                     << "      Pointwise:      " << pointwise_accesses[0][i] << ' ' << pointwise_accesses[1][i] << ' ' << pointwise_accesses[2][i] << ' ' << pointwise_accesses[3][i] << '\n'
                     << "      Transpose:      " << transpose_accesses[0][i] << ' ' << transpose_accesses[1][i] << ' ' << transpose_accesses[2][i] << ' ' << transpose_accesses[3][i] << '\n'
                     << "      Broadcast:      " << broadcast_accesses[0][i] << ' ' << broadcast_accesses[1][i] << ' ' << broadcast_accesses[2][i] << ' ' << broadcast_accesses[3][i] << '\n'
                     << "      Slice:          " << slice_accesses[0][i] << ' ' << slice_accesses[1][i] << ' ' << slice_accesses[2][i] << ' ' << slice_accesses[3][i] << '\n'
                     << "      Vectorizable:   " << vectorizable_accesses[0][i] << ' ' << vectorizable_accesses[1][i] << ' ' << vectorizable_accesses[2][i] << ' ' << vectorizable_accesses[3][i] << '\n'
                     << "      Strided:        " << strided_accesses[0][i] << ' ' << strided_accesses[1][i] << ' ' << strided_accesses[2][i] << ' ' << strided_accesses[3][i] << '\n'
                     << "      Scalar:         " << scalar_accesses[0][i] << ' ' << scalar_accesses[1][i] << ' ' << scalar_accesses[2][i] << ' ' << scalar_accesses[3][i] << '\n'
                     << "      Gather/Scatter: " << gather_scatter_accesses[0][i] << ' ' << gather_scatter_accesses[1][i] << ' ' << gather_scatter_accesses[2][i] << ' ' << gather_scatter_accesses[3][i] << '\n';
        }
    }

};


// The schedule-dependent portion of the featurization of a stage
struct ScheduleFeatures {
    int64_t num_realizations = 0; // Product of outer loops at store_at site
    int64_t num_productions = 0;  // Product of outer loops at compute_at site
    int64_t points_computed_per_realization = 0; // Number of times the innermost stmt happens per store_at
    int64_t points_computed_per_production = 0;  // Number of times the innermost stmt happens per compute_at
    int64_t points_computed_total = 0;
    // points_computed_total
    //  == num_realizations * points_computed_per_realization
    //  ~= num_productions * points_computed_per_production
    // Only approximately equal because of the simplifications made
    // regarding the modeling of sliding window

    int64_t points_computed_minimum = 0; // The minimum number of points that are actually required to be computed to produce a correct output.

    int64_t innermost_loop_extent = 0; // Trip count of innermost serial loop
    int64_t innermost_pure_loop_extent = 0; // Trip count of innermost loop over the innermost storage dimension

    int64_t inner_parallelism = 0; // The number of parallel jobs used in the production of this Func. 1 unless the Func is compute_root.
    int64_t outer_parallelism = 0; // The number of times this Func could be realized in parallel. 1 when the Func is compute_root.

    int64_t bytes_at_realization = 0; // Size of the region computed at the store_at site, measured in bytes
    int64_t bytes_at_production = 0; // Size of the region computed at the compute_at site, measured in bytes
    int64_t bytes_at_root = 0; // The same at root, regardless of where it's actually scheduled
    int64_t innermost_bytes_at_realization = 0;
    int64_t innermost_bytes_at_production = 0;
    int64_t innermost_bytes_at_root = 0;

    int64_t inlined_calls = 0; // For inlined Funcs, how many calls are made to this Func total

    // Logically these features should be grouped earlier, but the convnet currently doesn't know about them
    int64_t unique_bytes_read_per_realization = 0; // Number of unique bytes loaded from all inputs per production
    int64_t unique_lines_read_per_realization = 0; // Number of unique contiguous segments of memory loaded from all inputs per production
    int64_t allocation_bytes_read_per_realization = 0; // The sum of the sizes of the allocations accessed. Gives a hint as to the likely locality of it.

    int64_t working_set = 0; // The sum of the sizes of the allocations within the production of this Func. Probably a good thing if it fits in cache.

    int64_t vector_size = 0; // The vectorization factor (#simd lanes) to be used to compute this stage. Wasted work if it's smaller than the stage's native vector size (which is in the pipeline features).

    int64_t native_vector_size = 0; // The native vector size for the narrowest type used.

    int64_t num_vectors = 0; // Number of vectors computed (Assuming sliding worked)
    int64_t num_scalars = 0; // Number of scalars computed (e.g. from tails of loops)
    int64_t scalar_loads_per_vector = 0;
    int64_t vector_loads_per_vector = 0;
    int64_t scalar_loads_per_scalar = 0;

    int64_t bytes_at_task = 0;
    int64_t innermost_bytes_at_task = 0;

    void dump() const {
        std::cerr << "    num_realizations:                      " << num_realizations << '\n'
                 << "    num_productions:                       " << num_productions << '\n'
                 << "    points_computed_per_realization:       " << points_computed_per_realization << '\n'
                 << "    points_computed_per_production:        " << points_computed_per_production << '\n'
                 << "    points_computed_total:                 " << points_computed_total << '\n'
                 << "    points_computed_minimum:               " << points_computed_minimum << '\n'
                 << "    innermost_loop_extent:                 " << innermost_loop_extent << '\n'
                 << "    innermost_pure_loop_extent:            " << innermost_pure_loop_extent << '\n'
                 << "    inner_parallelism:                     " << inner_parallelism << '\n'
                 << "    outer_parallelism:                     " << outer_parallelism << '\n'
                 << "    bytes_at_realization:                  " << bytes_at_realization << '\n'
                 << "    bytes_at_production:                   " << bytes_at_production << '\n'
                 << "    bytes_at_root:                         " << bytes_at_root << '\n'
                 << "    innermost_bytes_at_realization:        " << innermost_bytes_at_realization << '\n'
                 << "    innermost_bytes_at_production:         " << innermost_bytes_at_production << '\n'
                 << "    innermost_bytes_at_root:               " << innermost_bytes_at_root << '\n'
                 << "    inlined_calls:                         " << inlined_calls << '\n'
                 << "    unique_bytes_read_per_realization:     " << unique_bytes_read_per_realization << '\n'
                 << "    unique_lines_read_per_realization:     " << unique_lines_read_per_realization << '\n'
                 << "    allocation_bytes_read_per_realization: " << allocation_bytes_read_per_realization << '\n'
                 << "    working_set:                           " << working_set << '\n'
                 << "    vector_size:                           " << vector_size << '\n'
                 << "    native_vector_size:                    " << native_vector_size << '\n'
                 << "    num_vectors:                           " << num_vectors << '\n'
                 << "    num_scalars:                           " << num_scalars << '\n'
                 << "    scalar_loads_per_vector:               " << scalar_loads_per_vector << '\n'
                 << "    vector_loads_per_vector:               " << vector_loads_per_vector << '\n'
                 << "    scalar_loads_per_scalar:               " << scalar_loads_per_scalar << '\n'
                 << "    bytes_at_task:                         " << bytes_at_task << '\n'
                 << "    innermost_bytes_at_task:               " << innermost_bytes_at_task << '\n';

    }
};

#endif
