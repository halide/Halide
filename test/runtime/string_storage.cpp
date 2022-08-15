#include "common.h"

#include "internal/string_storage.h"

using namespace Halide::Runtime::Internal;

int main(int argc, char **argv) {
    void *user_context = (void *)1;

    // test class interface
    {
        StringStorage ss;
        halide_abort_if_false(user_context, ss.length() == 0);

        const char *ts1 = "Testing!";
        const size_t ts1_length = strlen(ts1);
        ss.assign(user_context, ts1);
        halide_abort_if_false(user_context, ss.length() == ts1_length);
        halide_abort_if_false(user_context, ss.contains(ts1));

        const char *ts2 = "More ";
        const size_t ts2_length = strlen(ts2);
        ss.prepend(user_context, ts2);
        halide_abort_if_false(user_context, ss.length() == (ts1_length + ts2_length));
        halide_abort_if_false(user_context, ss.contains(ts2));
        halide_abort_if_false(user_context, ss.contains(ts1));

        ss.append(user_context, '!');
        halide_abort_if_false(user_context, ss.length() == (ts1_length + ts2_length + 1));

        ss.clear(user_context);
        halide_abort_if_false(user_context, ss.length() == 0);
    }

    // test copy and equality
    {
        const char *ts1 = "Test One!";
        const size_t ts1_length = strlen(ts1);

        const char *ts2 = "Test Two!";
        const size_t ts2_length = strlen(ts2);

        StringStorage ss1;
        ss1.assign(user_context, ts1, ts1_length);

        StringStorage ss2;
        ss2.assign(user_context, ts2, ts2_length);

        StringStorage ss3(ss1);

        halide_abort_if_false(user_context, ss1.length() == (ts1_length));
        halide_abort_if_false(user_context, ss2.length() == (ts2_length));
        halide_abort_if_false(user_context, ss3.length() == ss1.length());

        halide_abort_if_false(user_context, ss1 != ss2);
        halide_abort_if_false(user_context, ss1 == ss3);

        ss2 = ss1;
        halide_abort_if_false(user_context, ss1 == ss2);
    }
    print(user_context) << "Success!\n";
    return 0;
}
