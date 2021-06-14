#ifndef HL_MCTS_H
#define HL_MCTS_H

// TODO(rootjalex): figure out includes.
//                  should this be in the Halide namespace??
#include "Halide.h"
#include "MCTreeNode.h"
//#include <_types/_uint32_t.h>
#include <cmath>        // std::sqrt
#include <cstdint>      // uint32_t
#include <iostream>     // std::cerr
#include <limits>       // std::numeric_limits
#include <memory>       // std::shared_ptr
#include <utility>      // std::pair
#include <vector>       // st::vector
#include <algorithm>    // std::min_element

/*

Templated Monte Carlo Tree Search implementation, designed for state exploration.

TODO(rootjalex): add more details
*/


namespace MCTS {
    // Defined in AutoSchedule.cpp
    double get_exploration_percent();
    double get_exploitation_percent();
    uint32_t get_min_explore();
    uint32_t get_min_exploit();
    uint32_t get_rollout_length();
    uint32_t get_beam_size();

    // State must comply with interface in MCStateInterface.h
    // TODO(rootjalex): Should we do the Action template? needs default (empty) action.
    template<class State, class Action>
    class Solver {
        // The nodes of this search tree - Action is edge that leads to node.
        typedef TreeNode<State, Action> Node;

        // TODO(rootjalex): allow for timer use.

    public:
        // Number of iterations taken so far.
        uint32_t iterations = 0;
        // Total number of iterations to run
        uint32_t max_iterations = 0;
        // Maximum milliseconds allowed for exploration (flexible, non-discrete).
        uint32_t max_milliseconds = 0;

        // Multiplier for Upper Confidence Trees.
        // https://link.springer.com/chapter/10.1007%2F11871842_29
        double uct_k = std::sqrt(2);

        // Maximum depth to explore from a node.
        // (Should be size of DAG, probably).
        uint32_t num_simulations = 0;

    private:
        // If true, uses timer (not yet implemented), otherwise uses iteration count.
        bool use_timer = false;

        // Private to ensure that the static methods defined below are used.
        Solver() = default;

        typedef std::shared_ptr<Node> NodePtr;

        typedef std::pair<NodePtr, State> BeamElement;

        typedef std::vector<BeamElement> Beam;
    public:
        // TODO(rootjalex): implement this.
        // static MakeTimerSolver(uint32_t )
        static Solver<State, Action> MakeIterationSolver(uint32_t max_iterations, uint32_t num_simulations) {
            Solver<State, Action> solver;
            solver.max_iterations = max_iterations;
            solver.use_timer = false;
            solver.num_simulations = num_simulations;
            return solver;
        }

        /*
        static Solver<State, Action> MakeTimerSolver(uint32_t max_milliseconds) {
            Solver<State, Action> solver;
            solver.max_milliseconds = max_milliseconds;
            solver.use_timer = true;
            return solver;
        }
        */

        size_t n_valid_nodes = 0;
        size_t n_invalid_nodes = 0;

        // Node corresponds to best action to take immediately from this state,
        // and includes enough information to iteratively apply it's decisions.
        State solve(const State &starter_state, uint32_t n_decisions, int seed = 1) {
            std::mt19937 rng((uint32_t)seed);

            // TODO(rootjalex): replace with std::make_shared
            NodePtr root_node = NodePtr(new Node(starter_state, Action::Default(), /* parent */ nullptr, rng));
            State current_state = starter_state; // track the current best state.

            const double percent_to_explore = get_exploration_percent();
            const double percent_to_exploit = get_exploitation_percent();
            const uint32_t min_explore_iters = get_min_explore();
            const uint32_t min_exploit_iters = get_min_exploit();
            const uint32_t rollout_length = get_rollout_length();

            // TODO: be smarter about decision allocation?
            internal_assert(max_iterations >= n_decisions) << "Must have enough iterations for the number of decisions made\n";
            // const uint32_t n_iterations_per_decision = max_iterations / n_decisions;

            // uint32_t n_iterations_per_decision = max_iterations / 2;

            for (uint32_t d = 0; d < n_decisions; d++) {
                const uint32_t n_exploitation = ceil(percent_to_exploit * root_node->get_n_branches()) + min_exploit_iters;
                const uint32_t n_exploration = ceil(percent_to_explore * root_node->get_n_branches()) + min_explore_iters;
                const uint32_t n_iterations_total = n_exploitation + n_exploration;

                std::cerr << "Decision: " << d << " exploit: " << n_exploitation << " explore: " << n_exploration << " total: " << n_iterations_total << " out of " << root_node->get_n_branches() << "\n";

                // uint32_t n_iterations_per_decision = ceil(percent_to_explore * root_node->get_n_branches()) + min_iterations;
                internal_assert(n_iterations_total != 0) << "accidentally gave 0 iterations: " << root_node->get_n_branches() << "\n";
                // std::cerr << "Decision: " << d << " has " << n_iterations_per_decision << " iterations available, for " << root_node->get_n_branches() << " branches\n";
                std::tie(current_state, root_node) = make_decision(root_node, current_state, n_iterations_total, num_simulations, n_exploitation, rollout_length, n_decisions);
                // Clear the parent of the new root_node
                // TODO: figure out what to do here? Need some sort of back-tracking probably.
                internal_assert(root_node) << "make_decision could not find a decision to make\n";
                root_node->clear_parent(); // delete parent pointer, it's now garbage.
                // n_iterations_per_decision = n_iterations_per_decision / 2 + 1; // make it at least 1
            }

            std::cerr << "Iterations:" << iterations << std::endl;
            std::cerr << "Valids:" << n_valid_nodes << std::endl;
            std::cerr << "Invalids:" << n_invalid_nodes << std::endl;
            // std::cerr << "Explorations:" << n_explores << std::endl;
            // std::cerr << "Exploitations:" << n_exploitations << std::endl;

            return current_state;
        }




        State solve_beam(const State &starter_state, uint32_t n_decisions, int seed = 1) {
            std::mt19937 rng((uint32_t)seed);

            // TODO(rootjalex): replace with std::make_shared
            NodePtr root_node = NodePtr(new Node(starter_state, Action::Default(), /* parent */ nullptr, rng));
            State current_state = starter_state; // track the current best state.

            const double percent_to_explore = get_exploration_percent();
            const double percent_to_exploit = get_exploitation_percent();
            const uint32_t min_explore_iters = get_min_explore();
            const uint32_t min_exploit_iters = get_min_exploit();
            const uint32_t rollout_length = get_rollout_length();

            // TODO: be smarter about decision allocation?
            internal_assert(max_iterations >= n_decisions) << "Must have enough iterations for the number of decisions made\n";
            // const uint32_t n_iterations_per_decision = max_iterations / n_decisions;

            // uint32_t n_iterations_per_decision = max_iterations / 2;
            const uint32_t beam_size = get_beam_size();

            Beam beam(beam_size, {nullptr, current_state});
            uint32_t beam_fill = 1;
            beam[0] = {root_node, current_state};

            auto clear_parent = [](BeamElement &elem) { elem.first->clear_parent(); };

            for (uint32_t d = 0; d < n_decisions; d++) {

              const uint32_t search_depth = do_beam_rollouts(beam, beam_fill, percent_to_explore, percent_to_exploit,
                                                             min_explore_iters, min_exploit_iters, rollout_length);

              fill_beam(beam, beam_fill, beam_size, search_depth, true);

              std::for_each(beam.begin(), beam.begin() + beam_fill, clear_parent);
            }

            std::cerr << "Iterations:" << iterations << std::endl;
            std::cerr << "Valids:" << n_valid_nodes << std::endl;
            std::cerr << "Invalids:" << n_invalid_nodes << std::endl;
            //std::cerr << "Explorations:" << n_explores << std::endl;
            //std::cerr << "Exploitations:" << n_exploitations << std::endl;

            auto beam_element_ordering = [](BeamElement &lhs, BeamElement &rhs) { return lhs.first->get_value() < rhs.first->get_value(); };

            auto max_elem = std::max_element(beam.begin(), beam.begin() + beam_fill, beam_element_ordering);
            auto min_elem = std::min_element(beam.begin(), beam.begin() + beam_fill, beam_element_ordering);

            std::cerr << "Min element: " << min_elem->first->get_value() << "\n";
            std::cerr << "Max element: " << max_elem->first->get_value() << "\n";

            return min_elem->second;
        }

        // Returns maximum depth explored
        uint32_t do_beam_rollouts(Beam &beam, uint32_t beam_size, double percent_explore, double percent_exploit,
                                  uint32_t min_explore, uint32_t min_exploit, uint32_t rollout_length) {
        
            uint32_t max_depth_explored = 0;
            // TODO: this is easily parallelizable...
            for (uint32_t i = 0; i < beam_size; i++) {
                max_depth_explored = std::max(max_depth_explored,
                                              do_rollouts(beam[i].first, beam[i].second,
                                                          percent_explore, percent_exploit,
                                                          min_explore, min_exploit,
                                                          rollout_length));
            }
            return max_depth_explored;
        }

        // Returns maximum depth explored
        uint32_t do_rollouts(const NodePtr root_node, const State &root_state,
                             double percent_explore, double percent_exploit,
                             uint32_t min_explore, uint32_t min_exploit,
                             uint32_t rollout_length) {
            internal_assert(root_node) << "do_rollouts was given a nullptr\n";
            internal_assert(!root_state.is_terminal()) << "do_rollouts was given an end state\n";
            const uint32_t n_branches = root_node->get_n_branches();
            // if (n_branches == 1) {
            if (false) {
                // Fill the action if it hasn't been explored yet.
                root_node->choose_only_random_child();
                // TODO: we might need to do a special case here, or remove this optimization...
                return 0;
            } else {
                // Need to perform rollouts.
                uint32_t search_depth = 0;

                const uint32_t n_exploitation = ceil(percent_exploit * n_branches) + min_exploit;
                const uint32_t n_exploration = ceil(percent_explore * n_branches) + min_explore;
                const uint32_t n_iterations_total = std::min(n_exploitation + n_exploration, n_branches);

                for (uint32_t i = 0; i < n_iterations_total; i++) {
                    NodePtr rollout_node = nullptr;
                    if (i < n_exploitation) {
                        rollout_node = root_node->choose_specific_child(i % n_branches);
                    } else {
                        rollout_node = root_node->choose_any_random_child();
                    }
                    internal_assert(rollout_node) << "do_rollouts selected a nullptr\n";

                    for (uint32_t j = 0; (j < rollout_length) && (!rollout_node->is_leaf()); j++) {
                        // Make weighted random rollouts
                        rollout_node = rollout_node->choose_weighted_random_child();
                        internal_assert(rollout_node) << "simulation returned nullptr\n";
                    }

                    // Propagate visit count up the parent chain.
                    rollout_node->increment_visits();

                    const double node_cost = rollout_node->get_action().get_cost();
                    const uint32_t node_depth = rollout_node->get_depth();

                    search_depth = std::max(search_depth, node_depth);

                    // Back propagation. node_cost is passed by value,
                    // because the policy for backprop is handled via the State class.
                    // e.g. it might make node_cost the minimum of values, or the average, etc.
                    bool continue_updating = rollout_node->update(node_cost, node_depth);

                    if (continue_updating) {
                        // This messy backprop is due to the fact that
                        // node is shared but we don't have shared ptrs
                        // to parent nodes, as that would cause loops.
                        Node *parent_ptr = rollout_node->get_parent();
                        // This order of operations makes sure that the root_node is updated, but nothing past that.
                        while (parent_ptr && parent_ptr->update(node_cost, node_depth) && parent_ptr != root_node.get()) {
                            parent_ptr = parent_ptr->get_parent();
                        }
                    }
                }

                return search_depth;
            }
        }

        void fill_beam(Beam &beam, uint32_t &beam_size, uint32_t max_beam_size, uint32_t search_depth, bool use_search_depth) {
            internal_assert(beam_size > 0) << "fill_beam given an empty beam\n";
            Beam new_beam(max_beam_size, beam[0]);
            uint32_t new_beam_size = 0;

            // TODO: this might not be smart...
            auto max_iterator = new_beam.begin();

            auto beam_element_ordering = [](BeamElement &lhs, BeamElement &rhs) { return lhs.first->get_value() < rhs.first->get_value(); };

            // Iterate through the current beam.
            for (uint32_t b = 0; b < beam_size; b++) {
                NodePtr root_node = beam[b].first;
                State &root_state = beam[b].second;

                const int num_children = root_node->get_num_children();

                // Now iterate through this root's children, select the good ones.
                for (int i = 0; i < num_children; i++) {
                    NodePtr child_ptr = root_node->get_child(i);
                    const double child_value = child_ptr->get_value();
                    const uint32_t child_max_depth = child_ptr->get_state_depth();

                    bool correct_depth = !use_search_depth || (child_max_depth == search_depth);
                    bool obvious_insert = (new_beam_size < max_beam_size);
                    // TODO: how expensive does this get...?
                    max_iterator = max_element(new_beam.begin(), new_beam.begin() + new_beam_size, beam_element_ordering);
                    bool difficult_insert = (child_value < max_iterator->first->get_value());

                    if (correct_depth) {
                        if (obvious_insert) {
                            new_beam[new_beam_size] = {child_ptr, root_state.take_action(child_ptr->get_action())};
                            new_beam_size++;
                        } else if (difficult_insert) {
                            *max_iterator = {child_ptr, root_state.take_action(child_ptr->get_action())};
                        }
                    }
                }
            }

            beam = std::move(new_beam);
            beam_size = new_beam_size;
        }

        std::pair<State, NodePtr> make_decision(const NodePtr root_node, const State &root_state,
                                                const uint32_t n_iterations, const uint32_t n_simulations,
                                                const uint32_t k_best, const uint32_t rollout_length,
                                                const uint32_t n_decisions) {
            internal_assert(!root_state.is_terminal()) << "make_decision was given an end state\n";
            // Only one decision to make, don't waste any time:
            const uint32_t n_branches = root_node->get_n_branches();
            if (n_branches == 1) {
                // This should be the only child.
                NodePtr rollout_node = root_node->choose_only_random_child();
                return {root_state.take_action(rollout_node->get_action()), rollout_node};
            }

            uint32_t search_depth = 0;

            for (uint32_t i = 0; i < n_iterations; i++) {
                // TODO: decide expansion method??
                // NodePtr rollout_node = root_node->choose_any_random_child();
                // NodePtr rollout_node = root_node->choose_weighted_random_child();
                // NodePtr rollout_node = (root_node->is_fully_expanded()) ? get_best_value_child(root_node) : root_node->choose_new_random_child();
                // NodePtr rollout_node = nullptr;
                // if (root_node->is_fully_expanded()) {
                //     rollout_node = get_best_value_child(root_node);
                //     n_exploitations++;
                // } else {
                //     rollout_node = root_node->choose_new_random_child();
                //     n_explores++;
                // }

                NodePtr rollout_node = nullptr;
                if (i < k_best) {
                    rollout_node = root_node->choose_specific_child(i % n_branches);
                } else {
                    rollout_node = root_node->choose_any_random_child();
                }

                if (!rollout_node) {
                    // TODO: this probably should be an error.
                    return {root_state, nullptr};
                }
                // TODO(rootjalex): make this decision a configurable choice.
                for (uint32_t j = 0; (j < rollout_length) && (!rollout_node->is_leaf()); j++) {
                    // Make weighted random rollouts
                    rollout_node = rollout_node->choose_weighted_random_child();
                    internal_assert(rollout_node) << "simulation returned nullptr\n";
                }

                // Propagate visit count up the parent chain.
                rollout_node->increment_visits();

                const double node_cost = rollout_node->get_action().get_cost();
                const uint32_t node_depth = rollout_node->get_depth();

                search_depth = std::max(search_depth, node_depth);

                // Back propagation. node_cost is passed by value,
                // because the policy for backprop is handled via the State class.
                // e.g. it might make node_cost the minimum of values, or the average, etc.
                bool continue_updating = rollout_node->update(node_cost, node_depth);

                if (continue_updating) {
                    // This messy backprop is due to the fact that
                    // node is shared but we don't have shared ptrs
                    // to parent nodes, as that would cause loops.
                    Node *parent_ptr = rollout_node->get_parent();
                    // This order of operations makes sure that the root_node is updated, but nothing past that.
                    while (parent_ptr && parent_ptr->update(node_cost, node_depth) && parent_ptr != root_node.get()) {
                        parent_ptr = parent_ptr->get_parent();
                    }
                }
            }
            // TODO: other methods for choosing the best child?
            // const uint32_t search_depth = std::min(root_node->get_depth() + rollout_length, n_decisions);

            NodePtr best_node = get_min_value_child(root_node, /* search_depth */ search_depth, /* use_search_depth */ true);
            internal_assert(root_node) << "make_decision found nullptr\n";
            // TODO: clear parent of best_node?
            return {root_state.take_action(best_node->get_action()), best_node};
        }

        State choose_best_decisions(const State &starter_state, NodePtr root) {
            if (starter_state.is_terminal()) {
                return starter_state;
            }
            State current_state = starter_state;
            NodePtr current_node = root;
            while (current_node && !current_state.is_terminal()) {
                current_state = current_state.take_action(current_node->get_action());
                if (current_node->is_terminal()) {
                    internal_assert(current_state.is_terminal()) << "Best node has no actions but current_state is not terminal\n";
                    break;
                } else {
                    internal_assert(current_node->get_num_children() != 0) << "Non-terminal state has no children when chosen as best decision.\n";
                    current_node = get_min_value_child(current_node);
                }
            }
            internal_assert(current_node) << "choose_best_decisions ended with a nullptr node.\n";
            internal_assert(current_state.is_terminal()) << "choose_best_decisions ended with a non-terminal state.\n";
            return current_state;
        }

        // After a call to solve(), this function can be used to fetch the best state.
        // State get_optimal_state(const State &starter_state, NodePtr solved_action) {
        //     NodePtr node = solved_action;
        //     State current_state = starter_state;
        //     while (node) {
        //         // std::cerr << "Node value: " << node->get_value() << std::endl;
        //         // std::cerr << "State cost: " << current_state.calculate_cost() << std::endl;
        //         current_state = current_state.take_action(node->get_action());
        //         // TODO(rootjalex): is this the best policy? Should we use UTC?
        //         if (node->is_terminal() || node->get_num_children() == 0) {
        //             break;
        //         } else {
        //             node = get_min_value_child(node);
        //         }
        //     }
        //     internal_assert(node) << "get_optimal_state ended with a nullptr node\n";
        //     return current_state;
        // }

        // Find the best UTC score of the children that have already been generated.
        NodePtr get_best_value_child(NodePtr parent_node) const {
            // TODO(rootjalex): should we check if parent_node is fully expanded?

            double best_uct_score = std::numeric_limits<double>::min();
            NodePtr best_node = nullptr;
            
            const int num_children = parent_node->get_num_children();

            internal_assert(num_children != 0) << "get_best_value_child called on a node with 0 children.\n";

            // To see formula, go to: "Exploration and exploitation"
            // at: https://en.wikipedia.org/wiki/Monte_Carlo_tree_search#cite_note-Kocsis-Szepesvari-16

            // TODO(rootjalex): Might need to change this to stop reducing get_value() by visits, in case
            //                  num_value() represents minimum possible value...

            for (int i = 0; i < num_children; i++) {
                NodePtr child_ptr = parent_node->get_child(i);

                double uct_score = 0.5f; // TODO(rootjalex): better default value??

                // TODO(rootjalex): what do we do for `child_ptr->get_num_visits() == 0`??

                if (child_ptr->get_num_visits() != 0) {
                    const double nonzero_num_visits = child_ptr->get_num_visits();

                    // This is the UCT formula.
                    // We let the State handle value calculation, as it might be a minimum or it might be an average.
                    const double uct_exploitation = child_ptr->get_exploitation_value();
                    // TODO(rootjalex): Why is this N + 1?
                    const double uct_exploration = std::sqrt(std::log((double)parent_node->get_num_visits() + 1) / nonzero_num_visits);

                    uct_score = uct_exploitation + uct_k * uct_exploration;

                    // std::cerr << "\tExploitation: " << uct_exploitation << std::endl;
                    // std::cerr << "\tExploration: " << uct_exploration << std::endl;
                }

                // std::cerr << "\tScore of: " << uct_score << std::endl;
                if (!best_node || uct_score > best_uct_score) {
                    best_uct_score = uct_score;
                    best_node = child_ptr;
                }
            }

            // std::cerr << "\tBest node has score of: " << best_uct_score << std::endl;
            // std::cerr << "Num children: " << num_children << std::endl;
            internal_assert(best_node) << "get_best_value_child ended with a nullptr node\n" << "\tWith: " << num_children << " children.\n";

            return best_node;
        }

        /*
        // Find the most visited of the children that have already been generated.
        NodePtr get_most_visited_child(NodePtr parent_node) const {
            uint32_t most_visits = 0;
            NodePtr popular_node = nullptr;

            const int num_children = parent_node->get_num_children();

            for (int i = 0; i < num_children; i++) {
                NodePtr child_ptr = parent_node->get_child(i);
                const uint32_t num_visits = child_ptr->get_num_visits();

                // MUST be >= not just >, in the case that no child has been visited yet.
                if (!popular_node || num_visits >= most_visits) {
                    most_visits = num_visits;
                    popular_node = child_ptr;
                }
            }

            internal_assert(popular_node) << "get_most_visited_child ended with a nullptr node\n";
            return popular_node;
        }
        */

        // Find the child with the minimum value.
        NodePtr get_min_value_child(NodePtr parent_node, uint32_t search_depth = 0, bool use_search_depth = false) const {
            double best_value = std::numeric_limits<double>::max();
            NodePtr best_node = nullptr;

            const int num_children = parent_node->get_num_children();

            for (int i = 0; i < num_children; i++) {
                NodePtr child_ptr = parent_node->get_child(i);
                const double child_value = child_ptr->get_value();
                const uint32_t child_max_depth = child_ptr->get_state_depth();

                // bool lower_depth = (child_max_depth >= search_depth) && (child_max_depth >= best_depth);
                // Depth is correct
                bool correct_depth = !use_search_depth || (child_max_depth == search_depth);
                bool lower_cost = !best_node || (child_value < best_value);

                if (correct_depth && lower_cost) {
                    best_value = child_value;
                    best_node = child_ptr;
                }

                // std::cerr << "\t\tChild with cost:" << child_value << std::endl;
            }

            if (!best_node) {
                std::cerr << "Failed to find best child node, with " << num_children << std::endl;
                std::cerr << "Params: " << search_depth << " " << use_search_depth << std::endl;

                for (int i = 0; i < num_children; i++) {
                    NodePtr child_ptr = parent_node->get_child(i);
                    const double child_value = child_ptr->get_value();
                    const uint32_t child_max_depth = child_ptr->get_state_depth();

                    std::cerr << "Child(" << i << ") = [value = " << child_value << ", depth = " << child_max_depth << "]\n";
                }
            }

            internal_assert(best_node) << "get_min_value_child ended with a nullptr node\n";
            // TODO(rootjalex): assert(best_node);

            // std::cerr << "\t\tBest has cost:" << best_value << std::endl;

            return best_node;
        }
    };

} // namespace MCTS

#endif // HL_MCTS_H
