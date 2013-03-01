#ifndef HALIDE_SCOPE_H
#define HALIDE_SCOPE_H

#include <string>
#include <map>
#include <stack>
#include <utility>
#include <iostream>

#include "Util.h"

/** \file
 * Defines the Scope class, which is used for keeping track of names in a scope while traversing IR 
 */

namespace Halide { 
namespace Internal {

/** A common pattern when traversing Halide IR is that you need to
 * keep track of stuff when you find a Let or a LetStmt, and that it
 * should hide previous values with the same name until you leave the Let or LetStmt nodes
 * This class helps with that. */
template<typename T>
class Scope {
private:
    int count;
    std::map<std::string, std::stack<std::pair<T, int> > > table;
public:
    Scope() : count(0) {}

    /** Raw read-only access to the scope table. */
    const std::map<std::string, std::stack<std::pair<T, int> > > &get_table() {return table;}

    /** Retrive the value referred to by a name */
    T get(const std::string &name) const {
        typename std::map<std::string, std::stack<std::pair<T, int> > >::const_iterator iter = table.find(name);
        if (iter == table.end() || iter->second.empty()) {
            std::cerr << "Symbol '" << name << "' not found" << std::endl;
            assert(false);
        }
        return iter->second.top().first;
    }
        
    /** Return a reference to an entry */
    T &ref(const std::string &name) {
        typename std::map<std::string, std::stack<std::pair<T, int> > >::iterator iter = table.find(name);
        if (iter == table.end() || iter->second.empty()) {
            std::cerr << "Symbol '" << name << "' not found" << std::endl;
            assert(false);
        }
        return iter->second.top().first;
    }

    /** Get the depth of an entry. The depth of A is less than the
     * depth of B if A was pushed before B. */
    int depth(const std::string &name) {
        typename std::map<std::string, std::stack<std::pair<T, int> > >::const_iterator iter = table.find(name);
        if (iter == table.end() || iter->second.empty()) {
            std::cerr << "Symbol '" << name << "' not found" << std::endl;
            assert(false);
        }
        return iter->second.top().second;
    }

    /** Tests if a name is in scope */
    bool contains(const std::string &name) const {
        typename std::map<std::string, std::stack<std::pair<T, int> > >::const_iterator iter = table.find(name);
        return iter != table.end() && !iter->second.empty();
    }

    /** Add a new (name, value) pair to the current scope. Hide old
     * values that have this name until we pop this name.
     */
    void push(const std::string &name, T value) {
        table[name].push(std::make_pair(value, count));
        count++;
    }

    /** A name goes out of scope. Restore whatever its old value
     * was (or remove it entirely if there was nothing else of the
     * same name in an outer scope) */
    void pop(const std::string &name) {
        assert(!table[name].empty() && "Name not in symbol table");
        table[name].pop();
        count--;
    }
};
    
}
}

#endif
