#ifndef HL_COST_PRINTER_H
#define HL_COST_PRINTER_H

// TODO(rootjalex): figure out includes.
//                  should this be in the Halide namespace??
#include "Halide.h"
#include "MCTreeNode.h"
#include <cmath>        // std::sqrt
#include <cstdint>      // uint32_t
#include <iostream>     // std::cerr
#include <limits>       // std::numeric_limits
#include <memory>       // std::shared_ptr
#include <utility>      // std::pair
#include <vector>
#include <map>

/*

Templated Monte Carlo Tree Search implementation, designed for state exploration.

TODO(rootjalex): add more details
*/


namespace MCTS {

    // State must comply with interface in MCStateInterface.h
    // TODO(rootjalex): Should we do the Action template? needs default (empty) action.
    template<class State, class Action>
    class CostPrinter {
        // The nodes of this search tree - Action is edge that leads to node.
        typedef TreeNode<State, Action> Node;

        typedef std::shared_ptr<Node> NodePtr;

        std::map<uint32_t, size_t> depth_map;

        void print_node(NodePtr root, size_t parent, size_t loc) {
            uint32_t depth = root->get_depth();
            size_t rel_loc = depth_map[depth]++;
            std::cerr << "\t(" << root->get_depth() << ", " << rel_loc << ", " << parent << ", " << loc << ", " << root->get_state().calculate_cost() << "),\n";
            for (size_t i = 0; i < root->possible_actions.size(); i++) {
                const Action &action = root->possible_actions[i];
                NodePtr child_ptr = root->add_child_with_action(action);
                print_node(child_ptr, rel_loc, i);
            }
        }

    public:
        CostPrinter() = default;

        // Node corresponds to best action to take immediately from this state,
        // and includes enough information to iteratively apply it's decisions.
        void print(const State &current_state, int seed = 1) {
            std::mt19937 rng((uint32_t)seed);

            // TODO(rootjalex): replace with std::make_shared
            NodePtr root_node = NodePtr(new Node(current_state, Action::Default(), /* parent */ nullptr, rng));

            std::cerr << "[";
            print_node(root_node, 0, 0);
            std::cerr << "]" << std::endl;
        }
    };

} // namespace MCTS

#endif // HL_COST_PRINTER_H
