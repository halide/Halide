#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <map>
#include <vector>
#include <string>
#include <iostream>
#include <unistd.h>

using std::map;
using std::vector;

typedef uint32_t Id;

struct Packet {
    Id id, parent;
    uint8_t event, type, bits, width, value_idx, num_int_args;
    char name[17];
    uint8_t payload[4096-32]; // Not all of this will be used, but this is the max possible size.

    size_t value_bytes() const {
        size_t bytes_per_elem = 1;
        while (bytes_per_elem*8 < bits) bytes_per_elem <<= 1;
        return bytes_per_elem * width;
    }

    size_t int_args_bytes() const {
        return sizeof(int) * num_int_args;
    }

    size_t payload_bytes() const {
        return value_bytes() + int_args_bytes();
    }

    // Grab a packet from stdin. Returns false when stdin closes.
    bool read_from_stdin() {
        if (!read_stdin(this, 32)) {
            return false;
        }
        assert(read_stdin(payload, payload_bytes()) &&
               "Unexpected EOF mid-packet");
        return true;
    }

private:
    bool read_stdin(void *d, ssize_t size) {
        uint8_t *dst = (uint8_t *)d;
        if (!size) return true;
        while (1) {
            ssize_t s = read(0, dst, size);
            if (s == 0) {
                // EOF
                return false;
            } else if (s < 0) {
                perror("Failed during read");
                exit(-1);
                return 0;
            } else if (s == size) {
                return true;
            }
            size -= s;
            dst += s;
        }
    }
};

class Point {
    vector<int> p;
public:
    Point(int num_int_args, const int *int_args) {
        p.insert(p.end(), int_args, int_args + num_int_args);
    }

    Point(const vector<int> &v) : p(v) {
    }

    Point() {}

    int operator[](int x) const {
        return p[x];
    }

    int &operator[](int x) {
        return p[x];
    }

    int dimensions() const {
        return (int)p.size();
    }

    bool operator<(const Point &other) const {
        for (size_t i = 0; i < p.size(); i++) {
            if (p[i] < other[i]) return true;
            if (p[i] > other[i]) return false;
        }
        return false;
    }
};

struct Box {
    Point top_left;
    Point bottom_right;

    Box() {}

    Box(const Point &tl, const Point &br) : top_left(tl), bottom_right(br) {}

    Box(int num_int_args, int *int_args) {
        vector<int> tl, br;
        for (int i = 0; i < num_int_args/2; i++) {
            tl.push_back(int_args[i*2]);
            br.push_back(int_args[i*2] + int_args[i*2+1] - 1);
        }
        top_left = Point(tl);
        bottom_right = Point(br);
    }

    int dimensions() const {
        return top_left.dimensions();
    }

    void include(const Point &p) {
        if (dimensions() == 0) {
            top_left = p;
            bottom_right = p;
        } else {
            assert(p.dimensions() == top_left.dimensions());
            assert(p.dimensions() == bottom_right.dimensions());
            for (int i = 0; i < dimensions(); i++) {
                top_left[i] = std::min(top_left[i], p[i]);
                bottom_right[i] = std::max(bottom_right[i], p[i]);
            }
        }
    }

    void include(const Box &b) {
        include(b.top_left);
        include(b.bottom_right);
    }

    bool contains(const Point &p) const {
        assert(p.dimensions() == top_left.dimensions());
        assert(p.dimensions() == bottom_right.dimensions());
        for (int i = 0; i < p.dimensions(); i++) {
            if (top_left[i] > p[i]) return false;
            if (bottom_right[i] < p[i]) return false;
        }
        return true;
    }

    bool contains(const Box &b) const {
        return contains(b.top_left) && contains(b.bottom_right);
    }

};

class Count {
    size_t val;

public:
    Count() : val(0) {}

    void operator++(int) {
        val++;
    }

    void operator+=(int x) {
        val += x;
    }

    void operator+=(const Count &x) {
        val += x.val;
    }

    size_t value() const {
        return val;
    }

};



struct Production {
    enum {Producing, Updating, Consuming} state;

    Box region;

    void load(Count clock, const Packet &p) {
        assert(state == Updating || state == Consuming);
    }

    void store(Count clock, const Packet &p) {
        assert(state == Producing || state == Updating);
    }
};


struct Realization {
    map<Id, Production *> productions;

    Box region;

    struct PointState {
        size_t last_load, last_store;
        PointState() : last_load(0), last_store(0) {}
    };
    map<Point, PointState> state_map;

    void load(Count clock, const Packet &p) {
        Production *prod = productions[p.parent];
        assert(prod && "Load without a parent");
        prod->load(clock, p);
    }

    void store(Count clock, const Packet &p) {
        Production *prod = productions[p.parent];
        assert(prod && "Store without a parent");
        prod->store(clock, p);
    }

    void produce(Count clock, const Packet &p) {
        Production *prod = new Production;
        productions[p.id] = prod;
        prod->state = Production::Producing;
    }

    void update(Count clock, const Packet &p) {
        Production *prod = productions[p.parent];
        assert(prod && "Update without a parent");
        prod->state = Production::Updating;
    }

    void consume(Count clock, const Packet &p) {
        Production *prod = productions[p.parent];
        assert(prod && "Consume without a parent");
        prod->state = Production::Consuming;
    }

    void end_consume(Count clock, const Packet &p) {
        Production *prod = productions[p.parent];
        assert(prod && "Consume without a parent");
        // Retrieve stats
        delete prod;
        productions.erase(p.parent);
    }

};


struct FuncStats {
    map<Id, Realization *> realizations;

    Count loads, stores;

    FuncStats() {}

    void load(Count clock, const Packet &p) {
        Realization *r = realizations[p.parent];
        assert(r && "Load without a parent");
        r->load(clock, p);
        loads++;
    }

    void store(Count clock, const Packet &p) {
        Realization *r = realizations[p.parent];
        assert(r && "Store without a parent");
        r->store(clock, p);
        stores++;
    }

    void produce(Count clock, const Packet &p) {
        Realization *r = realizations[p.parent];
        assert(r && "Produce without a parent");
        realizations[p.id] = r;
        r->produce(clock, p);
    }

    void update(Count clock, const Packet &p) {
        Realization *r = realizations[p.parent];
        assert(r && "Update without a parent");
        r->update(clock, p);
    }

    void consume(Count clock, const Packet &p) {
        Realization *r = realizations[p.parent];
        assert(r && "Consume without a parent");
        r->consume(clock, p);
    }

    void end_consume(Count clock, const Packet &p) {
        Realization *r = realizations[p.parent];
        assert(r && "Update without a parent");
        r->end_consume(clock, p);
        realizations.erase(p.parent);
    }

    void begin_realize(Count clock, const Packet &p) {
        Realization *r = new Realization;
        realizations[p.id] = r;
    }

    void end_realize(Count clock, const Packet &p) {
        Realization *r = realizations[p.parent];
        // Aggregate stats
        delete r;
        realizations.erase(p.parent);
    }

    void report() const {
        std::cout << " stores:" << stores.value() << '\n';
        std::cout << " loads:" << loads.value() << '\n';
    }
};



int main(int argc, char **argv) {
    assert(sizeof(Packet) == 4096);

    map<std::string, FuncStats> funcs;

    Count clock;

    while (1) {
        Packet p;
        if (!p.read_from_stdin()) {
            break;
        }

        //printf("Packet header: %u %u %d %d %d %d %d %d %s\n", p.id, p.parent, p.event, p.type, p.bits, p.width, p.value_idx, p.num_int_args, p.name);

        FuncStats &f = funcs[p.name];

        switch (p.event) {
        case 0:
            f.load(clock, p);
            clock += p.value_bytes();
            break;
        case 1:
            f.store(clock, p);
            clock += p.value_bytes();
            break;
        case 2: // begin realization
            f.begin_realize(clock, p);
            break;
        case 3: // end realization
            f.end_realize(clock, p);
            break;
        case 4: // produce
            f.produce(clock, p);
            break;
        case 5: // update
            f.update(clock, p);
            break;
        case 6: // consume
            f.consume(clock, p);
            break;
        case 7: // end consume
            f.end_consume(clock, p);
            break;
        default:
            exit(-1);
        }
    }

    for (map<std::string, FuncStats>::iterator iter = funcs.begin();
         iter != funcs.end(); ++iter) {
        std::cout << "Function " << iter->first << ":\n";
        iter->second.report();
    }

    return 0;
}
