#include "RoundingMode.h"
#include "Error.h"
#include "llvm/Support/ErrorHandling.h"

namespace Halide {
namespace Internal {

const char* rounding_mode_to_string(RoundingMode rm) {
    switch(rm) {
        case RoundingMode::TowardZero:
            return "rz";
        case RoundingMode::ToNearestTiesToEven:
            return "rne";
        case RoundingMode::ToNearestTiesToAway:
            return "rna";
        case RoundingMode::TowardPositiveInfinity:
            return "ru";
        case RoundingMode::TowardNegativeInfinity:
            return "rd";
        default:
            internal_error << "Unhandled rounding mode " << static_cast<int>(rm) << "\n";
    }
    llvm_unreachable("Rounding mode not handled");
}

}
};
