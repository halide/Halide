

A very simple C++11 Templated MCTS (Monte Carlo Tree Search) implementation with examples for openFrameworks. 

MCTS Code Based on the Java (Simon Lucas - University of Essex) and Python (Peter Cowling, Ed Powley, Daniel Whitehouse - University of York) impelementations at http://mcts.ai/code/index.html

The code is not tailored for any specific use case. It's very generic (but perhaps not generic enough?)
It's probably not very optimized, my priority was on readability and flexibility (I wrote it to understand MCTS).
It supports variable number of agents, and variable number of actions per step, and is templated. See examples for how to design your 'State' and 'Action' classes.

# Usage
        State state;            // contains the current state, it must comply with the State interface
        Action action;          // contains an action that can be applied to a State, and bring it to a new State
        UCT<State, Action> uct; // Templated class. Builds a partial decision tree and searches it with UCT MCTS
        
        // OPTIONAL init uct params
        uct.uct_k = sqrt(2);
        uct.max_millis = 0;
        uct.max_iterations = 100;
        uct.simulation_depth = 5;
        
        loop {
        	// run uct mcts on current state and get best action
        	action = uct.run(state);
        
        	// apply the action to the current state
        	state.apply_action(action);
        }


# Dependencies:
## Library itself
None (C++11)

## Examples
openFrameworks 0.9.0

should work on any platform, but project files currently exist only for Windows / VS2015
