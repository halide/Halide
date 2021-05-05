#ifndef HANNK_LOG_H
#define HANNK_LOG_H

namespace hannk {

// Note: all severity values output to stderr, not stdout.
// Note: ERROR does *not* trigger an exit()/abort() call. FATAL does.
enum LogSeverity {
    INFO = 0,
    WARNING = 1,
    ERROR = 2,
    FATAL = 3,
};

namespace internal {

// All logging in hannk is done via this bottleneck;
// this is deliberately put in its own file to allow
// build systems to replace it without weak-linkage
// tricks or other shenanigans. Note that code shouldn't
// call this directly (it's meant for internal use by Logger and Checker).
//
// Note that calling with severity doesn't require the implementation to
// call abort(), although it's fine to do so if you like.
//
// Note that in the default implementation, all severity values output to stderr, not stdout.
void hannk_log(LogSeverity severity, const char *msg);

}  // namespace internal
}  // namespace hannk

#endif  // HANNK_LOG_H
