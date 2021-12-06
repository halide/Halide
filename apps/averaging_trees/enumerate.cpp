
#include "dag.h"

vector<Dag> enumerate_dags(int num_inputs, int num_ops, int max_unused_values = 1) {
    if (num_ops == 0) {
        Dag do_nothing{num_inputs, vector<Avg>{}};
        return vector<Dag>{do_nothing};
    }

    vector<Dag> dags = enumerate_dags(num_inputs, num_ops - 1, max_unused_values + 1);
    vector<Dag> new_dags;
    for (const auto &dag : dags) {
        // Add one new op to this dag. Don't worry about rounding direction.
        if (dag.unused_values() <= max_unused_values) {
            new_dags.push_back(dag);
        }
        int l = dag.last_used_input();
        for (int i = 0; i < num_inputs + (int)dag.ops.size(); i++) {
            // We're invariant to the order of the inputs, so force
            // the enumeration to consume them in-order
            if (i < num_inputs && i > l + 1) continue;
            for (int j = i + 1; j < num_inputs + (int)dag.ops.size(); j++) {
                if (j < num_inputs && j > std::max(i, l) + 1) continue;

                int instances_of_this_op = 0;
                for (const auto &op : dag.ops) {
                    instances_of_this_op += (op.i == i && op.j == j) ? 1 : 0;
                }
                // We're allowed two instances of each op: one rounding up and another rounding down

                if (instances_of_this_op < 2) {
                    new_dags.push_back(dag);
                    new_dags.back().ops.push_back(Avg{i, j, Round::Down});
                    if (new_dags.back().unused_values() > max_unused_values) {
                        new_dags.pop_back();
                    }
                }
            }
        }
    }

    return new_dags;
}

int main(int argc, const char **argv) {

    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " num_inputs max_ops\n";
        return 0;
    }

    const int num_inputs = atoi(argv[1]);
    const int max_ops = atoi(argv[2]);

    auto dags = enumerate_dags(num_inputs, max_ops);

    map<vector<int>, Result> best_error_map, best_bias_map;

    std::cout << "Enumerating " << dags.size() << " dags\n";
    size_t counter = 0;

    for (auto &dag : dags) {
        counter++;

        if (counter % 10000 == 0) {
            std::cout << counter << "/" << dags.size() << "\n";
        }

        if (dag.ops.empty()) {
            continue;
        }

        vector<int> kernel = dag.effective_kernel();
        std::sort(kernel.begin(), kernel.end());

        // Skip dags that compute something then discard it
        vector<bool> used(dag.ops.size() + num_inputs, false);
        for (auto op : dag.ops) {
            used[op.i] = used[op.j] = true;
        }
        bool unused_intermediate = false;
        for (int i = 0; i < (int)dag.ops.size() - 1; i++) {
            unused_intermediate |= (!used[num_inputs + i]);
        }
        if (unused_intermediate) {
            continue;
        }

        // Shift all the kernel coefficients as rightwards as possible
        // to canonicalize the kernel.
        vector<int> normalized_kernel;
        normalized_kernel.reserve(kernel.size());
        int mask = 0;
        for (int i : kernel) {
            mask |= i;
        }
        int shift = 0;
        while (!(mask & 1)) {
            mask >>= 1;
            shift++;
        }
        for (int i : kernel) {
            normalized_kernel.push_back(i >> shift);
        }

        // Try all possible roundings and find the one with the least
        // bias and the one with the least error
        Result best_bias{0, 0, 0, 0}, best_error{0, 0, 0, 0};
        size_t best_bias_idx = 0, best_error_idx = 0;
        for (size_t i = 0; i < ((size_t)1 << dag.ops.size()); i++) {
            for (size_t j = 0; j < dag.ops.size(); j++) {
                dag.ops[j].round = ((i >> j) & 1) ? Round::Up : Round::Down;
            }

            /*
            // Experimentally I've found that the following is a valid
            // filter (i.e. we don't miss any perfect kernels this
            // way).
            auto bias_estimate = dag.estimated_bias();
            if (bias_estimate != 0) {
                continue;
            }
            */

            // Skip dags that duplicate an op
            bool bad = false;
            for (size_t j = 0; j < dag.ops.size(); j++) {
                for (size_t k = 0; k < j; k++) {
                    bad |= (dag.ops[j] == dag.ops[k]);
                }
            }
            if (bad) continue;

            auto bias_and_error = dag.bias();
            auto bias = bias_and_error.bias;
            auto error = bias_and_error.error;

            /*
            if (bias == 0 && bias_estimate != 0) {
                dag.dump();
                std::cout << bias << " " << error << " " << bias_estimate << "\n";
                std::getc(stdin);
                // assert(false);
            }
            */

            if (best_error.error == 0 || std::abs(bias) < std::abs(best_bias.bias) ||
                (std::abs(bias) == std::abs(best_bias.bias) &&
                 error < best_bias.error)) {
                best_bias = bias_and_error;
                best_bias_idx = i;
            }
            if (best_error.error == 0 || error < best_error.error ||
                (error == best_error.error &&
                 std::abs(bias) < std::abs(best_error.bias))) {
                best_error = bias_and_error;
                best_error_idx = i;
            }
        }

        if (best_error.error < 0.5) {
            // Everything rejected
            continue;
        }

        auto bias_dag = dag;
        auto error_dag = dag;

        for (size_t j = 0; j < dag.ops.size(); j++) {
            bias_dag.ops[j].round = ((best_bias_idx >> j) & 1) ? Round::Up : Round::Down;
            error_dag.ops[j].round = ((best_error_idx >> j) & 1) ? Round::Up : Round::Down;
        }

        auto error_it = best_error_map.find(normalized_kernel);
        auto bias_it = best_bias_map.find(normalized_kernel);
        bool better_bias =
            error_it == best_error_map.end() ||
            std::abs(best_bias.bias) < std::abs(bias_it->second.bias) ||
            (std::abs(best_bias.bias) == std::abs(bias_it->second.bias) &&
             best_bias.error < bias_it->second.error);

        bool better_error =
            error_it == best_error_map.end() ||
            best_error.error < error_it->second.error ||
            (best_error.error == error_it->second.error &&
             std::abs(best_error.bias) < std::abs(bias_it->second.bias));

        // Uncomment if you only want perfect trees (zero bias, minimum peak error)
        // better_error &= (best_error.error == 0.5 && best_error.bias == 0);
        // better_bias &= (best_bias.error == 0.5 && best_bias.bias == 0);

        // Uncomment if you only want unbiased trees
        better_error &= best_error.bias == 0;
        better_bias &= best_bias.bias == 0;

        if (better_bias) {
            // This breaks a record for the best bias seen so far
            best_bias_map[normalized_kernel] = best_bias;
            bias_dag.dump();
            std::cout << "Kernel: ";
            for (int c : normalized_kernel) {
                std::cout << c << " ";
            }
            std::cout << "\n"
                      << "Bias: " << best_bias.bias << "\n"
                      << "Max abs error: " << best_bias.error << "\n"
                      << "Min error: " << best_bias.min_error << "\n"
                      << "Max error: " << best_bias.max_error << "\n";
        }

        if (better_error) {
            best_error_map[normalized_kernel] = best_error;

            if (!better_bias || best_error_idx != best_bias_idx) {
                // Only print it if we didn't just print it above
                error_dag.dump();
                std::cout << "Kernel: ";
                for (int c : normalized_kernel) {
                    std::cout << c << " ";
                }
                std::cout << "\n"
                          << "Bias: " << best_error.bias << "\n"
                          << "Max abs error: " << best_error.error << "\n"
                          << "Min error: " << best_error.min_error << "\n"
                          << "Max error: " << best_error.max_error << "\n";
            }
        }
    }

    return 0;
}
