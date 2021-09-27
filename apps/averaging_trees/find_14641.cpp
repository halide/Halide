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

Dag avg_dags(const Dag &l, const Dag &r, Round round) {
    Dag combined = l;
    int left_output_id = l.num_inputs + (int)l.ops.size() - 1;
    for (const auto &op : r.ops) {
        auto adjust_id = [&](int i) {
            if (i < l.num_inputs) {
                return i;
            } else {
                return i + (int)l.ops.size();
            }
        };
        combined.ops.emplace_back(adjust_id(op.i), adjust_id(op.j), op.round);
    }
    int right_output_id = l.num_inputs + (int)combined.ops.size() - 1;
    combined.ops.emplace_back(left_output_id, right_output_id, round);
    return combined;
}

int main(int argc, char **argv) {

    vector<Dag> all_dags;
    for (size_t i = 0; i < 32; i++) {
        Dag dag;
        dag.num_inputs = 5;

        /*
        if (i & 1) {
            dag.ops.emplace_back(0, 0, Round::Up);
        } else {
            dag.ops.emplace_back(4, 4, Round::Up);
        }
        */
        dag.ops.emplace_back(0, 4, (i & 1) ? Round::Down : Round::Up);   // 5
        dag.ops.emplace_back(2, 5, (i & 2) ? Round::Down : Round::Up);   // 6
        dag.ops.emplace_back(2, 6, (i & 4) ? Round::Down : Round::Up);   // 7
        dag.ops.emplace_back(1, 3, (i & 8) ? Round::Down : Round::Up);   // 8
        dag.ops.emplace_back(7, 8, (i & 16) ? Round::Down : Round::Up);  // 9

        all_dags.push_back(dag);
    }

    // Now try all combinations of N of these, starting with N = 2
    for (int n = 2; n < 4; n++) {
        int num_ops = n;  // total guess
        vector<Dag> combiners = enumerate_dags(n, num_ops);
        std::cout << "n = " << n << "\n";
        for (auto combiner : combiners) {
            for (int i = 0; i < std::pow(32, n); i++) {
                vector<int> inputs;
                int idx = i;
                bool ok = true;
                for (int j = 0; j < n; j++) {
                    inputs.push_back(idx & 31);
                    idx >>= 5;
                    size_t sz = inputs.size();
                    if (sz > 1) {
                        ok &= (inputs[sz - 1] > inputs[sz - 2]);
                    }
                }
                assert((int)inputs.size() == n);
                ok = ok && ((inputs[0] & 1) != (inputs[1] & 1));
                if (!ok) continue;

                vector<Dag> combined;
                for (int i : inputs) {
                    combined.push_back(all_dags[i]);
                }
                for (auto op : combiner.ops) {
                    combined.push_back(avg_dags(combined[op.i], combined[op.j], op.round));
                }

                auto r = combined.back().bias();
                if (r.max_error == 0.5 && r.bias == 0) {
                    combined.back().simplify(true);
                    combined.back().dump();
                    std::cout << r.bias << " " << r.min_error << " " << r.max_error << "\n";
                }
            }
        }
    }

    return 0;
}
