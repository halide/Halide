#ifndef SCOPE_H
#define SCOPE_H

#include <string>
#include <map>
#include <stack>

namespace Halide { namespace Internal {

    using std::string;
    using std::map;
    using std::stack;

    /* A common pattern when traversing Halide IR is that you need to
     * keep track of stuff when you find a Let or a LetStmt. This
     * class helps with that. */
    template<typename T>
    class Scope {
    private:
        map<string, stack<T> > table;
    public:
        /* Retrive the value referred to by a name */
        T get(const string &name) const {
            typename map<string, stack<T> >::const_iterator iter = table.find(name);
            assert(iter != table.end() && "Symbol not found");
            return iter->second.top();
        }
        
        /* Return a reference to an entry */
        T &ref(const string &name) {
            typename map<string, stack<T> >::iterator iter = table.find(name);
            assert(iter != table.end() && "Symbol not found");
            return iter->second.top();            
        }

        /* Tests if a name is in scope */
        bool contains(const string &name) const {
            typename map<string, stack<T> >::const_iterator iter = table.find(name);
            return iter != table.end() && !iter->second.empty();
        }

        /* Add a new (name, value) pair to the current scope. Hide old
         * values that have this name until we pop this name.
         */
        void push(const string &name, T value) {
            table[name].push(value);
        }

        /* A name goes out of scope. Restore whatever its old value
         * was (or remove it entirely if there was nothing else of the
         * same name in an outer scope) */
        void pop(const string &name) {
            assert(!table[name].empty() && "Name not in symbol table");
            table[name].pop();
        }
    };
    
}}

#endif
