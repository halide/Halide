#include "Halide.h"

namespace {

// Computes a shallow lower bound on a 3D input, see ShallowLowerBound()
// for details.
class ShallowLowerBoundGenerator : public Halide::Generator<ShallowLowerBoundGenerator> {
public:
    GeneratorParam<bool> auto_schedule{"auto_schedule", false};
    ImageParam input_{Float(32), 3, "input_im"};
    ImageParam valid_u8_{UInt(8), 3, "valid_u8"};
    Param<int> radius_x_{"radius_x"};
    Param<int> radius_y_{"radius_y"};
    Param<int> radius_z_{"radius_z"};

    // Does one circular shift of the first three dimensions of a Func to the right.
    Func CircularShift3(const Func& input, const Target& target) {
        Var x("x"), y("y"), z("z");

        Func output("output_circular_shift");
        output(z, x, y, Halide::_) = input(x, y, z, Halide::_);

        return output;
    }

    // Apply a box filter with radius r in the Y dimension, where each y value of
    // the output in [0, extent-1] is computed. The boundary conditions of the input
    // are assumed to be handled elsewhere. The box filter averages, instead of
    // summing.
    inline Func BoxFilterY(bool auto_schedule, const Func& input, const Expr& radius,
                           const Expr& extent, const Target& target) {
        Var x("x"), y("y"), z("z");

        Func output("output_box_filter_y");
        output(x, y, z) = Halide::undef<float>();

        // Compute the sum of the first 2*radius+1 elements. This serves as the first
        // column of the output, and also initializes the sliding-window running sum
        // that will be used to compute the rest of the output.
        Expr width = 2 * radius + 1;
        RDom y_first(-radius, width);
        output(x, 0, z) = cast<float>(0);
        output(x, 0, z) += input(x, y_first, z);

        // Iterate over y = [1, extent-1], updating the running sum as we go.
        RDom y_rest(1, extent - 1);
        output(x, y_rest, z) =
            (output(x, y_rest - 1, z) + input(x, y_rest + radius, z) -
                input(x, y_rest - radius - 1, z));

        // Divide by the width of the filter.
        output(x, y, z) = output(x, y, z) / cast<float>(width);

        // Schedule.
        if (!auto_schedule) {
            const int vector_size = target.natural_vector_size<float>();
            output.compute_root().parallel(z).vectorize(x, vector_size);
            for (int stage = 0; stage < 4; stage++) {
                output.update(stage).parallel(z).vectorize(x, vector_size);
            }
        } else {
            const int vector_size = target.natural_vector_size<float>();
            Var x_vi("x_vi"), x_vo("x_vo");
            output.compute_root();
            for (int stage = 0; stage < 4; stage++) {
                output.update(stage)
                    .split(x, x_vo, x_vi, vector_size)
                    .vectorize(x_vi)
                    .parallel(z);
            }
            output.update(2).reorder(x_vi, y_rest, x_vo, z);
        }
        return output;
    }

    // Applies a box filter to the first three dimensions of an input Func. Boundary
    // conditions on the input are assumed to be handled elsewhere. The radii and
    // extents of the input need to be specified.
    inline Func BoxFilter(bool auto_schedule, const Func& f_in, const Expr& radius_x,
                          const Expr& radius_y, const Expr& radius_z,
                          const Expr& extent_x, const Expr& extent_y,
                          const Expr& extent_z, const Target& target) {
        Var x("x"), y("y"), z("z");

        // Because a box filter is separable, we can apply three box filters along
        // each dimension. To make implementation easier, we repeatedly apply a box
        // filter to the y dimension of the input, and then permute the dimensions
        // by shifting them over by 1 so that a new dimension occupies the "y"
        // dimension.

        // Filter in x.
        Func f_x_in = CircularShift3(f_in, target);
        Func f_x_out = BoxFilterY(auto_schedule, f_x_in, radius_x, extent_x, target);

        // Filter in z.
        Func f_z_in = CircularShift3(f_x_out, target);
        Func f_z_out = BoxFilterY(auto_schedule, f_z_in, radius_z, extent_z, target);

        // Filter in y.
        Func f_y_in = CircularShift3(f_z_out, target);
        Func f_y_out = BoxFilterY(auto_schedule, f_y_in, radius_y, extent_y, target);

        // Schedule.
        if (!auto_schedule) {
            f_x_out.compute_root();
            f_z_out.compute_root();
            f_y_out.compute_root();
        } else {
            //f_x_in.compute_at(f_x_out, z);
        }
        return f_y_out;
    }

    // Performs a min filter along the y axis of the given radius, where the output
    // is computed under the assumption that the output needs to have valid values
    // in the Y range of [-radius, extent + radius].
    // This function assumes boundary conditions on "input" are handled elsewhere.
    inline Func MinFilterY(bool auto_schedule, const Func& input, const Expr& radius,
                           const Expr& extent, const Target& target) {
        Var x("x"), y("y"), z("z"), level("level");

        // The last level of the min-pyramid that we need to compute to extract the
        // solution.
        Expr last_level = cast<int>(ceil(log(radius + 1) / logf(2)));

        // Recursively construct pyr(x, y, z, level), which is a forward-looking min
        // filter over the input image from (x, y, z) through (x, y + 2^level - 1, z).
        // This is cheap to compute recursively as a function of "level", because a
        // min filter of support 2^level can be computed using the min of two values
        // in a min-filtered image with support 2^(level-1).
        Func pyr("pyr_min_filter_y");
        pyr(x, y, z, level) = input(x, y, z);
        RDom rd(-2 * radius, extent + 2 * radius, 1, last_level);
        // The clamp() here is not required for correctness, but is necessary for
        // Halide bounds inference.
        pyr(x, rd.x, z, rd.y) = Halide::min(
            pyr(x, rd.x, z, rd.y - 1),
            pyr(x, rd.x + clamp((1 << (rd.y - 1)), 0, 2 * radius), z, rd.y - 1));

        // Here we compute the output minimum value by, for every output value
        // coordinate, taking the minimum of two points on the last level of the
        // min-pyramid. These two points are chosen to span the range which we need
        // to compute the min over, which we can call [lo, hi]. The "first" coordinate
        // contains the min() over [lo, lo + 2^(last_level)-1], and the "second"
        // coordinate contains the min() over [hi - 2^(last_level)-1, hi], which
        // (because 2^(last_level)-1 <= 2*radius+1 and because min() is associative)
        // means that the min() of those two values is the minimum over [lo, hi].
        // The indexing math here is shifted to account for the fact that the
        // min-pyramid is "forward-looking").
        Expr first = pyr(x, y - radius, z, last_level);
        Expr second =
            pyr(x, y + radius + 1 - clamp(1 << last_level, 0, 2 * radius), z,
                last_level);  // The clamp is required only for bounds inference.

        // The radius == 0 case needs to be handled manually.
        Func output("output_min_filter_y");
        output(x, y, z) = select(radius == 0, input(x, y, z), min(first, second));

        // Schedule.
        if (!auto_schedule) {
            const int vector_size = target.natural_vector_size<float>();
            Var zx, xi;
            output.compute_root().parallel(z).vectorize(x, vector_size);
            pyr.compute_at(output, z).vectorize(x, vector_size);
            pyr.update().reorder(x, rd.x, rd.y, z).vectorize(x, vector_size);
        } else {
            const int vector_size = target.natural_vector_size<float>();
            Var x_vi("x_vi"), x_vo("x_vo");
            output
                .compute_root()
                .split(x, x_vo, x_vi, vector_size)
                .vectorize(x_vi)
                .reorder(x_vi, y, x_vo, z)
                .parallel(z);
            pyr
                .compute_at(output, z)
                .vectorize(x, vector_size);
            pyr.update(0)
                .split(x, x_vo, x_vi, vector_size)
                .vectorize(x_vi)
                .reorder(x_vi, rd.x, x_vo, rd.y, z);
        }
        return output;
    }

    // Applies a min filter to the first three dimensions of an input Func. Boundary
    // conditions on the input are assumed to be handled elsewhere. The radii and
    // extents of the input need to be specified.
    inline Func MinFilter(bool auto_schedule, const Func& f_in, const Expr& radius_x,
                          const Expr& radius_y, const Expr& radius_z,
                          const Expr& extent_x, const Expr& extent_y,
                          const Expr& extent_z, const Target& target) {
        Var x("x"), y("y"), z("z");

        // Because a min filter is separable, we can apply three min filters along
        // each dimension. To make implementation easier, we repeatedly apply a min
        // filter to the y dimension of the input, and then permute the dimensions
        // by shifting them over by 1 so that a new dimension occupies the "y"
        // dimension.

        // Filter in x.
        Func f_x_in = CircularShift3(f_in, target);
        Func f_x_out = MinFilterY(auto_schedule, f_x_in, radius_x, extent_x, target);

        // Filter in z.
        Func f_z_in = CircularShift3(f_x_out, target);
        Func f_z_out = MinFilterY(auto_schedule, f_z_in, radius_z, extent_z, target);

        // Filter in y.
        Func f_y_in = CircularShift3(f_z_out, target);
        Func f_y_out = MinFilterY(auto_schedule, f_y_in, radius_y, extent_y, target);

        // Schedule.
        if (!auto_schedule) {
            f_x_out.compute_root();
            f_z_out.compute_root();
            f_y_out.compute_root();
        }
        return f_y_out;
    }

    Func ShallowLowerBound(bool auto_schedule,
                           const Func& input, const Func& valid,
                           const Expr& radius_x, const Expr& radius_y,
                           const Expr& radius_z, const Expr& extent_x,
                           const Expr& extent_y, const Expr& extent_z,
                           const Target& target) {
        // TODO(barron): once b/65212470 is fixed, replace all uses of kMaxVal with
        // kInfinity.
        float kMaxVal = std::numeric_limits<float>::max();
        float kInfinity = std::numeric_limits<float>::infinity();

        Var x("x"), y("y"), z("z");

        // Identify the smallest valid input value.
        RDom r(0, extent_x, 0, extent_y, 0, extent_z);
        Func global_min("global_min");
        global_min() = Halide::Float(32).max();
        global_min() =
            min(global_min(), select(valid(r.x, r.y, r.z), input(r.x, r.y, r.z), kMaxVal));

        // Determine if any inputs are valid, which tells us whether or not to trust
        // global_min.
        Func any_valid("any_valid");
        any_valid() = Halide::Internal::const_false();
        any_valid() = any_valid() || valid(r.x, r.y, r.z);

        // This clamping is just to prevent Halide from complaining, boundary
        // conditions are not clamped, and instead positive infinity is used outside
        // of the input.
        Expr xc = clamp(x, 0, extent_x - 1);
        Expr yc = clamp(y, 0, extent_y - 1);
        Expr zc = clamp(z, 0, extent_z - 1);

        // Set invalid values and values outside of the range of the input to be
        // positive infinity.
        Func input_masked("input_masked");
        input_masked(x, y, z) =
            select((x >= 0) && (x < extent_x) && (y >= 0) && (y < extent_y) &&
                    (z >= 0) && (z < extent_z) && valid(xc, yc, zc),
                    input(xc, yc, zc), kInfinity);

        // Apply a min filter.
        Func min_val = MinFilter(auto_schedule, input_masked, radius_x, radius_y, radius_z, extent_x,
                                 extent_y, extent_z, target);

        // Set invalid outputs of the min filter to be the smallest valid value in the
        // input. This prevents positive infinity from being in the input to the box
        // filter in case there are very large regions of invalid pixels (infinite
        // values break the sliding-window approach used by the box filter code).
        Func min_val_fixed("min_val_fixed");
        min_val_fixed(x, y, z) =
            select(min_val(x, y, z) >= kMaxVal, global_min(), min_val(x, y, z));

        // Apply a box filter.
        Func lower_bound = BoxFilter(auto_schedule, min_val_fixed, radius_x, radius_y, radius_z,
                                     extent_x, extent_y, extent_z, target);

        // If no input is valid, then lower_bound is guaranteed to be bad, and so
        // we set it to positive infinity.
        Func lower_bound_clamped("lower_bound_clamped");
        lower_bound_clamped(x, y, z) =
            select(any_valid(), lower_bound(x, y, z), kInfinity);

        // Schedule.
        if (!auto_schedule) {
            any_valid.compute_root();
            global_min.compute_root();
            lower_bound_clamped.compute_root();
            lower_bound.compute_root();
            min_val.compute_root();
            min_val_fixed.compute_root();
        } else {
            const int vector_size = target.natural_vector_size<float>();
            Var u("u");

            global_min.compute_root();
            global_min.update().rfactor(r.z, u)
                .compute_root()
                .update(0)
                .parallel(u);

            any_valid.compute_root();


            Func intm1 = any_valid.update().rfactor(r.z, u);
            intm1.compute_root();
            intm1.update(0).parallel(u);

            Var v("v");
            Halide::RVar rxo("rxo"), rxi("rxi");
            Func intm2 = intm1.update(0).split(r.x, rxo, rxi, 16).rfactor(rxi, v);
            intm2.compute_at(intm1, u);
            intm2.update(0).vectorize(v);

            /*any_valid.update().rfactor(r.z, u)
                .compute_root()
                .update(0)
                .parallel(u);*/

            input_masked
                .compute_root()
                .vectorize(x, vector_size)
                .parallel(z);
            min_val_fixed
                .compute_root()
                .vectorize(x, vector_size)
                .parallel(z);
            lower_bound_clamped
                .compute_root()
                .vectorize(x, vector_size)
                .parallel(z);
        }
        return lower_bound_clamped;
    }

    Func build() {
        Var x("x"), y("y"), z("z"), c("c");

        Expr extent_x = input_.dim(0).extent();
        Expr extent_y = input_.dim(1).extent();
        Expr extent_z = input_.dim(2).extent();

        Func valid_bool("valid_bool");
        valid_bool(x, y, z) = valid_u8_(x, y, z) > 0;

        Func output = ShallowLowerBound(auto_schedule, input_, valid_bool, radius_x_, radius_y_,
                                        radius_z_, extent_x, extent_y, extent_z,
                                        get_target());

        // Provide estimates on the input image
        input_.dim(0).set_bounds_estimate(0, 256);
        input_.dim(1).set_bounds_estimate(0, 256);
        input_.dim(2).set_bounds_estimate(0, 256);

        valid_u8_.dim(0).set_bounds_estimate(0, 256);
        valid_u8_.dim(1).set_bounds_estimate(0, 256);
        valid_u8_.dim(2).set_bounds_estimate(0, 256);

        // Provide estimates on the parameters
        const int r = 256;
        radius_x_.set_estimate(r);
        radius_y_.set_estimate(r);
        radius_z_.set_estimate(r);

        // Provide estimates on the pipeline output
        output.estimate(output.args()[0], 0, 256);
        output.estimate(output.args()[1], 0, 256);
        output.estimate(output.args()[2], 0, 256);

        if (auto_schedule) {
            Pipeline pipeline(output);

            const int kParallelism = 32;
            const int kLastLevelCacheSize = 16 * 1024 * 1024;
            const int kBalance = 100;
            Halide::MachineParams machine_params(kParallelism, kLastLevelCacheSize, kBalance);
            //pipeline.auto_schedule(target, machine_params);

            //std::cout << "\n\n*******************************\nSCHEDULE:\n*******************************\n" << pipeline.auto_schedule(target) << "\n\n";

            valid_bool
                .compute_root()
                .vectorize(x, 32)
                .parallel(z);
        }
        return output;
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(ShallowLowerBoundGenerator, shallow_lower_bound)
