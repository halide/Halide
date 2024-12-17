#include "ApproximationTables.h"

namespace Halide {
namespace Internal {

namespace {

using OO = ApproximationPrecision::OptimizationObjective;

// clang-format off
// Generate this table with:
//   python3 src/polynomial_optimizer.py atan --order 1 2 3 4 5 6 7 8 --loss mse mae mulpe mulpe_mae --no-gui --format table
//
// Note that the maximal errors are computed with numpy with double precision.
// The real errors are a bit larger with single-precision floats (see correctness/fast_arctan.cpp).
// Also note that ULP distances which are not units are bogus, but this is because this error
// was again measured with double precision, so the actual reconstruction had more bits of
// precision than the actual float32 target value. So in practice the MaxULP Error
// will be close to round(MaxUlpE).
const std::vector<Approximation> table_atan = {
    {OO::MSE, 9.249650e-04, 7.078984e-02, 2.411e+06, {+8.56188008e-01}},
    {OO::MSE, 1.026356e-05, 9.214909e-03, 3.985e+05, {+9.76213454e-01, -2.00030200e-01}},
    {OO::MSE, 1.577588e-07, 1.323851e-03, 6.724e+04, {+9.95982073e-01, -2.92278128e-01, +8.30180680e-02}},
    {OO::MSE, 2.849011e-09, 1.992218e-04, 1.142e+04, {+9.99316541e-01, -3.22286501e-01, +1.49032461e-01, -4.08635592e-02}},
    {OO::MSE, 5.667504e-11, 3.080100e-05, 1.945e+03, {+9.99883373e-01, -3.30599535e-01, +1.81451316e-01, -8.71733830e-02, +2.18671936e-02}},
    {OO::MSE, 1.202662e-12, 4.846916e-06, 3.318e+02, {+9.99980065e-01, -3.32694393e-01, +1.94019697e-01, -1.17694732e-01, +5.40822080e-02, -1.22995279e-02}},
    {OO::MSE, 2.672889e-14, 7.722732e-07, 5.664e+01, {+9.99996589e-01, -3.33190090e-01, +1.98232868e-01, -1.32941469e-01, +8.07623712e-02, -3.46124853e-02, +7.15115276e-03}},
    {OO::MSE, 6.147315e-16, 1.245768e-07, 9.764e+00, {+9.99999416e-01, -3.33302229e-01, +1.99511173e-01, -1.39332647e-01, +9.70944891e-02, -5.68823386e-02, +2.25679012e-02, -4.25772648e-03}},

    {OO::MAE, 1.097847e-03, 4.801638e-02, 2.793e+06, {+8.33414544e-01}},
    {OO::MAE, 1.209593e-05, 4.968992e-03, 4.623e+05, {+9.72410454e-01, -1.91981283e-01}},
    {OO::MAE, 1.839382e-07, 6.107084e-04, 7.766e+04, {+9.95360080e-01, -2.88702052e-01, +7.93508437e-02}},
    {OO::MAE, 3.296902e-09, 8.164167e-05, 1.313e+04, {+9.99214108e-01, -3.21178073e-01, +1.46272006e-01, -3.89915187e-02}},
    {OO::MAE, 6.523525e-11, 1.147459e-05, 2.229e+03, {+9.99866373e-01, -3.30305517e-01, +1.80162434e-01, -8.51611537e-02, +2.08475020e-02}},
    {OO::MAE, 1.378842e-12, 1.667328e-06, 3.792e+02, {+9.99977226e-01, -3.32622991e-01, +1.93541452e-01, -1.16429278e-01, +5.26504600e-02, -1.17203722e-02}},
    {OO::MAE, 3.055131e-14, 2.480947e-07, 6.457e+01, {+9.99996113e-01, -3.33173716e-01, +1.98078484e-01, -1.32334692e-01, +7.96260166e-02, -3.36062649e-02, +6.81247117e-03}},
    {OO::MAE, 7.013215e-16, 3.757868e-08, 1.102e+01, {+9.99999336e-01, -3.33298615e-01, +1.99465749e-01, -1.39086791e-01, +9.64233077e-02, -5.59142254e-02, +2.18643190e-02, -4.05495427e-03}},

    {OO::MULPE, 1.355602e-03, 1.067325e-01, 1.808e+06, {+8.92130617e-01}},
    {OO::MULPE, 2.100588e-05, 1.075508e-02, 1.822e+05, {+9.89111122e-01, -2.14468039e-01}},
    {OO::MULPE, 3.573985e-07, 1.316370e-03, 2.227e+04, {+9.98665077e-01, -3.02990987e-01, +9.10404434e-02}},
    {OO::MULPE, 6.474958e-09, 1.548508e-04, 2.619e+03, {+9.99842198e-01, -3.26272641e-01, +1.56294460e-01, -4.46207045e-02}},
    {OO::MULPE, 1.313474e-10, 2.533532e-05, 4.294e+02, {+9.99974110e-01, -3.31823782e-01, +1.85886095e-01, -9.30024008e-02, +2.43894760e-02}},
    {OO::MULPE, 3.007880e-12, 3.530685e-06, 5.983e+01, {+9.99996388e-01, -3.33036463e-01, +1.95959706e-01, -1.22068745e-01, +5.83403647e-02, -1.37966171e-02}},
    {OO::MULPE, 6.348880e-14, 4.882649e-07, 8.276e+00, {+9.99999499e-01, -3.33273408e-01, +1.98895454e-01, -1.35153794e-01, +8.43185278e-02, -3.73434598e-02, +7.95583230e-03}},
    {OO::MULPE, 1.369569e-15, 7.585036e-08, 1.284e+00, {+9.99999922e-01, -3.33320840e-01, +1.99708563e-01, -1.40257063e-01, +9.93094012e-02, -5.97138046e-02, +2.44056181e-02, -4.73371006e-03}},

    {OO::MULPE_MAE, 9.548909e-04, 6.131488e-02, 2.570e+06, {+8.46713042e-01}},
    {OO::MULPE_MAE, 1.159917e-05, 6.746680e-03, 3.778e+05, {+9.77449762e-01, -1.98798279e-01}},
    {OO::MULPE_MAE, 1.783646e-07, 8.575388e-04, 6.042e+04, {+9.96388826e-01, -2.92591679e-01, +8.24585555e-02}},
    {OO::MULPE_MAE, 3.265269e-09, 1.190548e-04, 9.505e+03, {+9.99430906e-01, -3.22774535e-01, +1.49370817e-01, -4.07480795e-02}},
    {OO::MULPE_MAE, 6.574962e-11, 1.684690e-05, 1.515e+03, {+9.99909079e-01, -3.30795737e-01, +1.81810037e-01, -8.72860225e-02, +2.17776539e-02}},
    {OO::MULPE_MAE, 1.380489e-12, 2.497538e-06, 2.510e+02, {+9.99984893e-01, -3.32748885e-01, +1.94193211e-01, -1.17865932e-01, +5.40633775e-02, -1.22309990e-02}},
    {OO::MULPE_MAE, 3.053218e-14, 3.784868e-07, 4.181e+01, {+9.99997480e-01, -3.33205127e-01, +1.98309644e-01, -1.33094430e-01, +8.08643094e-02, -3.45859503e-02, +7.11261604e-03}},
    {OO::MULPE_MAE, 7.018877e-16, 5.862915e-08, 6.942e+00, {+9.99999581e-01, -3.33306326e-01, +1.99542180e-01, -1.39433369e-01, +9.72462857e-02, -5.69734398e-02, +2.25639390e-02, -4.24074590e-03}},
};
// clang-format on
}  // namespace

const Approximation *find_best_approximation(const std::vector<Approximation> &table,
                                             ApproximationPrecision precision) {
#define DEBUG_APPROXIMATION_SEARCH 0
    const Approximation *best = nullptr;
    constexpr int term_cost = 20;
    constexpr int extra_term_cost = 200;
    double best_score = 0;
#if DEBUG_APPROXIMATION_SEARCH
    std::printf("Looking for min_terms=%d, max_absolute_error=%f\n",
                precision.constraint_min_poly_terms, precision.constraint_max_absolute_error);
#endif
    for (size_t i = 0; i < table.size(); ++i) {
        const Approximation &e = table[i];

        double penalty = 0.0;

        int obj_score = e.objective == precision.optimized_for ? 100 * term_cost : 0;
        if (precision.optimized_for == ApproximationPrecision::MULPE_MAE &&
            e.objective == ApproximationPrecision::MULPE) {
            obj_score = 50 * term_cost;  // When MULPE_MAE is not available, prefer MULPE.
        }

        int num_terms = int(e.coefficients.size());
        int term_count_score = (12 - num_terms) * term_cost;
        if (num_terms < precision.constraint_min_poly_terms) {
            penalty += (precision.constraint_min_poly_terms - num_terms) * extra_term_cost;
        }

        double precision_score = 0;
        // If we don't care about the maximum number of terms, we maximize precision.
        switch (precision.optimized_for) {
        case ApproximationPrecision::MSE:
            precision_score = -std::log(e.mse);
            break;
        case ApproximationPrecision::MAE:
            precision_score = -std::log(e.mae);
            break;
        case ApproximationPrecision::MULPE:
            precision_score = -std::log(e.mulpe);
            break;
        case ApproximationPrecision::MULPE_MAE:
            precision_score = -0.5 * std::log(e.mulpe * e.mae);
            break;
        }

        if (precision.constraint_max_absolute_error > 0.0 &&
            precision.constraint_max_absolute_error < e.mae) {
            float error_ratio = e.mae / precision.constraint_max_absolute_error;
            penalty += 20 * error_ratio * extra_term_cost;  // penalty for not getting the required precision.
        }

        double score = obj_score + term_count_score + precision_score - penalty;
#if DEBUG_APPROXIMATION_SEARCH
        std::printf("Score for %zu (%zu terms): %f = %d + %d + %f - penalty %f\n",
                    i, e.coefficients.size(), score, obj_score, term_count_score,
                    precision_score, penalty);
#endif
        if (score > best_score || best == nullptr) {
            best = &e;
            best_score = score;
        }
    }
#if DEBUG_APPROXIMATION_SEARCH
    std::printf("Best score: %f\n", best_score);
#endif
    return best;
}

const Approximation *best_atan_approximation(Halide::ApproximationPrecision precision) {
    return find_best_approximation(table_atan, precision);
}

}  // namespace Internal
}  // namespace Halide
