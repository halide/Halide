#include "BoundaryConditions.h"

namespace Halide {

namespace BoundaryConditions {

Func repeat_edge(const Func &source,
                 const std::vector<std::pair<Expr, Expr> > &bounds) {
    std::vector<Var> args(source.args());
    user_assert(args.size() >= bounds.size()) <<
        "repeat_edge called with more bounds (" << bounds.size() <<
        ") than dimensions (" << args.size() << ") Func " <<
        source.name() << "has.\n";

    Func bounded;

    std::vector<Expr> actuals;
    for (size_t i = 0; i < bounds.size(); i++) {
        Var arg_var = args[i];

        actuals.push_back(clamp(likely(arg_var), bounds[i].first, bounds[i].first + bounds[i].second - 1));
    }

    // If there were fewer bounds than dimensions, regard the ones at the end as unbounded.
    actuals.insert(actuals.end(), args.begin() + actuals.size(), args.end());

    bounded(args) = source(actuals);
    return bounded;
}

Func constant_exterior(const Func &source, Expr value,
                       const std::vector<std::pair<Expr, Expr> > &bounds) {
    std::vector<Var> args(source.args());
    user_assert(args.size() >= bounds.size()) <<
        "constant_exterior called with more bounds (" << bounds.size() <<
        ") than dimensions (" << source.args().size() << ") Func " <<
        source.name() << "has.\n";

    Func bounded;

    Expr out_of_bounds = cast<bool>(false);
    for (size_t i = 0; i < bounds.size(); i++) {
        Var arg_var = source.args()[i];
        out_of_bounds = (out_of_bounds ||
                         arg_var < bounds[i].first ||
                         arg_var >= bounds[i].first + bounds[i].second);
    }

    bounded(args) = select(out_of_bounds, value, repeat_edge(source, bounds)(args));

    return bounded;
}



Func repeat_image(const Func &source,
                  const std::vector<std::pair<Expr, Expr> > &bounds) {
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
        Expr coord = arg_var - min;  // Enforce zero origin.
        coord = coord % extent;      // Range is 0 to w-1
        coord = coord + min;         // Restore correct min

        coord = select(arg_var < min || arg_var >= min + extent, coord,
                       clamp(likely(arg_var), min, min + extent - 1));
        actuals.push_back(coord);
    }

    // If there were fewer bounds than dimensions, regard the ones at the end as unbounded.
    assert(args.begin() + actuals.size() == args.end());
    actuals.insert(actuals.end(), args.begin() + actuals.size(), args.end());

    Func bounded;
    bounded(args) = source(actuals);

    return bounded;
}

Func mirror_image(const Func &source,
                  const std::vector<std::pair<Expr, Expr> > &bounds) {
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
        Expr coord = arg_var - min;    // Enforce zero origin.
        coord = coord % (2 * extent);  // Range is 0 to 2w-1
        coord = select(coord >= extent, 2 * extent - 1 - coord, coord);  // Range is -w+1, w
        coord = coord + min; // Restore correct min
        coord = clamp(coord, min, min + extent - 1);
        coord = select(arg_var < min || arg_var >= min + extent, coord,
                       clamp(likely(arg_var), min, min + extent-1));
        actuals.push_back(coord);
    }

    // If there were fewer bounds than dimensions, regard the ones at the end as unbounded.
    actuals.insert(actuals.end(), args.begin() + actuals.size(), args.end());

    Func bounded;

    bounded(args) = source(actuals);

    return bounded;
}

Func mirror_interior(const Func &source,
                     const std::vector<std::pair<Expr, Expr> > &bounds) {
    std::vector<Var> args(source.args());
    user_assert(args.size() >= bounds.size()) <<
        "mirror_interior called with more bounds (" << bounds.size() <<
        ") than dimensions (" << args.size() << ") Func " <<
        source.name() << "has.\n";

    Func mirrored;

    std::vector<Expr> actuals;
    for (size_t i = 0; i < bounds.size(); i++) {
        Var arg_var = args[i];

        Expr min = bounds[i].first;
        Expr extent = bounds[i].second;
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
    }

    // If there were fewer bounds than dimensions, regard the ones at the end as unbounded.
    actuals.insert(actuals.end(), args.begin() + actuals.size(), args.end());

    Func bounded;
    bounded(args) = source(actuals);

    return bounded;
}

}

}
