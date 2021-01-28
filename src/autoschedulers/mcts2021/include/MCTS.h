#ifndef HL_MCTS_H
#define HL_MCTS_H

// TODO(rootjalex): figure out includes.
//                  should this be in the Halide namespace??
#include "MCTreeNode.h"
#include <cmath>        // std::sqrt
#include <cstdint>      // uint32_t
#include <iostream>     // std::cerr
#include <limits>       // std::numeric_limits
#include <memory>       // std::shared_ptr
#include <utility>      // std::pair
#include <vector>


/*

Templated Monte Carlo Tree Search implementation, designed for state exploration.

TODO(rootjalex): add more details
*/


namespace MCTS {

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

    private:
        // If true, uses timer (not yet implemented), otherwise uses iteration count.
        bool use_timer = false;

        // Private to ensure that the static methods defined below are used.
        Solver() = default;
    public:
        // TODO(rootjalex): implement this.
        // static MakeTimerSolver(uint32_t )
        static Solver<State, Action> MakeIterationSolver(uint32_t max_iterations) {
            Solver<State, Action> solver;
            solver.max_iterations = max_iterations;
            solver.use_timer = false;
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

        // Node corresponds to best action to take immediately from this state,
        // and includes enough information to iteratively apply it's decisions.
        std::shared_ptr<Node> solve(const State &current_state, int seed = 1) {
            std::mt19937 rng((uint32_t)seed);
            Node root_node(current_state, Action::Default(), /* parent */ nullptr, rng);

            std::shared_ptr<Node> best_node = nullptr;

            iterations = 0;

            // TODO(rootjalex): set up timer set up

            // Breaks if no more states to explore, or time or iterations are up.
            while (true) {

                // Start at root and find best valued node that has been expanded.
                Node *node = &root_node;
                // TODO(rootjalex): is there a faster way to track from the root?
                //                  this could be expensive for large Pipelines
                while (!node->is_terminal() && node->is_fully_expanded()) {
                    node = get_best_value_child(node);
                    // TODO(rootjalex): assert(node);
                }

                // Node is either a terminal, or has more actions that can be tried.
                // Expand if it has more actions to be taken.
                if (!node->is_fully_expanded()) {
                    // TODO(rootjalex): this needs to be expanded if we want to go all the way to leaves.
                    node = node->choose_new_random_child();
                }

                // We don't have a simulation step, because only one action per state can be chosen.

                double node_cost = node->get_state().caclulate_cost();

                // Back propagation. node_cost is passed by value,
                // because the policy for backprop is handled via the State class.
                // e.g. it might make node_cost the minimum of values, or the average, etc.
                while (node && node->update(node_cost)) {
                    node = node->get_parent();
                }

                // TODO(rootjalex): reference code uses get_most_visited_child of the root. Why the heck?

                best_node = get_min_value_child(&root_node);

                // TODO(rootjalex): check timing here

                if (!use_timer && iterations >= max_iterations) {
                    break;
                }

                iterations++;
            }

            // TODO(rootjalex): assert(best_node)

            return best_node;
        }

        void print() {
            std::cerr << "print solver" << std::endl;
        }

        // Find the best UTC score of the children that have already been generated.
        std::shared_ptr<Node> get_best_value_child(Node *parent_node) const {
            // TODO(rootjalex): should we check if parent_node is fully expanded?

            double best_uct_score = std::numeric_limits<double>::min();
            std::shared_ptr<Node> best_node = nullptr;
            
            const int num_children = parent_node->get_num_children();

            // To see formula, go to: "Exploration and exploitation"
            // at: https://en.wikipedia.org/wiki/Monte_Carlo_tree_search#cite_note-Kocsis-Szepesvari-16

            // TODO(rootjalex): Might need to change this to stop reducing get_value() by visits, in case
            //                  num_value() represents minimum possible value...

            for (int i = 0; i < num_children; i++) {
                std::shared_ptr<Node> child_ptr = parent_node->get_child(i);
                const double nonzero_num_visits = child_ptr->get_num_visits() + std::numeric_limits<double>::epsilon();
                
                // This is the UCT formula.
                // We let the State handle value calculation, as it might be a minimum or it might be an average.
                const double uct_exploitation = child_ptr->get_exploitation_value();
                // TODO(rootjalex): Why is this N + 1?
                const double uct_exploration = std::sqrt(std::log((double)parent_node->get_num_visits() + 1) / nonzero_num_visits);

                const double uct_score = uct_exploitation + uct_k * uct_exploration;

                if (uct_score > best_uct_score) {
                    best_uct_score = uct_score;
                    best_node = child_ptr;
                }
            }

            // TODO(rootjalex): assert(best_node);

            return best_node;
        }

        // Find the most visited of the children that have already been generated.
        std::shared_ptr<Node> get_most_visited_child(Node *parent_node) const {
            uint32_t most_visits = 0;
            std::shared_ptr<Node> popular_node = nullptr;

            const int num_children = parent_node->get_num_children();

            for (int i = 0; i < num_children; i++) {
                std::shared_ptr<Node> child_ptr = parent_node->get_child(i);
                const uint32_t num_visits = child_ptr->get_num_visits();

                // MUST be >= not just >, in the case that no child has been visited yet.
                if (num_visits >= most_visits) {
                    most_visits = num_visits;
                    popular_node = child_ptr;
                }
            }

            // TODO(rootjalex): assert(popular_node);

            return popular_node;
        }

        // Find the child with the minimum value.
        std::shared_ptr<Node> get_min_value_child(Node *parent_node) const {
            double best_value = std::numeric_limits<double>::max();
            std::shared_ptr<Node> best_node = nullptr;

            const int num_children = parent_node->get_num_children();

            for (int i = 0; i < num_children; i++) {
                std::shared_ptr<Node> child_ptr = parent_node->get_child(i);
                const uint32_t child_value = child_ptr->get_value();

                if (child_value < best_value) {
                    best_value = child_value;
                    best_node = child_ptr;
                }
            }

            // TODO(rootjalex): assert(best_node);

            return best_node;
        }
    };

} // namespace MCTS

#endif // HL_MCTS_H
