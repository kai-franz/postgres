#include <stdlib.h>

double sample_freq = 0.0;

bool should_rerank() {
  return random() < RAND_MAX * sample_freq;
}

Datum *ExecWithFilterManager(ExprState *state, ExprContext *econtext, bool *isNull) {
  if (!should_rerank()) {
    return state->exprfunc(state, econtext, isNull);
  }

}