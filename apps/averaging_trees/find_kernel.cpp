
#include "dag.h"

vector<Dag> enumerate_dags(const vector<int> &ids, const vector<int> &kernel, int num_inputs, Round round = Round::Down) {

    vector<Dag> result;
    // For all possible partitions of ids into two equal sets,
    // generate a dag for each and combine.

    if (ids.size() == 2) {
        result.emplace_back();
        result.back().num_inputs = num_inputs;
        result.back().ops.emplace_back(ids[0], ids[1], round);
        return result;
    }

    // To generate all partitions, we'll iterate up to 1 << ids.size()
    // and treat that as a bit-mask telling us which side each id goes
    // to.

    for (size_t i = 0; i < (1ULL << (int)ids.size()); i++) {
        if (__builtin_popcount(i) != ids.size() / 2) {
            // Not a balanced partition
            continue;
        }

        vector<int> left_ids, right_ids;
        left_ids.reserve(ids.size() / 2);
        right_ids.reserve(ids.size() / 2);
        for (int j = 0; j < (int)ids.size(); j++) {
            if (i & (1 << j)) {
                left_ids.push_back(ids[j]);
            } else {
                right_ids.push_back(ids[j]);
            }
        }

        // avg is commutative, so to break symmetry we require that
        // the set that goes left is lexicographically before the set
        // that goes right.
        bool before = true;
        for (int j = 0; j < (int)left_ids.size(); j++) {
            if (left_ids[j] < right_ids[j]) {
                break;
            } else if (left_ids[j] > right_ids[j]) {
                before = false;
                break;
            }
        }
        if (!before) continue;

        // Each instance of each id is the same, so again to break
        // symmetries we require than for each id, they go to the left
        // before going to the right.
        vector<bool> id_has_gone_right(kernel.size(), false);
        bool bad = false;
        for (int j = 0; j < (int)ids.size(); j++) {
            if (i & (1 << j)) {
                bad |= id_has_gone_right[ids[j]];
            } else {
                id_has_gone_right[ids[j]] = true;
            }
        }
        if (bad) continue;

        Round subround = round == Round::Down ? Round::Up : Round::Down;
        vector<Dag> left = enumerate_dags(left_ids, kernel, num_inputs, subround);
        vector<Dag> right = enumerate_dags(right_ids, kernel, num_inputs, subround);

        for (const auto &l : left) {
            for (const auto &r : right) {
                Dag combined = l;
                int left_output_id = num_inputs + (int)l.ops.size() - 1;
                for (const auto &op : r.ops) {
                    auto adjust_id = [&](int i) {
                        if (i < num_inputs) {
                            return i;
                        } else {
                            return i + (int)l.ops.size();
                        }
                    };
                    combined.ops.emplace_back(adjust_id(op.i), adjust_id(op.j), op.round);
                }
                int right_output_id = num_inputs + (int)combined.ops.size() - 1;
                combined.ops.emplace_back(left_output_id, right_output_id, round);

                // Any ids that share a coefficient could be swapped
                // in the program, so break the symmetry by rejecting
                // anything that uses an large id with the same
                // coefficient as a small id before the small one.
                map<int, int> first_use_of_coefficient;
                for (auto k : kernel) {
                    first_use_of_coefficient[k] = -1;
                }
                bool bad = false;
                for (const auto &op : combined.ops) {
                    if (op.i < num_inputs) {
                        int coeff = kernel[op.i];
                        if (first_use_of_coefficient[coeff] < 0) {
                            first_use_of_coefficient[coeff] = op.i;
                        } else if (first_use_of_coefficient[coeff] > op.i) {
                            bad = true;
                        }
                    }
                    if (op.j < num_inputs) {
                        int coeff = kernel[op.j];
                        if (first_use_of_coefficient[coeff] < 0) {
                            first_use_of_coefficient[coeff] = op.j;
                        } else if (first_use_of_coefficient[coeff] > op.j) {
                            bad = true;
                        }
                    }
                }
                if (!bad) {
                    result.push_back(combined);
                }
            }
        }
    }

    return result;
}

int main(int argc, const char **argv) {

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " 1 4 6 4 1\n";
        return 0;
    }

    const int num_inputs = argc - 1;
    vector<int> kernel;
    int kernel_sum = 0;
    for (int i = 1; i < argc; i++) {
        kernel.push_back(std::atoi(argv[i]));
        kernel_sum += kernel.back();
    }
    assert(((kernel_sum & (kernel_sum - 1)) == 0) && "Kernel must sum to a power of two");

    // Place the inputs at the leaves of a balanced binary tree
    vector<int> ids;
    int id = 0;
    for (auto k : kernel) {
        for (int i = 0; i < k; i++) {
            ids.push_back(id);
        }
        id++;
    }

    vector<Dag> dags = enumerate_dags(ids, kernel, num_inputs);
    auto start = std::chrono::high_resolution_clock::now();
    std::cout << "Dags: " << dags.size() << "\n";
    int counter = 0;
    double best_bias = 1e100, error_of_best_bias = 1e100;
    double best_error = 1e100, bias_of_best_error = 1e100;

    set<int32_t> difficult_inputs;

    size_t quick_rejected = 0, quick_accepted = 0;

    set<Dag> seen_dags;

    for (auto &dag : dags) {

        dag.simplify(false);

        if (counter % 10 == 0) {
            auto t = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(t - start).count();
            auto total_time = (elapsed / counter) * dags.size();
            auto minutes_remaining = (total_time - elapsed) / 60;
            std::cout << counter << " / " << dags.size() << " (" << minutes_remaining << " minutes remaining) " << difficult_inputs.size() << " " << quick_rejected << " " << quick_accepted << "\n";
        }
        counter++;
        // Try all rounding options for this dag
        std::set<size_t> positive_bias, negative_bias;
        size_t rounding_choices = (size_t)1 << dag.ops.size();
        for (size_t i = 0; i < rounding_choices; i++) {

            for (size_t j = 0; j < dag.ops.size(); j++) {
                dag.ops[j].round = ((i >> j) & 1) ? Round::Up : Round::Down;
            }

            if (!seen_dags.insert(dag).second) {
                continue;
            }

            if (dag.estimated_bias() != 0) {
                continue;
            }

            // Before we do an expensive bias computation, see if we
            // already know this bias will be worse than a similar
            // tree
            bool skip_it = false;
            for (size_t j : positive_bias) {
                if ((i & j) == j) {
                    // We round up everywhere this other tree does,
                    // and more, and it has positive bias, so we're
                    // screwed.
                    skip_it = true;
                    break;
                }
            }
            for (size_t j : negative_bias) {
                if ((i & j) == i) {
                    // We round down everywhere this other tree does,
                    // and more, and it has negative bias, so we're
                    // screwed.
                    skip_it = true;
                    break;
                }
            }
            if (skip_it) continue;

            {
                auto p = dag.bias_on(difficult_inputs);
                if (p.error >= best_error) {
                    // std::cerr << "Quick reject because error is: " << p.error << "\n";
                    quick_rejected++;
                    continue;
                } else {
                    quick_accepted++;
                }
            }

            auto p = dag.bias();
            double bias = p.bias;
            double error = p.error;

            if (error >= best_error) {
                if (difficult_inputs.count(p.worst_input)) {
                    // We should have quick rejected it
                    std::cout << p.worst_input << " " << p.error << "\n";
                    assert(false);
                }
            }

            difficult_inputs.insert(p.worst_input);

            if (bias > 0) {
                positive_bias.insert(i);
            } else if (bias < 0) {
                negative_bias.insert(i);
            }

            bool better_bias =
                (std::abs(bias) < std::abs(best_bias) ||
                 (std::abs(bias) == std::abs(best_bias) && error < error_of_best_bias));
            bool better_error =
                (error < best_error ||
                 (error == best_error && std::abs(bias) < std::abs(bias_of_best_error)));
            if (better_bias) {
                best_bias = bias;
                error_of_best_bias = error;
            }
            if (better_error) {
                best_error = error;
                bias_of_best_error = bias;
            }
            if (better_bias || better_error) {
                dag.dump();
                std::cout << "Bias: " << bias << " Error: " << error << " Estimated bias: " << dag.estimated_bias() << "\n";
            }
        }
    }

    return 0;
}
