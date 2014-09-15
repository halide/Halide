#include "BoundaryConditions.h"

namespace Halide {

namespace BoundaryConditions {

Func constant_exterior(const Func &source, Expr value,
                       const std::vector<std::pair<Expr, Expr> > &bounds) {
    std::vector<Var> args(source.args());
    user_assert(args.size() >= bounds.size()) <<
        "constant_exterior called with more bounds (" << bounds.size() <<
        ") than dimensions (" << source.args().size() << ") Func " <<
        source.name() << "has.\n";

    Func bounded;

    Expr out_of_bounds = cast<bool>(false);
    std::vector<Expr> actuals;

    for (size_t i = 0; i < bounds.size(); i++) {
        Var arg_var = args[i];

        out_of_bounds = out_of_bounds ||
            (arg_var < bounds[i].first || arg_var >= (bounds[i].first + bounds[i].second));
        actuals.push_back(clamp(arg_var, bounds[i].first, bounds[i].first + bounds[i].second - 1));
    }


    // If there were fewer bounds than dimensions, regard the ones at the end as unbounded.
    actuals.insert(actuals.end(), args.begin() + actuals.size(), args.end());

    bounded(args) = select(out_of_bounds, value, source(actuals));
    return bounded;
}

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

        actuals.push_back(clamp(arg_var, bounds[i].first, bounds[i].first + bounds[i].second - 1));
    }

    // If there were fewer bounds than dimensions, regard the ones at the end as unbounded.
    actuals.insert(actuals.end(), args.begin() + actuals.size(), args.end());

    bounded(args) = source(actuals);
    return bounded;
}

Func repeat_image(const Func &source,
                  const std::vector<std::pair<Expr, Expr> > &bounds) {
    std::vector<Var> args(source.args());
    user_assert(args.size() >= bounds.size()) <<
        "repeat_image called with more bounds (" << bounds.size() <<
        ") than dimensions (" << args.size() << ") Func " <<
        source.name() << "has.\n";

    Func bounded;

    std::vector<Expr> actuals;
    for (size_t i = 0; i < bounds.size(); i++) {
        Var arg_var = args[i];

        // Andrew: does this logic still work well with bounds inference,
        // particularly after the min offseting.
        Expr min = bounds[i].first;
        Expr extent = bounds[i].second;
        Expr coord = arg_var - min;  // Enforce zero origin.
        coord = coord % extent;      // Range is 0 to w-1
        coord = coord + min;         // Restore correct min

        actuals.push_back(coord);
    }

    // If there were fewer bounds than dimensions, regard the ones at the end as unbounded.
    assert(args.begin() + actuals.size() == args.end());
    actuals.insert(actuals.end(), args.begin() + actuals.size(), args.end());

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

    Func bounded;

    std::vector<Expr> actuals;
    for (size_t i = 0; i < bounds.size(); i++) {
        Var arg_var = args[i];

        // Andrew: does this logic still work well with bounds inference,
        // particularly after the min offseting.
        Expr min = bounds[i].first;
        Expr extent = bounds[i].second;
        Expr coord = arg_var - min;    // Enforce zero origin.
        coord = coord % (2 * extent);  // Range is 0 to 2w-1
        coord = clamp(select(coord >= extent, 2 * extent - 1 - coord, coord), 0, extent - 1);  // Range is -w+1, w
        coord = cast<int32_t>(coord + min); // Restore correct min

        actuals.push_back(coord);
    }

    // If there were fewer bounds than dimensions, regard the ones at the end as unbounded.
    actuals.insert(actuals.end(), args.begin() + actuals.size(), args.end());

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

    Func bounded;

    std::vector<Expr> actuals;
    for (size_t i = 0; i < bounds.size(); i++) {
        Var arg_var = args[i];

        // Andrew: does this logic still work well with bounds inference,
        // particularly after the min offseting.
        Expr min = bounds[i].first;
        Expr limit = bounds[i].second - 1;
        Expr coord = arg_var - min;  // Enforce zero origin.
        coord = coord % (2 * limit); // Range is 0 to 2w-1
        coord = coord - limit;       // Range is -w, w
        coord = abs(coord);          // Range is 0, w
        coord = limit - coord;       // Range is 0, w
        coord = cast<int32_t>(coord + min); // Restore correct min

        actuals.push_back(coord);
    }

    // If there were fewer bounds than dimensions, regard the ones at the end as unbounded.
    actuals.insert(actuals.end(), args.begin() + actuals.size(), args.end());

    bounded(args) = source(actuals);
    return bounded;
}

}

}
