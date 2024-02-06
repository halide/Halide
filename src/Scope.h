#ifndef HALIDE_SCOPE_H
#define HALIDE_SCOPE_H

#include <iostream>
#include <map>
#include <stack>
#include <string>
#include <utility>
#include <vector>

#include "Debug.h"
#include "Error.h"

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
    std::vector<T> _rest;
    bool _empty = true;

public:
    SmallStack() = default;

    void pop() {
        if (_rest.empty()) {
            _empty = true;
            _top = T();
        } else {
            _top = std::move(_rest.back());
            _rest.pop_back();
        }
    }

    void push(T t) {
        if (!_empty) {
            _rest.push_back(std::move(_top));
        }
        _top = std::move(t);
        _empty = false;
    }

    T top() const {
        return _top;
    }

    T &top_ref() {
        return _top;
    }

    const T &top_ref() const {
        return _top;
    }

    bool empty() const {
        return _empty;
    }

    size_t size() const {
        return _empty ? 0 : (_rest.size() + 1);
    }
};

template<>
class SmallStack<void> {
    // A stack of voids. Voids are all the same, so just record how many voids are in the stack
    int counter = 0;

public:
    void pop() {
        counter--;
    }
    void push() {
        counter++;
    }
    bool empty() const {
        return counter == 0;
    }
};

/** A common pattern when traversing Halide IR is that you need to
 * keep track of stuff when you find a Let or a LetStmt, and that it
 * should hide previous values with the same name until you leave the
 * Let or LetStmt nodes This class helps with that. */
template<typename T = void>
class Scope {
private:
    std::map<std::string, SmallStack<T>> table;

    const Scope<T> *containing_scope = nullptr;

public:
    Scope() = default;
    Scope(Scope &&that) noexcept = default;
    Scope &operator=(Scope &&that) noexcept = default;

    // Copying a scope object copies a large table full of strings and
    // stacks. Bad idea.
    Scope(const Scope<T> &) = delete;
    Scope<T> &operator=(const Scope<T> &) = delete;

    /** Set the parent scope. If lookups fail in this scope, they
     * check the containing scope before returning an error. Caller is
     * responsible for managing the memory of the containing scope. */
    void set_containing_scope(const Scope<T> *s) {
        containing_scope = s;
    }

    /** A const ref to an empty scope. Useful for default function
     * arguments, which would otherwise require a copy constructor
     * (with llvm in c++98 mode) */
    static const Scope<T> &empty_scope() {
        static Scope<T> _empty_scope;
        return _empty_scope;
    }

    /** Retrieve the value referred to by a name */
    template<typename T2 = T,
             typename = typename std::enable_if<!std::is_same<T2, void>::value>::type>
    T2 get(const std::string &name) const {
        typename std::map<std::string, SmallStack<T>>::const_iterator iter = table.find(name);
        if (iter == table.end() || iter->second.empty()) {
            if (containing_scope) {
                return containing_scope->get(name);
            } else {
                internal_error << "Name not in Scope: " << name << "\n"
                               << *this << "\n";
            }
        }
        return iter->second.top();
    }

    /** Return a reference to an entry. Does not consider the containing scope. */
    template<typename T2 = T,
             typename = typename std::enable_if<!std::is_same<T2, void>::value>::type>
    T2 &ref(const std::string &name) {
        typename std::map<std::string, SmallStack<T>>::iterator iter = table.find(name);
        if (iter == table.end() || iter->second.empty()) {
            internal_error << "Name not in Scope: " << name << "\n"
                           << *this << "\n";
        }
        return iter->second.top_ref();
    }

    /** Tests if a name is in scope */
    bool contains(const std::string &name) const {
        typename std::map<std::string, SmallStack<T>>::const_iterator iter = table.find(name);
        if (iter == table.end() || iter->second.empty()) {
            if (containing_scope) {
                return containing_scope->contains(name);
            } else {
                return false;
            }
        }
        return true;
    }

    /** How many nested definitions of a single name exist? */
    size_t count(const std::string &name) const {
        auto it = table.find(name);
        if (it == table.end()) {
            return 0;
        } else {
            return it->second.size();
        }
    }

    /** Add a new (name, value) pair to the current scope. Hide old
     * values that have this name until we pop this name.
     */
    template<typename T2 = T,
             typename = typename std::enable_if<!std::is_same<T2, void>::value>::type>
    void push(const std::string &name, T2 &&value) {
        table[name].push(std::forward<T2>(value));
    }

    template<typename T2 = T,
             typename = typename std::enable_if<std::is_same<T2, void>::value>::type>
    void push(const std::string &name) {
        table[name].push();
    }

    /** A name goes out of scope. Restore whatever its old value
     * was (or remove it entirely if there was nothing else of the
     * same name in an outer scope) */
    void pop(const std::string &name) {
        typename std::map<std::string, SmallStack<T>>::iterator iter = table.find(name);
        internal_assert(iter != table.end()) << "Name not in Scope: " << name << "\n"
                                             << *this << "\n";
        iter->second.pop();
        if (iter->second.empty()) {
            table.erase(iter);
        }
    }

    /** Iterate through the scope. Does not capture any containing scope. */
    class const_iterator {
        typename std::map<std::string, SmallStack<T>>::const_iterator iter;

    public:
        explicit const_iterator(const typename std::map<std::string, SmallStack<T>>::const_iterator &i)
            : iter(i) {
        }

        const_iterator() = default;

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

        template<typename T2 = T,
                 typename = typename std::enable_if<!std::is_same<T2, void>::value>::type>
        const T2 &value() {
            return iter->second.top_ref();
        }
    };

    const_iterator cbegin() const {
        return const_iterator(table.begin());
    }

    const_iterator cend() const {
        return const_iterator(table.end());
    }

    void swap(Scope<T> &other) noexcept {
        table.swap(other.table);
        std::swap(containing_scope, other.containing_scope);
    }
};

template<typename T>
std::ostream &operator<<(std::ostream &stream, const Scope<T> &s) {
    stream << "{\n";
    typename Scope<T>::const_iterator iter;
    for (iter = s.cbegin(); iter != s.cend(); ++iter) {
        stream << "  " << iter.name() << "\n";
    }
    stream << "}";
    return stream;
}

/** Helper class for pushing/popping Scope<> values, to allow
 * for early-exit in Visitor/Mutators that preserves correctness.
 * Note that this name can be a bit confusing, since there are two "scopes"
 * involved here:
 * - the Scope object itself
 * - the lifetime of this helper object
 * The "Scoped" in this class name refers to the latter, as it temporarily binds
 * a name within the scope of this helper's lifetime. */
template<typename T = void>
struct ScopedBinding {
    Scope<T> *scope = nullptr;
    std::string name;

    ScopedBinding() = default;

    ScopedBinding(Scope<T> &s, const std::string &n, T value)
        : scope(&s), name(n) {
        scope->push(name, std::move(value));
    }

    ScopedBinding(bool condition, Scope<T> &s, const std::string &n, const T &value)
        : scope(condition ? &s : nullptr), name(n) {
        if (condition) {
            scope->push(name, value);
        }
    }

    bool bound() const {
        return scope != nullptr;
    }

    ~ScopedBinding() {
        if (scope) {
            scope->pop(name);
        }
    }

    // allow move but not copy
    ScopedBinding(const ScopedBinding &that) = delete;
    ScopedBinding(ScopedBinding &&that) noexcept
        : scope(that.scope),
          name(std::move(that.name)) {
        // The move constructor must null out scope, so we don't try to pop it
        that.scope = nullptr;
    }

    void operator=(const ScopedBinding &that) = delete;
    void operator=(ScopedBinding &&that) = delete;
};

template<>
struct ScopedBinding<void> {
    Scope<> *scope;
    std::string name;
    ScopedBinding(Scope<> &s, const std::string &n)
        : scope(&s), name(n) {
        scope->push(name);
    }
    ScopedBinding(bool condition, Scope<> &s, const std::string &n)
        : scope(condition ? &s : nullptr), name(n) {
        if (condition) {
            scope->push(name);
        }
    }
    ~ScopedBinding() {
        if (scope) {
            scope->pop(name);
        }
    }

    // allow move but not copy
    ScopedBinding(const ScopedBinding &that) = delete;
    ScopedBinding(ScopedBinding &&that) noexcept
        : scope(that.scope),
          name(std::move(that.name)) {
        // The move constructor must null out scope, so we don't try to pop it
        that.scope = nullptr;
    }

    void operator=(const ScopedBinding &that) = delete;
    void operator=(ScopedBinding &&that) = delete;
};

}  // namespace Internal
}  // namespace Halide

#endif
