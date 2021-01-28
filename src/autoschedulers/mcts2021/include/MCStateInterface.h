#ifndef HL_MC_STATE_INTERFACE_H
#define HL_MC_STATE_INTERFACE_H

/*

THIS SHOULD NOT BE USED IN ANY WAY. IT IS AN INTERFACE DESCRIPTION ONLY.
 - rootjalex
*/

#include <vector>

namespace MCTS {

    template<class Action>
    class StateInterface {
    public:
        StateInterface(const StateInterface &state);
        StateInterface &operator=(const StateInterface &state);

        // Generate all possible actions to be taken from this state.
        std::vector<Action> generate_possible_actions() const;

        // Generate the state produced by taking this Action from this state.
        StateInterface take_action(const Action &action) const;

        // Gets the value of this State.
        double get_value() const;

        // Is this a leaf node.
        bool is_terminal() const;

        // Used for evaluation / value that is propagated backwards.
        double calculate_cost();

        // Update the value of this state. Passed by
        // reference in case it needs to be updated.
        bool update(double &cost_value);

        // Return a stored or calculated exploration value.
        // This might be the minimum cost found so far, or
        // the average cost of child nodes that have been explored.
        double get_exploitation_value(uint32_t num_visits);

    };

} // namespace MCTS

#endif // HL_MC_STATE_INTERFACE_H
