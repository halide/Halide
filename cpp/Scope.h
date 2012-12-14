#ifndef SCOPE_H
#define SCOPE_H

#include <assert.h>
#include <string>
#include <map>
#include <stack>
#include <utility>
#include <iostream>

namespace Halide { 
namespace Internal {

using std::string;
using std::map;
using std::stack;
using std::pair;
using std::make_pair;

/* A common pattern when traversing Halide IR is that you need to
 * keep track of stuff when you find a Let or a LetStmt. This
 * class helps with that. */
template<typename T>
class Scope {
private:
    int count;
    map<string, stack<pair<T, int> > > table;
public:
    Scope() : count(0) {}

    /* Retrive the value referred to by a name */
    T get(const string &name) const {
        typename map<string, stack<pair<T, int> > >::const_iterator iter = table.find(name);
        if (iter == table.end()) {
            std::cerr << "Symbol '" << name << "' not found" << std::endl;
            assert(false);
        }
        return iter->second.top().first;
    }
        
    /* Return a reference to an entry */
    T &ref(const string &name) {
        typename map<string, stack<pair<T, int> > >::iterator iter = table.find(name);
        if (iter == table.end()) {
            std::cerr << "Symbol '" << name << "' not found" << std::endl;
            assert(false);
        }
        return iter->second.top().first;
    }

    /* Get the depth of an entry. The depth of A is less than the
     * depth of B if A was pushed before B. */
    int depth(const string &name) {
        typename map<string, stack<pair<T, int> > >::const_iterator iter = table.find(name);
        if (iter == table.end()) {
            std::cerr << "Symbol '" << name << "' not found" << std::endl;
            assert(false);
        }
        return iter->second.top().second;
    }

    /* Tests if a name is in scope */
    bool contains(const string &name) const {
        typename map<string, stack<pair<T, int> > >::const_iterator iter = table.find(name);
        return iter != table.end() && !iter->second.empty();
    }

    /* Add a new (name, value) pair to the current scope. Hide old
     * values that have this name until we pop this name.
     */
    void push(const string &name, T value) {
        table[name].push(make_pair(value, count));
        count++;
    }

    /* A name goes out of scope. Restore whatever its old value
     * was (or remove it entirely if there was nothing else of the
     * same name in an outer scope) */
    void pop(const string &name) {
        assert(!table[name].empty() && "Name not in symbol table");
        table[name].pop();
        count--;
    }
};
    
}
}

#endif
