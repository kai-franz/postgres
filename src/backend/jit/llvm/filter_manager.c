#include <stdlib.h>
#include "postgres.h"

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>

#include "access/htup_details.h"
#include "access/nbtree.h"
#include "catalog/objectaccess.h"
#include "catalog/pg_type.h"
#include "executor/execExpr.h"
#include "executor/execdebug.h"
#include "executor/nodeAgg.h"
#include "executor/nodeSubplan.h"
#include "funcapi.h"
#include "jit/llvmjit.h"
#include "jit/llvmjit_emit.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "parser/parse_coerce.h"
#include "parser/parsetree.h"
#include "pgstat.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/date.h"
#include "utils/fmgrtab.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/timestamp.h"
#include "utils/typcache.h"
#include "utils/xml.h"

#include <time.h>

#define SAMPLE_SIZE 1024
#define SAMPLE_FREQ 0.1;

void reorder_clauses(ExprState *state);
Datum ExecWithFilterManager(ExprState *state, ExprContext *econtext, bool *isNull);
bool should_rerank(ExprState *state);

bool should_rerank(ExprState *state) {
  bool res = random() < (RAND_MAX / SAMPLE_SIZE) * SAMPLE_FREQ;
  if (res) {
    elog(LOG, "reranking; tuples since last rerank: %d", state->nonreranks);
    state->nonreranks = 0;
    state->reranks++;
  } else {
    state->nonreranks++;
  }
  return res;
}

Datum ExecWithFilterManager(ExprState *state, ExprContext *econtext, bool *isNull) {
  Datum nextIter = 1;
  int i = 0;
  while (nextIter != 0 && i < state->num_funcs - 1) {
    nextIter = state->clauses[i](state, econtext, isNull);
    i++;
  }
  /*
  // store resnull and return the appropriate value
  Datum resvalue = state->resvalue;
  bool resnull = state->resnull;
  *isNull = resnull;
   */

  return state->clauses[state->num_funcs - 1](state, econtext, isNull);
}

void reorder_clauses(ExprState *state) {
  // sort clauses 1 through num_funcs - 2 based on rank, descending, using selection sort
  bool changed = false;
  for (int i = 1; i < state->num_funcs - 1; i++) {
    int max_idx = i;
    for (int j = i + 1; j < state->num_funcs - 1; j++) {
      if (state->ranks[j] > state->ranks[max_idx]) {
        max_idx = j;
      }
    }
    if (max_idx != i) {
      changed = true;
      double temp_rank = state->ranks[i];
      state->ranks[i] = state->ranks[max_idx];
      state->ranks[max_idx] = temp_rank;
      ExprStateEvalFunc temp_clause = state->clauses[i];
      state->clauses[i] = state->clauses[max_idx];
      state->clauses[max_idx] = temp_clause;
    }
  }
  if (changed) {
    elog(LOG, "filter ordering has changed");
  }
}
