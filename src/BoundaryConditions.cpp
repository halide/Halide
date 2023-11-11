#include "BoundaryConditions.h"

namespace Halide {

namespace BoundaryConditions {

Func repeat_edge(const Func &source,
                 const Region &bounds) {
    std::vector<Var> args(source.args());
    user_assert(args.size() >= bounds.size())
        << "repeat_edge called with more bounds (" << bounds.size()
        << ") than dimensions (" << args.size()
        << ") Func " << source.name() << " has.\n";

    std::vector<Expr> actuals;
    for (size_t i = 0; i < bounds.size(); i++) {
        Var arg_var = args[i];
        Expr min = bounds[i].min;
        Expr extent = bounds[i].extent;

        if (min.defined() && extent.defined()) {
            actuals.push_back(clamp(likely(arg_var), min, min + extent - 1));
        } else if (!min.defined() && !extent.defined()) {
            actuals.push_back(arg_var);
        } else {
            user_error << "Partially undefined bounds for dimension " << arg_var
                       << " of Func " << source.name() << "\n";
        }
    }

    // If there were fewer bounds than dimensions, regard the ones at the end as unbounded.
    actuals.insert(actuals.end(), args.begin() + actuals.size(), args.end());

    Func bounded("repeat_edge");
    bounded(args) = source(actuals);

    return bounded;
}

Func constant_exterior(const Func &source, const Tuple &value,
                       const Region &bounds) {
    std::vector<Var> source_args = source.args();
    std::vector<Var> args(source_args);
    user_assert(args.size() >= bounds.size())
        << "constant_exterior called with more bounds (" << bounds.size()
        << ") than dimensions (" << source_args.size()
        << ") Func " << source.name() << " has.\n";

    Expr out_of_bounds = cast<bool>(false);
    for (size_t i = 0; i < bounds.size(); i++) {
        Var arg_var = source_args[i];
        Expr min = bounds[i].min;
        Expr extent = bounds[i].extent;

        if (min.defined() && extent.defined()) {
            out_of_bounds = (out_of_bounds ||
                             arg_var < min ||
                             arg_var >= min + extent);
        } else if (min.defined() || extent.defined()) {
            user_error << "Partially undefined bounds for dimension " << arg_var
                       << " of Func " << source.name() << "\n";
        }
    }

    Func bounded("constant_exterior");
    if (value.as_vector().size() > 1) {
        std::vector<Expr> def;
        for (size_t i = 0; i < value.as_vector().size(); i++) {
            def.push_back(select(out_of_bounds, value[i], likely(repeat_edge(source, bounds)(args)[i])));
        }
        bounded(args) = Tuple(def);
    } else {
        bounded(args) = select(out_of_bounds, value[0], likely(repeat_edge(source, bounds)(args)));
    }

    return bounded;
}

Func constant_exterior(const Func &source, const Expr &value,
                       const Region &bounds) {
    return constant_exterior(source, Tuple(value), bounds);
}

Func repeat_image(const Func &source,
                  const Region &bounds) {
    std::vector<Var> args(source.args());
    user_assert(args.size() >= bounds.size())
        << "repeat_image called with more bounds (" << bounds.size()
        << ") than dimensions (" << args.size()
        << ") Func " << source.name() << " has.\n";

    std::vector<Expr> actuals;
    for (size_t i = 0; i < bounds.size(); i++) {
        Var arg_var = args[i];
        Expr min = bounds[i].min;
        Expr extent = bounds[i].extent;

        if (min.defined() && extent.defined()) {
            Expr coord = arg_var - min;  // Enforce zero origin.
            coord = coord % extent;      // Range is 0 to w-1
            coord = coord + min;         // Restore correct min
            coord = select(arg_var < min || arg_var >= min + extent, coord,
                           likely(clamp(likely(arg_var), min, min + extent - 1)));

            // In the line above, we want loop partitioning to both cause the
            // clamp to go away, and also cause the select to go away. For loop
            // partitioning to make one of these constructs go away we need one
            // of two things to be true:
            //
            // 1) One arg has a likely intrinsic buried somewhere within it, and
            //    the other arg doesn't.
            // 2) Both args have likely intrinsics, but in one of the args it is
            //    not within any inner min/max/select node. This is called an
            //    'uncaptured' likely.
            //
            // The issue with this boundary condition is that the true branch of
            // the select (coord) may well have a likely within it somewhere
            // introduced by a loop tail strategy, so condition 1 doesn't
            // hold. To be more robust, we make condition 2 hold, by introducing
            // an uncaptured likely to the false branch.
            actuals.push_back(coord);
        } else if (!min.defined() && !extent.defined()) {
            actuals.push_back(arg_var);
        } else {
            user_error << "Partially undefined bounds for dimension " << arg_var
                       << " of Func " << source.name() << "\n";
        }
    }

    // If there were fewer bounds than dimensions, regard the ones at the end as unbounded.
    actuals.insert(actuals.end(), args.begin() + actuals.size(), args.end());

    Func bounded("repeat_image");
    bounded(args) = source(actuals);

    return bounded;
}

Func mirror_image(const Func &source,
                  const Region &bounds) {
    std::vector<Var> args(source.args());
    user_assert(args.size() >= bounds.size())
        << "mirror_image called with more bounds (" << bounds.size()
        << ") than dimensions (" << args.size() << ") Func "
        << source.name() << " has.\n";

    std::vector<Expr> actuals;
    for (size_t i = 0; i < bounds.size(); i++) {
        Var arg_var = args[i];

        Expr min = bounds[i].min;
        Expr extent = bounds[i].extent;

        if (min.defined() && extent.defined()) {
            Expr coord = arg_var - min;                                      // Enforce zero origin.
            coord = coord % (2 * extent);                                    // Range is 0 to 2w-1
            coord = select(coord >= extent, 2 * extent - 1 - coord, coord);  // Range is -w+1, w
            coord = coord + min;                                             // Restore correct min
            coord = clamp(coord, min, min + extent - 1);
            coord = select(arg_var < min || arg_var >= min + extent, coord,
                           likely(clamp(likely(arg_var), min, min + extent - 1)));
            actuals.push_back(coord);
        } else if (!min.defined() && !extent.defined()) {
            actuals.push_back(arg_var);
        } else {
            user_error << "Partially undefined bounds for dimension " << arg_var
                       << " of Func " << source.name() << "\n";
        }
    }

    // If there were fewer bounds than dimensions, regard the ones at the end as unbounded.
    actuals.insert(actuals.end(), args.begin() + actuals.size(), args.end());

    Func bounded("mirror_image");
    bounded(args) = source(actuals);

    return bounded;
}

Func mirror_interior(const Func &source,
                     const Region &bounds) {
    std::vector<Var> args(source.args());
    user_assert(args.size() >= bounds.size())
        << "mirror_interior called with more bounds (" << bounds.size()
        << ") than dimensions (" << args.size()
        << ") Func " << source.name() << " has.\n";

    std::vector<Expr> actuals;
    for (size_t i = 0; i < bounds.size(); i++) {
        Var arg_var = args[i];

        Expr min = bounds[i].min;
        Expr extent = bounds[i].extent;

        if (min.defined() && extent.defined()) {
            Expr limit = extent - 1;
            Expr coord = arg_var - min;   // Enforce zero origin.
            coord = coord % (2 * limit);  // Range is 0 to 2w-1
            coord = coord - limit;        // Range is -w, w
            coord = abs(coord);           // Range is 0, w
            coord = limit - coord;        // Range is 0, w
            coord = coord + min;          // Restore correct min

            // The boundary condition probably doesn't apply
            coord = select(arg_var < min || arg_var >= min + extent, coord,
                           likely(clamp(likely(arg_var), min, min + extent - 1)));

            actuals.push_back(coord);
        } else if (!min.defined() && !extent.defined()) {
            actuals.push_back(arg_var);
        } else {
            user_error << "Partially undefined bounds for dimension " << arg_var
                       << " of Func " << source.name() << "\n";
        }
    }

    // If there were fewer bounds than dimensions, regard the ones at the end as unbounded.
    actuals.insert(actuals.end(), args.begin() + actuals.size(), args.end());

    Func bounded("mirror_interior");
    bounded(args) = source(actuals);

    return bounded;
}

}  // namespace BoundaryConditions

}  // namespace Halide
