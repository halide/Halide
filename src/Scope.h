#ifndef HALIDE_SCOPE_H
#define HALIDE_SCOPE_H

#include <string>
#include <map>
#include <stack>
#include <utility>
#include <iostream>

#include "Util.h"
#include "Debug.h"

/** \file
 * Defines the Scope class, which is used for keeping track of names in a scope while traversing IR
 */

namespace Halide {
namespace Internal {

/** A stack which can store one item very efficiently. Using this
 * instead of std::stack speeds up Scope substantially. */
template<typename T>
class SmallStack {
private:
    T _top;
    SmallStack<T> *_rest;
    bool _empty;

public:
    SmallStack() : _rest(NULL),
                   _empty(true) {}

    ~SmallStack() {
        if (_rest != NULL) {
            assert(!_empty);
            delete _rest;
            _rest = NULL;
        }
    }

    SmallStack(const SmallStack<T> &other) : _top(other._top),
                                             _rest(NULL),
                                             _empty(other._empty) {
        if (other._rest != NULL) {
            _rest = new SmallStack<T>(*other._rest);
        }
    }

    SmallStack<T> &operator=(const SmallStack<T> &other) {
        if (this == &other) return *this;
        _top = other._top;
        if (_rest) {
            delete _rest;
        }
        _rest = other._rest;
        _empty = other._empty;
        return *this;
    }


    void pop() {
        assert(!_empty);
        if (_rest != NULL) {
            _top = _rest->_top;
            SmallStack<T> *new_rest = _rest->_rest;
            _rest->_rest = NULL;
            delete _rest;
            _rest = new_rest;
        } else {
            _empty = true;
        }
    }

    void push(const T &t) {
        if (_empty) {
            _empty = false;
            _top = t;
        } else {
            SmallStack<T> *new_rest = new SmallStack<T>();
            new_rest->_rest = _rest;
            new_rest->_top = _top;
            new_rest->_empty = _empty;
            _top = t;
            _rest = new_rest;
        }
    }

    T top() const {
        assert(!_empty);
        return _top;
    }

    T &top_ref() {
        assert(!_empty);
        return _top;
    }

    bool empty() const {
        return _empty;
    }
};

/** A common pattern when traversing Halide IR is that you need to
 * keep track of stuff when you find a Let or a LetStmt, and that it
 * should hide previous values with the same name until you leave the
 * Let or LetStmt nodes This class helps with that. */
template<typename T>
class Scope {
private:
    std::map<std::string, SmallStack<T> > table;
public:
    Scope() {}

    /** Retrive the value referred to by a name */
    T get(const std::string &name) const {
        typename std::map<std::string, SmallStack<T> >::const_iterator iter = table.find(name);
        if (iter == table.end() || iter->second.empty()) {
            std::cerr << "Symbol '" << name << "' not found" << std::endl;
            assert(false);
        }
        return iter->second.top();
    }

    /** Return a reference to an entry */
    T &ref(const std::string &name) {
        typename std::map<std::string, SmallStack<T> >::iterator iter = table.find(name);
        if (iter == table.end() || iter->second.empty()) {
            std::cerr << "Symbol '" << name << "' not found" << std::endl;
            assert(false);
        }
        return iter->second.top_ref();
    }

    /** Tests if a name is in scope */
    bool contains(const std::string &name) const {
        typename std::map<std::string, SmallStack<T> >::const_iterator iter = table.find(name);
        return iter != table.end() && !iter->second.empty();
    }

    /** Add a new (name, value) pair to the current scope. Hide old
     * values that have this name until we pop this name.
     */
    void push(const std::string &name, const T &value) {
        table[name].push(value);
    }

    /** A name goes out of scope. Restore whatever its old value
     * was (or remove it entirely if there was nothing else of the
     * same name in an outer scope) */
    void pop(const std::string &name) {
        typename std::map<std::string, SmallStack<T> >::iterator iter = table.find(name);
        assert(iter != table.end() && "Name not in symbol table");
        iter->second.pop();
        if (iter->second.empty()) {
            table.erase(iter);
        }
    }

    /** Iterate through the scope. */
    class const_iterator {
        typename std::map<std::string, SmallStack<T> >::const_iterator iter;
    public:
        explicit const_iterator(const typename std::map<std::string, SmallStack<T> >::const_iterator &i) :
            iter(i) {
        }

        const_iterator() {}

        bool operator!=(const const_iterator &other) {
            return iter != other.iter;
        }

        void operator++() {
            ++iter;
        }

        const std::string &name() {
            return iter->first;
        }

        const SmallStack<T> &stack() {
            return iter->second;
        }

        const T &value() {
            return iter->second.top();
        }
    };

    const_iterator cbegin() const {
        return const_iterator(table.begin());
    }

    const_iterator cend() const {
        return const_iterator(table.end());
    }

    class iterator {
        typename std::map<std::string, SmallStack<T> >::iterator iter;
    public:
        explicit iterator(typename std::map<std::string, SmallStack<T> >::iterator i) :
            iter(i) {
        }

        iterator() {}

        bool operator!=(const iterator &other) {
            return iter != other.iter;
        }

        void operator++() {
            ++iter;
        }

        const std::string &name() {
            return iter->first;
        }

        SmallStack<T> &stack() {
            return iter->second;
        }

        T &value() {
            return iter->second.top_ref();
        }
    };

    iterator begin() {
        return iterator(table.begin());
    }

    iterator end() {
        return iterator(table.end());
    }
};

template<typename T>
std::ostream &operator<<(std::ostream &stream, Scope<T>& s) {
    stream << "{\n";
    typename Scope<T>::const_iterator iter;
    for (iter = s.cbegin(); iter != s.cend(); ++iter) {
        stream << "  " << iter.name() << "\n";
    }
    stream << "}";
    return stream;
}

}
}

#endif
