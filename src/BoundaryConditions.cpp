#include "BoundaryConditions.h"

namespace Halide {

namespace BoundaryConditions {

Func repeat_edge(const Func &source,
                 const std::vector<std::pair<Expr, Expr>> &bounds) {
    std::vector<Var> args(source.args());
    user_assert(args.size() >= bounds.size()) <<
        "repeat_edge called with more bounds (" << bounds.size() <<
        ") than dimensions (" << args.size() << ") Func " <<
        source.name() << "has.\n";

    std::vector<Expr> actuals;
    for (size_t i = 0; i < bounds.size(); i++) {
        Var arg_var = args[i];
        Expr min = bounds[i].first;
        Expr extent = bounds[i].second;

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

Func constant_exterior(const Func &source, Tuple value,
                       const std::vector<std::pair<Expr, Expr>> &bounds) {
    std::vector<Var> source_args = source.args();
    std::vector<Var> args(source_args);
    user_assert(args.size() >= bounds.size()) <<
        "constant_exterior called with more bounds (" << bounds.size() <<
        ") than dimensions (" << source_args.size() << ") Func " <<
        source.name() << "has.\n";

    Expr out_of_bounds = cast<bool>(false);
    for (size_t i = 0; i < bounds.size(); i++) {
        Var arg_var = source_args[i];
        Expr min = bounds[i].first;
        Expr extent = bounds[i].second;

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
            def.push_back(select(out_of_bounds, value[i], repeat_edge(source, bounds)(args)[i]));
        }
        bounded(args) = Tuple(def);
    } else {
        bounded(args) = select(out_of_bounds, value[0], repeat_edge(source, bounds)(args));
    }

    return bounded;
}

Func constant_exterior(const Func &source, Expr value,
                       const std::vector<std::pair<Expr, Expr>> &bounds) {
    return constant_exterior(source, Tuple(value), bounds);
}


Func repeat_image(const Func &source,
                  const std::vector<std::pair<Expr, Expr>> &bounds) {
    std::vector<Var> args(source.args());
    user_assert(args.size() >= bounds.size()) <<
        "repeat_image called with more bounds (" << bounds.size() <<
        ") than dimensions (" << args.size() << ") Func " <<
        source.name() << "has.\n";

    std::vector<Expr> actuals;
    for (size_t i = 0; i < bounds.size(); i++) {
        Var arg_var = args[i];
        Expr min = bounds[i].first;
        Expr extent = bounds[i].second;

        if (min.defined() && extent.defined()) {
            Expr coord = arg_var - min;  // Enforce zero origin.
            coord = coord % extent;      // Range is 0 to w-1
            coord = coord + min;         // Restore correct min


            coord = select(arg_var < min || arg_var >= min + extent, coord,
                           clamp(likely(arg_var), min, min + extent - 1));

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
                  const std::vector<std::pair<Expr, Expr>> &bounds) {
    std::vector<Var> args(source.args());
    user_assert(args.size() >= bounds.size()) <<
        "mirror_image called with more bounds (" << bounds.size() <<
        ") than dimensions (" << args.size() << ") Func " <<
        source.name() << "has.\n";

    std::vector<Expr> actuals;
    for (size_t i = 0; i < bounds.size(); i++) {
        Var arg_var = args[i];

        Expr min = bounds[i].first;
        Expr extent = bounds[i].second;

        if (min.defined() && extent.defined()) {
            Expr coord = arg_var - min;    // Enforce zero origin.
            coord = coord % (2 * extent);  // Range is 0 to 2w-1
            coord = select(coord >= extent, 2 * extent - 1 - coord, coord);  // Range is -w+1, w
            coord = coord + min; // Restore correct min
            coord = clamp(coord, min, min + extent - 1);
            coord = select(arg_var < min || arg_var >= min + extent, coord,
                           clamp(likely(arg_var), min, min + extent-1));
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
                     const std::vector<std::pair<Expr, Expr>> &bounds) {
    std::vector<Var> args(source.args());
    user_assert(args.size() >= bounds.size()) <<
        "mirror_interior called with more bounds (" << bounds.size() <<
        ") than dimensions (" << args.size() << ") Func " <<
        source.name() << "has.\n";

    std::vector<Expr> actuals;
    for (size_t i = 0; i < bounds.size(); i++) {
        Var arg_var = args[i];

        Expr min = bounds[i].first;
        Expr extent = bounds[i].second;

        if (min.defined() && extent.defined()) {
            Expr limit = extent - 1;
            Expr coord = arg_var - min;  // Enforce zero origin.
            coord = coord % (2 * limit); // Range is 0 to 2w-1
            coord = coord - limit;       // Range is -w, w
            coord = abs(coord);          // Range is 0, w
            coord = limit - coord;       // Range is 0, w
            coord = coord + min;         // Restore correct min

            // The boundary condition probably doesn't apply
            coord = select(arg_var < min || arg_var >= min + extent, coord,
                           clamp(likely(arg_var), min, min + extent - 1));

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
