#ifndef _SS_STATS_
#define _SS_STATS_

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INVALID_START (0ULL - 1ULL)

// Mostly inspired by Cilk stats

enum interval
{
    INTERVAL_PRE_AUTOSCHEDULE,              ///< Time spent initializing the auto scheduler
    INTERVAL_AUTOSCHEDULE,                  ///< Time spent in the auto scheduler
    INTERVAL_PRE_BEAM,                      ///< Time spent in initializing the mcts/rts
    INTERVAL_MCTS,                          ///< Time spent in running the mcts/rts
    INTERVAL_MCTS_BEAM_ROLLOUT,             ///< Time spent in running the mcts beam rollout
    INTERVAL_MCTS_PRE_DOROLLOUT,            ///< Time spent before performing the dorollout for individual node
    INTERVAL_MCTS_ROLLOUT_ITERATIONS,       ///< Time spent in performing the mcts iteration
    INTERVAL_MCTS_SELECTION_AND_EXPANSION,  ///< Time spent in selecting and expansion
    INTERVAL_MCTS_SIMULATION,               ///< Time spent in simulation
    INTERVAL_MCTS_BACKPROPOGATE,            ///< Time spent in backpropogating    
    INTERVAL_MCTS_POST_DOROLLOUT,           ///< Time spent after performing all the rollout
    INTERVAL_MCTS_FILL_BEAM,                ///< Time spent in running the mcts fill beam
    INTERVAL_POST_AUTOSCHEDULE,             ///< Time spent cleaning up the auto scheduler
    INTERVAL_N
};

typedef struct statistics
{
    /** Number of times each interval is entered */
    unsigned long long count[INTERVAL_N];

    /**
     * Time when the system entered each interval, in system-dependent
     * "ticks"
     */
    unsigned long long start[INTERVAL_N];

    /** Total time spent in each interval, in system-dependent "ticks" */
    unsigned long long accum[INTERVAL_N];

    /**
     * Largest global number of stacks seen by this worker.
     * The true maximum at end of execution is the max of the
     * worker maxima.
     */
    long stack_hwm;
} statistics;

void __ss_init_stats(statistics *s);
void __ss_accum_stats(statistics *to, statistics *from);
void __ss_note_interval(statistics *s, enum interval i);
unsigned long long __ss_start_interval(statistics *s, enum interval i);
unsigned long long __ss_start_interval_val(statistics *s, enum interval i, unsigned long long val);
void __ss_reset_interval(statistics *s, enum interval i);
void __ss_stop_interval(statistics *s, enum interval i);
void __ss_stop_interval_val(statistics *s, enum interval i, unsigned long long val);
void dump_interesting_stats(FILE *stat_file, statistics *s);
unsigned long long __ss_start_and_stop_interval(statistics *s , enum interval i, enum interval j);

#ifdef SS_PROFILE
#pragma message "Compiled with stats"
# define START_INTERVAL(w, i) __ss_start_interval(w, i);
# define START_INTERVAL_VAL(w, i, val) __ss_start_interval_val(w, i, val);
# define RESET_INTERVAL(w, i) __ss_reset_interval(w, i)
# define STOP_INTERVAL(w, i) __ss_stop_interval(w, i);
# define STOP_INTERVAL_VAL(w, i, val) __ss_stop_interval_val(w, i, val);
# define NOTE_INTERVAL(w, i) __ss_note_interval(w, i);
# define START_AND_STOP_INTERVAL(w, start, stop) __ss_start_and_stop_interval(w, start, stop);
#else
#pragma message "Compiled without stats"
/** Start an interval.  No effect unless SS_PROFILE is defined. */
# define START_INTERVAL(w, i)
# define START_INTERVAL_VAL(w, i, val)
# define RESET_INTERVAL(w, i)
/** End an interval.  No effect unless SS_PROFILE is defined. */
# define STOP_INTERVAL(w, i)
# define STOP_INTERVAL_VAL(w, i, val)
/** Increment a counter.  No effect unless SS_PROFILE is defined. */
# define NOTE_INTERVAL(w, i)

# define START_AND_STOP_INTERVAL(w, startindex, stopindex)
#endif


#endif
