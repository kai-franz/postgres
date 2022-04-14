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


double sample_freq = 0.0;

bool should_rerank(ExprState *state) {
  return state->eval_count++ == 500000;
//  return random() < RAND_MAX * sample_freq;
}

Datum ExecWithFilterManager(ExprState *state, ExprContext *econtext, bool *isNull) {
//  if (should_rerank(state) && state->qual_len != 0) {
//    void *last_filter = state->filters[state->qual_len - 1];
//    state->filters[state->qual_len - 1] = state->filters[0];
//    state->filters[0] = last_filter;
//  }
//  return state->exprfunc(state, econtext, isNull);
  int i = 0;
  bool next_clause = true;
  while (next_clause && i++ < state->qual_len + 1) {
    next_clause = state->clauses[i](state, econtext, isNull);
    next_clause = true; // TODO: remove this
  }
  return state->clauses[state->qual_len + 1](state, econtext, isNull);
}


//void FilterManager::Clause::RunFilter(VectorProjection *input_batch, TupleIdList *tid_list) {
//
//  if (!ShouldReRank()) {
//    for (const auto &term : terms_) {
//      term->fn(input_batch, tid_list, opaque_context_);
//      if (tid_list->IsEmpty()) break;
//    }
//    return;
//  }
//
//  if (TPL_UNLIKELY(input_copy_.GetCapacity() != tid_list->GetCapacity())) {
//    input_copy_.Resize(tid_list->GetCapacity());
//    temp_.Resize(tid_list->GetCapacity());
//  }
//
//  // Copy the input TID list now because we'll incrementally update the original
//  // as we apply the terms of the clause.
//  input_copy_.AssignFrom(*tid_list);
//
//#if COLLECT_OVERHEAD == 1
//  util::Timer<std::micro> timer;
//  timer.Start();
//
//  for (const auto &term : terms_) {
//    term->fn(input_batch, tid_list, opaque_context_);
//    if (tid_list->IsEmpty()) break;
//  }
//  timer.Stop();
//  const double fast = timer.GetElapsed();
//
//  tid_list->AssignFrom(input_copy_);
//  timer.Start();
//#endif
//
//  const auto tuple_count = tid_list->GetTupleCount();
//  const auto input_selectivity = tid_list->ComputeSelectivity();
//  for (const auto &term : terms_) {
//    temp_.AssignFrom(input_copy_);
//    const auto exec_ns = util::TimeNanos([&]() { term->fn(input_batch, &temp_, opaque_context_); });
//    const auto term_selectivity = temp_.ComputeSelectivity();
//    const auto term_cost = exec_ns / tuple_count;
//    term->rank = (input_selectivity - term_selectivity) / term_cost;
//    LOG_TRACE("Term [{}]: term-selectivity={:04.3f}, cost={:>06.3f}, rank={:.8f}",
//              term->insertion_index, term_selectivity, term_cost, term->rank);
//    tid_list->IntersectWith(temp_);
//  }
//
//#ifndef NDEBUG
//  // Log a message if the term ordering after re-ranking has changed.
//  const auto old_order = GetOptimalTermOrder();
//  std::sort(terms_.begin(), terms_.end(),
//  [](const auto &a, const auto &b) { return a->rank > b->rank; });
//  const auto new_order = GetOptimalTermOrder();
//  if (old_order != new_order) {
//    LOG_DEBUG("Order Change: old={}, new={}", fmt::join(old_order, ","), fmt::join(new_order, ","));
//  }
//#else
//  // Reorder the terms based on their updated ranking.
//  std::sort(terms_.begin(), terms_.end(),
//            [](const auto &a, const auto &b) { return a->rank > b->rank; });
//#endif
//
//  // Update sample count.
//  sample_count_++;
//
//#if COLLECT_OVERHEAD == 1
//  timer.Stop();
//  double slow = timer.GetElapsed();
//  overhead_micros_ += (slow - fast);
//#endif
//}
