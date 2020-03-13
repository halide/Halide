#pragma once

/*
 Interface that the State class needs to comply with.
 It doesn't need to extend this class, in fact, you don't need to include this file
 This is just here for reference.
 'Action' is your custom class containing action information
 See examples for usage
*/

#include <vector>
#include <string>

namespace msa {
namespace mcts {
    enum class ActionEnum {
        Inline,
        Retile,
        Option,
        Parallelize
    };
    struct Action {
    public:
        ActionEnum ae;
        unsigned index;
        unsigned option_var;
        Action(ActionEnum ae_, unsigned index_) : ae(ae_), index(index_) {}
        Action(ActionEnum ae_, unsigned index_, unsigned option_var_) : ae(ae_), index(index_), option_var(option_var_) {}
        bool operator==(const Action& a) const {
            return ae == a.ae && index == a.index && option_var == a.option_var;
        }
    };


//struct Action {

//};

class State {

    // copy and assignment operators should perform a DEEP clone of the given state
    State(const State& other);
    State& operator = (const State& other);

    // whether or not this state is terminal (reached end)
    bool is_terminal() const;

    //  agent id (zero-based) for agent who is about to make a decision
    int agent_id() const;

    // apply action to state
    void apply_action(const Action& action);

    // return possible actions from this state
    void get_actions(std::vector<Action>& actions) const;

    // get a random action, return false if no actions found
    bool get_random_action(Action& action) const;

    // evaluate this state and return a vector of rewards (for each agent)
    const std::vector<float> evaluate() const;

    // return state as string (for debug purposes)
    std::string to_string() const;

};

}
}
