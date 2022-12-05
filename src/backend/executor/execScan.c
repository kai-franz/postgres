/*-------------------------------------------------------------------------
 *
 * execScan.c
 *	  This code provides support for generalized relation scans. ExecScan
 *	  is passed a node and a pointer to a function to "do the right thing"
 *	  and return a tuple from the relation. ExecScan then does the tedious
 *	  stuff - checking the qualification and projecting the tuple
 *	  appropriately.
 *
 * Portions Copyright (c) 1996-2021, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/executor/execScan.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "executor/executor.h"
#include "executor/vector.h"
#include "miscadmin.h"
#include "utils/memutils.h"

#define VECTOR_SIZE 1024


/*
 * ExecScanFetch -- check interrupts & fetch next potential tuple
 *
 * This routine is concerned with substituting a test tuple if we are
 * inside an EvalPlanQual recheck.  If we aren't, just execute
 * the access method's next-tuple routine.
 */
static inline TupleTableSlot *
ExecScanFetch(ScanState *node,
			  ExecScanAccessMtd accessMtd,
			  ExecScanRecheckMtd recheckMtd)
{
	EState	   *estate = node->ps.state;

	CHECK_FOR_INTERRUPTS();

	if (estate->es_epq_active != NULL)
	{
		EPQState   *epqstate = estate->es_epq_active;

		/*
		 * We are inside an EvalPlanQual recheck.  Return the test tuple if
		 * one is available, after rechecking any access-method-specific
		 * conditions.
		 */
		Index		scanrelid = ((Scan *) node->ps.plan)->scanrelid;

		if (scanrelid == 0)
		{
			/*
			 * This is a ForeignScan or CustomScan which has pushed down a
			 * join to the remote side.  The recheck method is responsible not
			 * only for rechecking the scan/join quals but also for storing
			 * the correct tuple in the slot.
			 */

			TupleTableSlot *slot = node->ss_ScanTupleSlot;

			if (!(*recheckMtd) (node, slot))
				ExecClearTuple(slot);	/* would not be returned by scan */
			return slot;
		}
		else if (epqstate->relsubs_done[scanrelid - 1])
		{
			/*
			 * Return empty slot, as we already performed an EPQ substitution
			 * for this relation.
			 */

			TupleTableSlot *slot = node->ss_ScanTupleSlot;

			/* Return empty slot, as we already returned a tuple */
			return ExecClearTuple(slot);
		}
		else if (epqstate->relsubs_slot[scanrelid - 1] != NULL)
		{
			/*
			 * Return replacement tuple provided by the EPQ caller.
			 */

			TupleTableSlot *slot = epqstate->relsubs_slot[scanrelid - 1];

			Assert(epqstate->relsubs_rowmark[scanrelid - 1] == NULL);

			/* Mark to remember that we shouldn't return more */
			epqstate->relsubs_done[scanrelid - 1] = true;

			/* Return empty slot if we haven't got a test tuple */
			if (TupIsNull(slot))
				return NULL;

			/* Check if it meets the access-method conditions */
			if (!(*recheckMtd) (node, slot))
				return ExecClearTuple(slot);	/* would not be returned by
												 * scan */
			return slot;
		}
		else if (epqstate->relsubs_rowmark[scanrelid - 1] != NULL)
		{
			/*
			 * Fetch and return replacement tuple using a non-locking rowmark.
			 */

			TupleTableSlot *slot = node->ss_ScanTupleSlot;

			/* Mark to remember that we shouldn't return more */
			epqstate->relsubs_done[scanrelid - 1] = true;

			if (!EvalPlanQualFetchRowMark(epqstate, scanrelid, slot))
				return NULL;

			/* Return empty slot if we haven't got a test tuple */
			if (TupIsNull(slot))
				return NULL;

			/* Check if it meets the access-method conditions */
			if (!(*recheckMtd) (node, slot))
				return ExecClearTuple(slot);	/* would not be returned by
												 * scan */
			return slot;
		}
	}

	/*
	 * Run the node-type-specific access method function to get the next tuple
	 */
	return (*accessMtd) (node);
}

/* ----------------------------------------------------------------
 *		ExecScan
 *
 *		Scans the relation using the 'access method' indicated and
 *		returns the next qualifying tuple.
 *		The access method returns the next tuple and ExecScan() is
 *		responsible for checking the tuple returned against the qual-clause.
 *
 *		A 'recheck method' must also be provided that can check an
 *		arbitrary tuple of the relation against any qual conditions
 *		that are implemented internal to the access method.
 *
 *		Conditions:
 *		  -- the "cursor" maintained by the AMI is positioned at the tuple
 *			 returned previously.
 *
 *		Initial States:
 *		  -- the relation indicated is opened for scanning so that the
 *			 "cursor" is positioned before the first qualifying tuple.
 * ----------------------------------------------------------------
 */
TupleTableSlot *
ExecScan(ScanState *node,
		 ExecScanAccessMtd accessMtd,	/* function returning a tuple */
		 ExecScanRecheckMtd recheckMtd)
{
	ExprContext *econtext;
	ExprState  *qual;
	ProjectionInfo *projInfo;

	/*
	 * Fetch data from node
	 */
	qual = node->ps.qual;
	projInfo = node->ps.ps_ProjInfo;
	econtext = node->ps.ps_ExprContext;

	/* interrupt checks are in ExecScanFetch */

	/*
	 * If we have neither a qual to check nor a projection to do, just skip
	 * all the overhead and return the raw scan tuple.
	 */
	if (!qual && !projInfo)
	{
		ResetExprContext(econtext);
		return ExecScanFetch(node, accessMtd, recheckMtd);
	}

	/*
	 * Reset per-tuple memory context to free any expression evaluation
	 * storage allocated in the previous tuple cycle.
	 */
	ResetExprContext(econtext);

	/*
	 * get a tuple from the access method.  Loop until we obtain a tuple that
	 * passes the qualification.
	 */
	for (;;)
	{
		TupleTableSlot *slot;

		slot = ExecScanFetch(node, accessMtd, recheckMtd);

		/*
		 * if the slot returned by the accessMtd contains NULL, then it means
		 * there is nothing more to scan so we just return an empty slot,
		 * being careful to use the projection result slot so it has correct
		 * tupleDesc.
		 */
		if (TupIsNull(slot))
		{
			if (projInfo)
				return ExecClearTuple(projInfo->pi_state.resultslot);
			else
				return slot;
		}

		/*
		 * place the current tuple into the expr context
		 */
		econtext->ecxt_scantuple = slot;

		/*
		 * check that the current tuple satisfies the qual-clause
		 *
		 * check for non-null qual here to avoid a function call to ExecQual()
		 * when the qual is null ... saves only a few cycles, but they add up
		 * ...
		 */
		if (qual == NULL || ExecQual(qual, econtext))
		{
			/*
			 * Found a satisfactory scan tuple.
			 */
			if (projInfo)
			{
				/*
				 * Form a projection tuple, store it in the result tuple slot
				 * and return it.
				 */
				return ExecProject(projInfo);
			}
			else
			{
				/*
				 * Here, we aren't projecting, so just return scan tuple.
				 */
				return slot;
			}
		}
		else
			InstrCountFiltered1(node, 1);

		/*
		 * Tuple fails qual, so free per-tuple memory and try again.
		 */
		ResetExprContext(econtext);
	}
}

/* ----------------------------------------------------------------
 *		ExecScanVectorized
 *
 *		Vectorized version of ExecScan.
 * ----------------------------------------------------------------
 */
TupleTableSlot *
ExecScanVectorized(ScanState *node,
                   ExecScanAccessMtd accessMtd,	/* function returning a tuple */
                   ExecScanRecheckMtd recheckMtd)
{
  ExprContext *econtext;
  ExprState  *qual;
  ProjectionInfo *projInfo;
  bool		eof_tuplestore;
  bool qual_results[VECTOR_SIZE];

  /*
   * Fetch data from node
   */
  qual = node->ps.qual;
  projInfo = node->ps.ps_ProjInfo;
  econtext = node->ps.ps_ExprContext;
  Tuplestorestate *tuplestorestate;

  tuplestorestate = node->tuplestorestate;

  /* interrupt checks are in ExecScanFetch */

  /*
   * If first time through, and we need a tuplestore, initialize it.
   */
  if (tuplestorestate == NULL)
  {
    tuplestorestate = tuplestore_begin_heap(true, false, work_mem);
//    if (node->eflags & EXEC_FLAG_MARK)
//    {
//      /*
//       * Allocate a second read pointer to serve as the mark. We know it
//       * must have index 1, so needn't store that.
//       */
//      int			ptrno PG_USED_FOR_ASSERTS_ONLY;
//
//      ptrno = tuplestore_alloc_read_pointer(tuplestorestate,
//                                            node->eflags);
//      Assert(ptrno == 1);
//    }
    node->tuplestorestate = tuplestorestate;
    eof_tuplestore = true;
  } else {
    eof_tuplestore = node->tupleStorePos == node->tupleStoreSize;
  }



  /**
   * If we aren't at the end of the tuplestore, advance the read pointer
   * to the next tuple that satisfies the qual (or the end of the tuplestore, if no such tuple exists).
   */
  if (!eof_tuplestore && qual && !qual_results[node->tupleStorePos]) {
    int prevTupleStorePos = node->tupleStorePos;
    while (node->tupleStorePos < node->tupleStoreSize && !qual_results[node->tupleStorePos]) {
      node->tupleStorePos++;
    }
    if (node->tupleStorePos < node->tupleStoreSize) {
      int delta = node->tupleStorePos - prevTupleStorePos;
      InstrCountFiltered1(node, delta);
      tuplestore_skiptuples(tuplestorestate, delta, true);
    } else {
      eof_tuplestore = true;
    }
  }

  while (eof_tuplestore) {
    /*
     * Nothing more to scan, so return an empty slot, being careful to use
     * the projection result slot so it has correct tupleDesc.
     */
    if (node->eof_underlying) {
      if (projInfo)
        return ExecClearTuple(projInfo->pi_state.resultslot);
      else
        return NULL;
    }

    /*
     * Fill the vector with tuples from the underlying node.
     */
    tuplestore_clear(tuplestorestate);
    int tupleStoreSize = 0;
    bool eof_underlying = false;
    elog(LOG, "Filling vector");
    while (tupleStoreSize < VECTOR_SIZE && !eof_underlying) {
      TupleTableSlot *slot;
      slot = ExecScanFetch(node, accessMtd, recheckMtd);
      if (TupIsNull(slot)) {
          eof_underlying = true;
          elog(LOG, "Underlying node is empty");
          continue;
      } else {
        tuplestore_puttupleslot(tuplestorestate, slot);
        tupleStoreSize++;
      }
    }
    node->eof_underlying = eof_underlying;
    node->tupleStoreSize = tupleStoreSize;

    /*
     * Reset the read pointer to the start of the tuplestore.
     */
    tuplestore_rescan(tuplestorestate);

    node->tupleStorePos = -1;
    /*
     * Evaluate the qual clause for each tuple in the tuplestore.
     */
    if (qual) {
      TupleTableSlot *slot;
      slot = node->ps.ps_ResultTupleSlot;
      for (int i = 0; i < tupleStoreSize; i++) {
        tuplestore_gettupleslot(tuplestorestate, true, true, slot);
        econtext->ecxt_scantuple = slot;
        qual_results[i] = (bool) ExecQual(qual, econtext);
        ResetExprContext(econtext);
        if (qual_results[i] && node->tupleStorePos == -1) {
          node->tupleStorePos = i;
        }
      }
      /*
       * Reset the read pointer to the start of the tuplestore.
       */
      tuplestore_rescan(tuplestorestate);
      if (node->tupleStorePos >= 0) {
        tuplestore_skiptuples(tuplestorestate, node->tupleStorePos, true);
      }
    } else {
      node->tupleStorePos = 0;
    }
    /*
     * If none of the tuples in the tuplestore satisfy the qual clause, then
     * discard the current vector and try again.
     */
    eof_tuplestore = node->tupleStorePos == -1;
    if (eof_tuplestore) {
      elog(LOG, "Vector contains no qualifying tuples");
    }
  }



//  /*
//   * Reset per-tuple memory context to free any expression evaluation
//   * storage allocated in the previous tuple cycle.
//   */
//  ResetExprContext(econtext);

  /*
   * If we reach this point, there is at least one tuple in the vector that satisfies the qual clause.
   * This will be the next tuple returned by the tuple store; return this tuple.
   */
  TupleTableSlot *slot;
  slot = node->ps.ps_ResultTupleSlot;
  tuplestore_gettupleslot(tuplestorestate, true, true, slot);
  elog(LOG, "Returning tuple %d", node->tupleStorePos);
  node->tupleStorePos++;
  econtext->ecxt_scantuple = slot;
  Assert(!TupIsNull(slot));
  if (projInfo) {
    /*
     * Form a projection tuple, store it in the result tuple slot
     * and return it.
     */
    ResetExprContext(econtext);
    bool qualPassed = ExecQual(qual, econtext);
    Assert(qualPassed);
    Assert(false);
    TupleTableSlot *projectedSlot = ExecProject(projInfo);
    Assert(!TupIsNull(projectedSlot));
    return projectedSlot;
  } else {
    /*
     * Here, we aren't projecting, so just return scan tuple.
     */
    return slot;
  }
}

/*
 * ExecAssignScanProjectionInfo
 *		Set up projection info for a scan node, if necessary.
 *
 * We can avoid a projection step if the requested tlist exactly matches
 * the underlying tuple type.  If so, we just set ps_ProjInfo to NULL.
 * Note that this case occurs not only for simple "SELECT * FROM ...", but
 * also in most cases where there are joins or other processing nodes above
 * the scan node, because the planner will preferentially generate a matching
 * tlist.
 *
 * The scan slot's descriptor must have been set already.
 */
void
ExecAssignScanProjectionInfo(ScanState *node)
{
	Scan	   *scan = (Scan *) node->ps.plan;
	TupleDesc	tupdesc = node->ss_ScanTupleSlot->tts_tupleDescriptor;

	ExecConditionalAssignProjectionInfo(&node->ps, tupdesc, scan->scanrelid);
}

/*
 * ExecAssignScanProjectionInfoWithVarno
 *		As above, but caller can specify varno expected in Vars in the tlist.
 */
void
ExecAssignScanProjectionInfoWithVarno(ScanState *node, Index varno)
{
	TupleDesc	tupdesc = node->ss_ScanTupleSlot->tts_tupleDescriptor;

	ExecConditionalAssignProjectionInfo(&node->ps, tupdesc, varno);
}

/*
 * ExecScanReScan
 *
 * This must be called within the ReScan function of any plan node type
 * that uses ExecScan().
 */
void
ExecScanReScan(ScanState *node)
{
	EState	   *estate = node->ps.state;

	/*
	 * We must clear the scan tuple so that observers (e.g., execCurrent.c)
	 * can tell that this plan node is not positioned on a tuple.
	 */
	ExecClearTuple(node->ss_ScanTupleSlot);

	/* Rescan EvalPlanQual tuple if we're inside an EvalPlanQual recheck */
	if (estate->es_epq_active != NULL)
	{
		EPQState   *epqstate = estate->es_epq_active;
		Index		scanrelid = ((Scan *) node->ps.plan)->scanrelid;

		if (scanrelid > 0)
			epqstate->relsubs_done[scanrelid - 1] = false;
		else
		{
			Bitmapset  *relids;
			int			rtindex = -1;

			/*
			 * If an FDW or custom scan provider has replaced the join with a
			 * scan, there are multiple RTIs; reset the epqScanDone flag for
			 * all of them.
			 */
			if (IsA(node->ps.plan, ForeignScan))
				relids = ((ForeignScan *) node->ps.plan)->fs_relids;
			else if (IsA(node->ps.plan, CustomScan))
				relids = ((CustomScan *) node->ps.plan)->custom_relids;
			else
				elog(ERROR, "unexpected scan node: %d",
					 (int) nodeTag(node->ps.plan));

			while ((rtindex = bms_next_member(relids, rtindex)) >= 0)
			{
				Assert(rtindex > 0);
				epqstate->relsubs_done[rtindex - 1] = false;
			}
		}
	}
}
