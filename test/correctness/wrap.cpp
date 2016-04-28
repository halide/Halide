#include "Halide.h"
#include <assert.h>
#include <stdio.h>
#include <algorithm>
#include <map>
#include <set>

using std::map;
using std::vector;
using std::string;

using namespace Halide;
using namespace Halide::Internal;

class CheckCalls : public IRVisitor {
public:
    map<string, vector<string>> calls; // Caller -> set of callees
    string producer = "";
private:
    using IRVisitor::visit;

    void visit(const ProducerConsumer *op) {
    	string old_producer = producer;
        producer = op->name;
        calls[producer]; // Make sure each producer is allocated a slot
        op->produce.accept(this);
        producer = old_producer;

    	if (op->update.defined()) {
    		// Just lump all the update stages together
	        producer = op->name + ".update(" + std::to_string(0) + ")";
    	    calls[producer]; // Make sure each producer is allocated a slot
    		op->update.accept(this);
	        producer = old_producer;
    	}
    	op->consume.accept(this);
        producer = old_producer;
    }

    void visit(const Load *op) {
    	IRVisitor::visit(op);
        if (!producer.empty()) {
        	assert(calls.count(producer) > 0);
        	vector<string> &callees = calls[producer];
        	if(std::find(callees.begin(), callees.end(), op->name) == callees.end()) {
		    	callees.push_back(op->name);
		    	// Sort the callees after every insertion to make our life easier
		    	// during correctness check
				std::sort(callees.begin(), callees.end());
			}
    	}
    }
};

int func_wrap_test() {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x) = x;
    f.compute_root();

    g(x, y) = f(x);
    Func wrapper = f.in(g).compute_root();

    // Check the call graphs.
    // Expect 'g' to call 'wrapper', 'wrapper' to call 'f', 'f' to call nothing
    Module m = g.compile_to_module({});
    CheckCalls c;
    m.functions[0].body.accept(&c);

    if (c.calls.size() != 3) {
    	printf("Expect 3 callers instead of %d\n", (int)c.calls.size());
    	return -1;
    }
    if (!(c.calls.count(f.name()) && (c.calls[f.name()].size() == 0))) {
    	printf("Expect \"f\" to call nothing\n");
    	return -1;
    }
    if (!(c.calls.count(wrapper.name()) &&
    	  (c.calls[wrapper.name()].size() == 1) &&
    	  (c.calls[wrapper.name()][0] == f.name()))) {
    	printf("Expect \"wrapper\" to call \"f\"\n");
    	return -1;
    }
    if (!(c.calls.count(g.name()) &&
    	  (c.calls[g.name()].size() == 1) &&
    	  (c.calls[g.name()][0] == wrapper.name()))) {
    	printf("Expect \"g\" to call wrapper\n");
    	return -1;
    }

    Image<int> im = g.realize(200, 200);
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = x;
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n",
                       x, y, im(x, y), correct);
                return -1;
            }
        }
    }
    return 0;
}

int global_wrap_test() {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x) = x;
    f.compute_root();

    g(x, y) = f(x);
    Func wrapper = f.in().compute_root();

    // Check the call graphs.
    // Expect 'g' to call 'wrapper', 'wrapper' to call 'f', 'f' to call nothing
    Module m = g.compile_to_module({});
    CheckCalls c;
    m.functions[0].body.accept(&c);

    if (c.calls.size() != 3) {
    	printf("Expect 3 callers instead of %d\n", (int)c.calls.size());
    	return -1;
    }
    if (!(c.calls.count(f.name()) && (c.calls[f.name()].size() == 0))) {
    	printf("Expect \"f\" to call nothing\n");
    	return -1;
    }
    if (!(c.calls.count(wrapper.name()) &&
    	  (c.calls[wrapper.name()].size() == 1) &&
    	  (c.calls[wrapper.name()][0] == f.name()))) {
    	printf("Expect \"wrapper\" to call \"f\"\n");
    	return -1;
    }
    if (!(c.calls.count(g.name()) &&
    	  (c.calls[g.name()].size() == 1) &&
    	  (c.calls[g.name()][0] == wrapper.name()))) {
    	printf("Expect \"g\" to call wrapper\n");
    	return -1;
    }

    Image<int> im = g.realize(200, 200);
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = x;
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n",
                       x, y, im(x, y), correct);
                return -1;
            }
        }
    }
    return 0;
}

int update_defined_after_wrap_test() {
    Func f("f"), g("g");
    Var x("x"), y("y");

    f(x, y) = x + y;
    f.compute_root();

    g(x, y) = f(x, y);
    Func wrapper = f.in(g).compute_root();

    // Update of 'g' is defined after f.in(g) is called. g's updates should
    // still call f's wrapper.
    RDom r(0, 100, 0, 100);
    r.where(r.x < r.y);
    g(r.x, r.y) += 2*f(r.x, r.y);

    // Check the call graphs.
    // Expect both initialization of 'g' and its update to call 'wrapper' and 'g',
    // wrapper' to call 'f', 'f' to call nothing
    Module m = g.compile_to_module({});
    CheckCalls c;
    m.functions[0].body.accept(&c);

    if (c.calls.size() != 4) {
    	printf("Expect 4 callers instead of %d\n", (int)c.calls.size());
    	return -1;
    }
    if (!(c.calls.count(f.name()) && (c.calls[f.name()].size() == 0))) {
    	printf("Expect \"f\" to call nothing\n");
    	return -1;
    }
    if (!(c.calls.count(wrapper.name()) &&
    	  (c.calls[wrapper.name()].size() == 1) &&
    	  (c.calls[wrapper.name()][0] == f.name()))) {
    	printf("Expect \"wrapper\" to call \"f\"\n");
    	return -1;
    }
    if (!(c.calls.count(g.name()) &&
    	  (c.calls[g.name()].size() == 1) &&
    	  (c.calls[g.name()][0] == wrapper.name()))) {
    	printf("Expect \"g\" to call \"wrapper\"\n");
    	return -1;
    }
    if (!(c.calls.count(g.update(0).name()) &&
    	  (c.calls[g.update(0).name()].size() == 2) &&
    	  (c.calls[g.update(0).name()][0] == wrapper.name()) &&
    	  (c.calls[g.update(0).name()][1] == g.name()))) {
    	printf("Expect \"g_update\" to call \"wrapper\" and \"g\"\n");
    	return -1;
    }

    Image<int> im = g.realize(200, 200);
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = x + y;
            if ((0 <= x && x <= 99) && (0 <= y && y <= 99)) {
                correct += (x < y) ? 2*correct : 0;
            }
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n",
                       x, y, im(x, y), correct);
                return -1;
            }
        }
    }
    return 0;
}

int rdom_wrapper_test() {
	// Scheduling initialization + update on the same compute level using wrapper
	Func f("f"), g("g"), result("result");
    Var x("x"), y("y");

    f(x, y) = x + y;
    f.compute_root();

    g(x, y) = 10;
    g(x, y) += 2 * f(x, x);
    g(x, y) += 3 * f(y, y);
    result(x, y) = g(x, y) + 20;
    Func wrapper = g.in(result).compute_at(result, x);

    // Check the call graphs.
    // Expect 'result' to call 'wrapper', initialization of 'g' to call nothing
    // and its update to call 'f' and 'g', wrapper' to call 'g', 'f' to call nothing
    Module m = result.compile_to_module({});
    CheckCalls c;
    m.functions[0].body.accept(&c);

    if (c.calls.size() != 5) {
    	printf("Expect 5 callers instead of %d\n", (int)c.calls.size());
    	return -1;
    }
    if (!(c.calls.count(f.name()) && (c.calls[f.name()].size() == 0))) {
    	printf("Expect \"f\" to call nothing\n");
    	return -1;
    }
    if (!(c.calls.count(wrapper.name()) &&
    	  (c.calls[wrapper.name()].size() == 1) &&
    	  (c.calls[wrapper.name()][0] == g.name()))) {
    	printf("Expect \"wrapper\" to call \"g\"\n");
    	return -1;
    }
    if (!(c.calls.count(result.name()) &&
    	  (c.calls[result.name()].size() == 1) &&
    	  (c.calls[result.name()][0] == wrapper.name()))) {
    	printf("Expect \"result\" to call \"wrapper\"\n");
    	return -1;
    }
    if (!(c.calls.count(g.name()) && (c.calls[g.name()].size() == 0))) {
    	printf("Expect \"g\" to call nothing\n");
    	return -1;
    }
    if (!(c.calls.count(g.update(0).name()) &&
    	  (c.calls[g.update(0).name()].size() == 2) &&
    	  (c.calls[g.update(0).name()][0] == f.name()) &&
    	  (c.calls[g.update(0).name()][1] == g.name()))) {
    	printf("Expect \"g_update\" to call \"f\" and \"g\"\n");
    	return -1;
    }

    Image<int> im = result.realize(200, 200);
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = 4*x + 6* y + 30;
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n",
                       x, y, im(x, y), correct);
                return -1;
            }
        }
    }
    return 0;
}

int global_and_custom_wrap_test() {
	// Scheduling initialization + update on the same compute level using wrapper
	Func f("f"), g("g"), result("result");
    Var x("x"), y("y");

    f(x) = x;
    f.compute_root();

    g(x, y) = f(x);
    result(x, y) = f(x) + g(x, y);
    Func f_in_g = f.in(g).compute_at(g, x);
    Func f_wrapper = f.in().compute_at(result, y);
    g.compute_at(result, y);

    // Check the call graphs.
    // Expect 'result' to call 'g' and 'f_wrapper', 'g' to call 'f_in_g',
    // 'f_wrapper' to call 'f', f_in_g' to call 'f', 'f' to call nothing
    Module m = result.compile_to_module({});
    /*CheckCalls c;
    m.functions[0].body.accept(&c);

    for (const auto &iter : c.calls) {
    	std::cout << "Producer: " << iter.first << "\n";
    	for (const auto &callees : iter.second) {
    		std::cout << "     Callees: " << callees << "\n";
    	}
    }

    if (c.calls.size() != 5) {
    	printf("Expect 5 callers instead of %d\n", (int)c.calls.size());
    	return -1;
    }
    if (!(c.calls.count(f.name()) && (c.calls[f.name()].size() == 0))) {
    	printf("Expect \"f\" to call nothing\n");
    	return -1;
    }
    if (!(c.calls.count(f_in_g.name()) &&
    	  (c.calls[f_in_g.name()].size() == 1) &&
    	  (c.calls[f_in_g.name()][0] == f.name()))) {
    	printf("Expect \"f_in_g\" to call \"f\"\n");
    	return -1;
    }
    if (!(c.calls.count(f_wrapper.name()) &&
    	  (c.calls[f_wrapper.name()].size() == 1) &&
    	  (c.calls[f_wrapper.name()][0] == f.name()))) {
    	printf("Expect \"f_wrapper\" to call \"f\"\n");
    	return -1;
    }
    if (!(c.calls.count(g.name()) &&
    	  (c.calls[g.name()].size() == 1) &&
    	  (c.calls[g.name()][0] == f_in_g.name()))) {
    	printf("Expect \"g\" to call \"f_in_g\"\n");
    	return -1;
    }
    if (!(c.calls.count(result.name()) &&
    	  (c.calls[result.name()].size() == 2) &&
    	  (c.calls[result.name()][0] == f_wrapper.name()) &&
    	  (c.calls[result.name()][1] == g.name()))) {
    	printf("Expect \"result\" to call \"f_wrapper\" and \"g\"\n");
    	return -1;
    }*/

    Image<int> im = result.realize(200, 200);
    for (int y = 0; y < im.height(); y++) {
        for (int x = 0; x < im.width(); x++) {
            int correct = 2*x;
            if (im(x, y) != correct) {
                printf("im(%d, %d) = %d instead of %d\n",
                       x, y, im(x, y), correct);
                return -1;
            }
        }
    }
    return 0;
}

int main(int argc, char **argv) {
	/*printf("Running func wrap test\n");
    if (func_wrap_test() != 0) {
        return -1;
    }

    printf("Running global wrap test\n");
    if (func_wrap_test() != 0) {
        return -1;
    }

	printf("Running update is defined after wrap test\n");
    if (update_defined_after_wrap_test() != 0) {
        return -1;
    }

    printf("Running rdom wrapper test\n");
    if (rdom_wrapper_test() != 0) {
        return -1;
    }*/

    printf("Running global + custom wrapper test\n");
    if (global_and_custom_wrap_test() != 0) {
        return -1;
    }

    printf("Success!\n");
    return 0;
}



