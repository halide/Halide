#include "Halide.h"

#include <map>
#include <mutex>
#include <set>
#include <stdio.h>
#include <string>
#include <thread>
#include <vector>

using namespace Halide;

namespace {

struct Event {
    int id = 0;
    halide_trace_event_code_t event = halide_trace_load;
    int parent_id = 0;
    int thread_id = 0;
    std::string func;
    std::thread::id host_thread_id;
};

struct Store {
    int x = 0;
    int y = 0;
    int parent_id = 0;
    int thread_id = 0;
};

std::mutex trace_mutex;
std::map<int, Event> events;
std::vector<Store> stores;
std::set<int> parallel_task_ids;
int next_event_id = 1;
bool trace_failed = false;

int my_trace(JITUserContext *, const halide_trace_event_t *ev) {
    std::scoped_lock lock(trace_mutex);

    const int id = next_event_id++;
    events[id] = {id, ev->event, ev->parent_id, ev->thread_id, ev->func, std::this_thread::get_id()};

    if (ev->event == halide_trace_begin_parallel_task) {
        parallel_task_ids.insert(id);
    } else if (ev->event == halide_trace_store) {
        if (ev->dimensions != 2 || ev->lanes != 1) {
            printf("Store trace event had %d dimensions and %d lanes.\n",
                   ev->dimensions, ev->lanes);
            trace_failed = true;
            return id;
        }
        stores.push_back({ev->coordinates[0], ev->coordinates[1],
                          ev->parent_id, ev->thread_id});
    }

    return id;
}

int parallel_task_ancestor_id(int id) {
    while (id != 0) {
        auto it = events.find(id);
        if (it == events.end()) {
            printf("Trace parent id %d was not found.\n", id);
            return 0;
        }
        if (it->second.event == halide_trace_begin_parallel_task) {
            return id;
        }
        id = it->second.parent_id;
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (get_jit_target_from_environment().arch == Target::WebAssembly) {
        printf("[SKIP] WebAssembly JIT does not support threads.\n");
        return 0;
    }

    Internal::JITSharedRuntime::set_num_threads(4);

    Func f("f");
    Var x("x"), y("y");

    f(x, y) = x + y;
    f.compute_root().parallel(y).trace_stores().trace_realizations();
    f.jit_handlers().custom_trace = &my_trace;

    f.realize({8, 8});

    if (trace_failed) {
        return 1;
    }

    if (parallel_task_ids.size() != 8) {
        printf("Expected one parallel task trace event per y row; saw %d.\n",
               (int)parallel_task_ids.size());
        return 1;
    }

    if (stores.size() != 64) {
        printf("Expected 64 store events; saw %d.\n",
               (int)stores.size());
        return 1;
    }

    std::map<int, int> task_id_for_y;
    std::map<int, int> thread_id_for_y;
    std::map<int, std::set<int>> x_values_for_y;
    std::map<std::thread::id, int> thread_id_for_host_thread;
    std::map<int, std::thread::id> host_thread_for_thread_id;
    std::set<int> task_ids_for_stores;
    std::set<int> thread_ids_for_tasks;
    for (const Store &store : stores) {
        const int task_id = parallel_task_ancestor_id(store.parent_id);
        if (task_id == 0) {
            printf("Store at (%d, %d) did not have a parallel task ancestor.\n",
                   store.x, store.y);
            return 1;
        }

        const Event &task = events.at(task_id);
        if (task.thread_id == 0) {
            printf("Task event %d did not have a thread ID.\n", task_id);
            return 1;
        }
        if (store.thread_id != 0) {
            printf("Store at (%d, %d) unexpectedly carried a thread ID %d.\n",
                   store.x, store.y, store.thread_id);
            return 1;
        }
        if (task.func != "f.s0.y") {
            printf("Store at (%d, %d) was under task func '%s', not f.s0.y.\n",
                   store.x, store.y, task.func.c_str());
            return 1;
        }

        auto [it, inserted] = task_id_for_y.emplace(store.y, task_id);
        if (!inserted && it->second != task_id) {
            printf("Stores in y row %d mapped to multiple task IDs: %d and %d.\n",
                   store.y, it->second, task_id);
            return 1;
        }
        auto [thread_it, thread_inserted] = thread_id_for_y.emplace(store.y, task.thread_id);
        if (!thread_inserted && thread_it->second != task.thread_id) {
            printf("Stores in y row %d mapped to multiple thread IDs: %d and %d.\n",
                   store.y, thread_it->second, task.thread_id);
            return 1;
        }
        auto [host_it, host_inserted] = thread_id_for_host_thread.emplace(task.host_thread_id, task.thread_id);
        if (!host_inserted && host_it->second != task.thread_id) {
            printf("A host thread mapped to multiple trace thread IDs: %d and %d.\n",
                   host_it->second, task.thread_id);
            return 1;
        }
        auto [id_it, id_inserted] = host_thread_for_thread_id.emplace(task.thread_id, task.host_thread_id);
        if (!id_inserted && id_it->second != task.host_thread_id) {
            printf("Trace thread ID %d mapped to multiple host threads.\n",
                   task.thread_id);
            return 1;
        }
        x_values_for_y[store.y].insert(store.x);
        task_ids_for_stores.insert(task_id);
        thread_ids_for_tasks.insert(task.thread_id);
    }

    if (task_id_for_y.size() != 8 || task_ids_for_stores != parallel_task_ids) {
        printf("Expected exactly one used task ID per y row.\n");
        return 1;
    }

    if (thread_id_for_y.size() != 8 || thread_ids_for_tasks.empty()) {
        printf("Expected one used thread ID per y row; saw %d rows and %d thread IDs:",
               (int)thread_id_for_y.size(), (int)thread_ids_for_tasks.size());
        for (int id : thread_ids_for_tasks) {
            printf(" %d", id);
        }
        printf("\n");
        return 1;
    }

    for (int y = 0; y < 8; y++) {
        if (!task_id_for_y.count(y)) {
            printf("Missing stores for y row %d.\n", y);
            return 1;
        }
        for (int x = 0; x < 8; x++) {
            if (!x_values_for_y[y].count(x)) {
                printf("Missing store at (%d, %d).\n", x, y);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
