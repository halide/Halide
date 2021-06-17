#include "mcts_stats.h"

// Mostly based on cilk runtime stats

static const char *names[] = {
  /*[INTERVAL_PRE_AUTOSCHEDULE]*/               "in pre autoschedule",
  /*[INTERVAL_AUTOSCHEDULE]*/                   "in autoschedule",
  /*[INTERVAL_PRE_BEAM]*/                       "  of which: pre beam",
  /*[INTERVAL_MCTS]*/                           "  of which: mcts",
  /*[INTERVAL_MCTS_BEAM_ROLLOUT]*/              "     of which: mcts beam rollout",
  /*[INTERVAL_MCTS_PRE_DOROLLOUT]*/             "        of which: mcts pre dorollout",
  /*[INTERVAL_MCTS_ROLLOUT_ITERATIONS]*/        "        of which: mcts iterations",
  /*[INTERVAL_MCTS_SELECTION_AND_EXPANSION]*/   "           of which: selection and expansion",
  /*[INTERVAL_MCTS_SIMULATION]*/                "           of which: simulation",
  /*[INTERVAL_MCTS_BACKPROPOGATE]*/             "           of which: backpropogation",
  /*[INTERVAL_MCTS_POST_DOROLLOUT]*/            "        of which: mcts post dorollout",
  /*[INTERVAL_MCTS_FILL_BEAM]*/                 "     of which: mcts fill beam",
  /*[INTERVAL_POST_AUTOSCHEDULE]*/              "in post autoschedule"
};

/* timer support */
unsigned long long __cilkrts_getticks(void)
{
#if defined __i386__ || defined __x86_64
  unsigned a, d;
  __asm__ volatile("rdtsc" : "=a" (a), "=d" (d));
  return ((unsigned long long)a) | (((unsigned long long)d) << 32);
#else
#   warning "unimplemented cycle counter"
  return 0;
#endif
}

void __ss_init_stats(statistics *s)
{
  int i;
  for (i = 0; i < INTERVAL_N; ++i) {
    s->start[i] = INVALID_START;
    s->accum[i] = 0;
    s->count[i] = 0;
  }

  s->stack_hwm = 0;
}

void __ss_accum_stats(statistics *to, statistics *from)
{
  assert(0 && "Not used");
  int i;

  for (i = 0; i < INTERVAL_N; ++i) {
    to->accum[i] += from->accum[i];
    to->count[i] += from->count[i];
    from->accum[i] = 0;
    from->count[i] = 0;
  }

  if (from->stack_hwm > to->stack_hwm)
    to->stack_hwm = from->stack_hwm;
  from->stack_hwm = 0;
}

void __ss_note_interval(statistics *s, enum interval i)
{
  if (s) {
    assert(s->start[i] == INVALID_START);
    s->count[i]++;
  }
}

unsigned long long  __ss_start_interval(statistics *s, enum interval i)
{
  if (s) {
    assert(s->start[i] == INVALID_START);
    s->start[i] = __cilkrts_getticks();
    s->count[i]++;
    return s->start[i];
  }
  return 0;
}

unsigned long long  __ss_start_interval_val(statistics *s, enum interval i, unsigned long long val)
{
  if (s) {
    assert(s->start[i] == INVALID_START);
    s->start[i] = val;
    s->count[i]++;
    return val;
  }
  return 0;
}

void __ss_reset_interval(statistics *s, enum interval i)
{
  if(s){
    assert(s->start[i] != INVALID_START);
    s->start[i] = INVALID_START;
    s->count[i]--;
  }

}

void __ss_stop_interval(statistics *s, enum interval i)
{
  if (s) {
    assert(s->start[i] != INVALID_START);
    s->accum[i] += __cilkrts_getticks() - s->start[i];
    s->start[i] = INVALID_START;
  }
}

void __ss_stop_interval_val(statistics *s, enum interval i, unsigned long long val)
{
  if (s) {
    assert(s->start[i] != INVALID_START);
    s->accum[i] += val - s->start[i];
    s->start[i] = INVALID_START;
  }
}

unsigned long long __ss_start_and_stop_interval(statistics * s, enum interval start, enum interval stop)
{
  if (s) {
    assert(s->start[start] == INVALID_START);
    assert(s->start[stop] != INVALID_START);

    s->start[start] = __cilkrts_getticks();
    s->count[start]++;

    s->accum[stop] += s->start[start] - s->start[stop];
    s->start[stop] = INVALID_START;
    return s->start[start];
  }
  return 0;


}

void dump_interesting_stats(FILE * stat_file, statistics *s){
  long long totalTime = s->accum[INTERVAL_PRE_AUTOSCHEDULE] + s->accum[INTERVAL_AUTOSCHEDULE] + s->accum[INTERVAL_POST_AUTOSCHEDULE];
  assert(totalTime);

  fprintf(stat_file, "\n");
  // Format of statistics: count:Ticks:ticks/count:Percentage
  for(int i =0;i<INTERVAL_N; ++i){
    fprintf(stat_file, "%s,%llu", names[i], s->count[i]);

    if(s->accum[i]){
      switch(i){
      case INTERVAL_PRE_AUTOSCHEDULE:
        fprintf(stat_file, ",%.3f,%.3f,%.10f %%", (double)s->accum[i], (double)s->accum[i]/(double)s->count[i], 100.0 * (double)s->accum[i] / (double)totalTime);
        break;
      case INTERVAL_AUTOSCHEDULE:
        fprintf(stat_file, ",%.3f,%.3f,%.10f %%", (double)s->accum[i], (double)s->accum[i]/(double)s->count[i], 100.0 * (double)s->accum[i] / (double)totalTime);
        break;
      case INTERVAL_PRE_BEAM:
        fprintf(stat_file, ",%.3f,%.3f,%.10f %%", (double)s->accum[i], (double)s->accum[i]/(double)s->count[i], 100.0 * (double)s->accum[i] / (double)s->accum[INTERVAL_AUTOSCHEDULE]);
        break;
      case INTERVAL_MCTS:
        fprintf(stat_file, ",%.3f,%.3f,%.10f %%", (double)s->accum[i], (double)s->accum[i]/(double)s->count[i], 100.0 * (double)s->accum[i] / (double)s->accum[INTERVAL_AUTOSCHEDULE]);
        break;
      case INTERVAL_MCTS_BEAM_ROLLOUT:
        fprintf(stat_file, ",%.3f,%.3f,%.10f %%", (double)s->accum[i], (double)s->accum[i]/(double)s->count[i], 100.0 * (double)s->accum[i] / (double)s->accum[INTERVAL_MCTS]);
        break;
      case INTERVAL_MCTS_PRE_DOROLLOUT:
        fprintf(stat_file, ",%.3f,%.3f,%.10f %%", (double)s->accum[i], (double)s->accum[i]/(double)s->count[i], 100.0 * (double)s->accum[i] / (double)s->accum[INTERVAL_MCTS_BEAM_ROLLOUT]);
        break;
      case INTERVAL_MCTS_ROLLOUT_ITERATIONS:
        fprintf(stat_file, ",%.3f,%.3f,%.10f %%", (double)s->accum[i], (double)s->accum[i]/(double)s->count[i], 100.0 * (double)s->accum[i] / (double)s->accum[INTERVAL_MCTS_BEAM_ROLLOUT]);
        break;
      case INTERVAL_MCTS_SELECTION_AND_EXPANSION:
        fprintf(stat_file, ",%.3f,%.3f,%.10f %%", (double)s->accum[i], (double)s->accum[i]/(double)s->count[i], 100.0 * (double)s->accum[i] / (double)s->accum[INTERVAL_MCTS_ROLLOUT_ITERATIONS]);
        break;
      case INTERVAL_MCTS_SIMULATION:
        fprintf(stat_file, ",%.3f,%.3f,%.10f %%", (double)s->accum[i], (double)s->accum[i]/(double)s->count[i], 100.0 * (double)s->accum[i] / (double)s->accum[INTERVAL_MCTS_ROLLOUT_ITERATIONS]);
        break;
      case INTERVAL_MCTS_BACKPROPOGATE:
        fprintf(stat_file, ",%.3f,%.3f,%.10f %%", (double)s->accum[i], (double)s->accum[i]/(double)s->count[i], 100.0 * (double)s->accum[i] / (double)s->accum[INTERVAL_MCTS_ROLLOUT_ITERATIONS]);
        break;
      case INTERVAL_MCTS_POST_DOROLLOUT:
        fprintf(stat_file, ",%.3f,%.3f,%.10f %%", (double)s->accum[i], (double)s->accum[i]/(double)s->count[i], 100.0 * (double)s->accum[i] / (double)s->accum[INTERVAL_MCTS_BEAM_ROLLOUT]);
        break;
      case INTERVAL_MCTS_FILL_BEAM:
        fprintf(stat_file, ",%.3f,%.3f,%.10f %%", (double)s->accum[i], (double)s->accum[i]/(double)s->count[i], 100.0 * (double)s->accum[i] / (double)s->accum[INTERVAL_MCTS]);
        break;
      case INTERVAL_POST_AUTOSCHEDULE:
        fprintf(stat_file, ",%.3f,%.3f,%.10f %%", (double)s->accum[i], (double)s->accum[i]/(double)s->count[i], 100.0 * (double)s->accum[i] / (double)totalTime);
        break;
      }
    }
    fprintf(stat_file, "\n");
  }
  fprintf(stat_file, "\n");
}

