
#include "dag.h"

template<typename AcceptDAGCount, typename AcceptDag>
vector<Dag> enumerate_dags(std::mt19937 *rng,
                           const vector<int> &ids,
                           const vector<int> &kernel,
                           int num_inputs,
                           Round round = Round::Down,
                           AcceptDAGCount *accept_dag_count = nullptr,
                           AcceptDag *accept_dag = nullptr) {

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

    size_t count = 0;

    size_t num_partitions = (1ULL << (int)ids.size());

    for (size_t p = 0; p < num_partitions; p++) {
        // Arbitrarily rotate the set of partitions so that we start
        // somewhere interesting.
        size_t i = rng ? ((*rng)() & (num_partitions - 1)) : p;

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
        vector<Dag> left = enumerate_dags<void, void>(rng, left_ids, kernel, num_inputs, subround);
        vector<Dag> right = enumerate_dags<void, void>(rng, right_ids, kernel, num_inputs, subround);

        if constexpr (!std::is_void<AcceptDAGCount>::value) {
            count += left.size() * right.size();
            (*accept_dag_count)(count);
        }

        // We want to iterate over all pairs in a pseudo-random order
        if (rng) {
            std::shuffle(left.begin(), left.end(), *rng);
            std::shuffle(right.begin(), right.end(), *rng);
            if (left.size() > 32) {
                left.erase(left.begin() + 32, left.end());
            }
            if (right.size() > 32) {
                right.erase(right.begin() + 32, right.end());
            }
        }

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
                    if constexpr (!std::is_void<AcceptDag>::value) {
                        (*accept_dag)(combined);
                    } else {
                        result.push_back(combined);
                    }
                }
            }
        }
    }

    return result;
}

int main(int argc, const char **argv) {

    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " seed 1 4 6 4 1\n";
        return 0;
    }

    const int num_inputs = argc - 2;
    vector<int> kernel;
    int kernel_sum = 0;
    int seed = std::atoi(argv[1]);
    for (int i = 2; i < argc; i++) {
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

    // vector<Dag> dags = enumerate_dags(ids, kernel, num_inputs);
    auto start = std::chrono::high_resolution_clock::now();
    int counter = 0;
    double best_bias = 1e100, error_of_best_bias = 1e100;
    double best_error = 1e100, bias_of_best_error = 1e100;
    size_t ops_of_best_bias = -1, ops_of_best_error = -1;

    set<int32_t> difficult_inputs;

    size_t quick_rejected = 0, quick_accepted = 0;

    set<Dag> seen_dags;

    size_t num_dags;

    const bool random = kernel_sum > 16;
    std::mt19937 rng{(unsigned)seed};

    if (random) {
        std::cout << "Kernel sums to more than 16. Using randomization. Search will not be"
                     " exhaustive.\n";
    }

    auto accept_dag_count = [&](size_t s) {
        num_dags = s;
    };

    auto accept_dag = [&](Dag &dag) {
        dag.simplify(false);

        if (counter % 10 == 0) {
            auto t = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(t - start).count();
            auto total_time = (elapsed / counter) * num_dags;
            auto minutes_remaining = (total_time - elapsed) / 60;
            std::cout << counter << " / " << num_dags << " (" << minutes_remaining << " minutes remaining) " << difficult_inputs.size() << " " << quick_rejected << " " << quick_accepted << "\n";
        }
        counter++;
        // Try all rounding options for this dag
        size_t tried = 0;
        size_t rounding_choices = random ? 65536 : ((size_t)1 << dag.ops.size());
        for (size_t c = 0; c < rounding_choices; c++) {

            size_t i = random ? rng() : c;

            for (size_t j = 0; j < dag.ops.size(); j++) {
                // Try solutions that round up first, because x86 has
                // average-round-up but not average-round-down.
                dag.ops[j].round = ((i >> j) & 1) ? Round::Down : Round::Up;
            }

            if (!random && !seen_dags.insert(dag).second) {
                continue;
            }

            tried++;
            if (random && tried >= 16) {
                break;
            }

            {
                auto p = dag.bias_on(difficult_inputs);
                if (p.error > best_error) {
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

            if (error > best_error) {
                if (difficult_inputs.count(p.worst_input)) {
                    // We should have quick rejected it
                    std::cout << p.worst_input << " " << p.error << "\n";
                    assert(false);
                }
            }

            difficult_inputs.insert(p.worst_input);

            dag.simplify(true);

            bool better_bias =
                (std::abs(bias) < std::abs(best_bias) ||
                 (std::abs(bias) == std::abs(best_bias) &&
                  (error < error_of_best_bias ||
                   (error == error_of_best_bias &&
                    dag.ops.size() < ops_of_best_bias))));
            bool better_error =
                (error < best_error ||
                 (error == best_error &&
                  (std::abs(bias) < std::abs(bias_of_best_error) ||
                   (std::abs(bias) == std::abs(bias_of_best_error) &&
                    dag.ops.size() < ops_of_best_error))));

            if (better_bias) {
                best_bias = bias;
                error_of_best_bias = error;
                ops_of_best_bias = dag.ops.size();
            }
            if (better_error) {
                best_error = error;
                bias_of_best_error = bias;
                ops_of_best_error = dag.ops.size();
            }
            if (better_bias || better_error) {
                dag.dump();
                std::cout << "Bias: " << bias << " Error: " << error << "\n";
            }
        }
    };

    do {
        enumerate_dags(random ? &rng : nullptr, ids, kernel, num_inputs, Round::Down, &accept_dag_count, &accept_dag);
    } while (random);

    if (best_error > 0.5 || best_bias > 0) {
        std::cout << "No optimal averaging tree found. Try doubling the coefficients.\n";
    }

    return 0;
}
