#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    // Say we have a whole bunch of producer-consumer pairs, scheduled
    // differently, and we always want to compute the corresponding producer
    // innermost, even though that's not the same Var for each consumer. We can
    // write a generic schedule using Func::split_vars() to get the list of
    // scheduling points for each g

    std::vector<Func> producers, consumers;
    Var x, xo, xi;

    for (int i = 0; i < 4; i++) {
        producers.emplace_back("f" + std::to_string(i));
        producers.back()(x) = x + i;
        consumers.emplace_back("g" + std::to_string(i));
        consumers.back()(x) = producers.back()(x) + 1;
    }

    // And we want to schedule some of consumers differently than others:

    for (int i = 0; i < 4; i++) {
        consumers[i].compute_root();

        if (i == 3) {
            consumers[3].split(x, xo, xi, 8);
        }

        producers[i].compute_at(consumers[i], consumers[i].split_vars()[0]);

        // Just check these schedules are all legal, by running each but not
        // checking the output.
        consumers[i].realize({10});
    }

    printf("Success!\n");
    return 0;
}
