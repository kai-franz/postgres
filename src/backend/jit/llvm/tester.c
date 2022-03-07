#include <stdlib.h>
#include "postgres.h"
#include "fmgr.h"
#include "common/int.h"

#include "access/htup_details.h"
#include "access/tupdesc_details.h"
#include "executor/tuptable.h"
#include "jit/llvmjit.h"
#include "jit/llvmjit_emit.h"
#include "executor/execExpr.h"

FunctionCallInfo createFilter(void);

/*
 * This macro initializes all the fields of a FunctionCallInfoBaseData except
 * for the args[] array.
 */
#define InitFunctionCallInfoData(Fcinfo, Flinfo, Nargs, Collation, Context, Resultinfo) \
	do { \
		(Fcinfo).flinfo = (Flinfo); \
		(Fcinfo).context = (Context); \
		(Fcinfo).resultinfo = (Resultinfo); \
		(Fcinfo).fncollation = (Collation); \
		(Fcinfo).isnull = false; \
		(Fcinfo).nargs = (Nargs); \
	} while (0)

FunctionCallInfo createFilter() {
  ExprEvalStep scratch;
  FunctionCallInfo fcinfo = malloc(sizeof(struct FunctionCallInfoBaseData));
  FmgrInfo flinfo;
  int nargs = 2;
  Oid inputcollid = 0;
  flinfo.fn_addr = &int4ge;
  flinfo.fn_oid = 150;
  flinfo.nargs = 2;
  flinfo.fn_retset = false;
  flinfo.fn_strict = true;
  flinfo.fn_stats = 2;
  flinfo.fn_extra = NULL;
  flinfo.fn_mcxt = NULL;
  fmNodePtr fn_expr = malloc(sizeof(struct Node));
  fn_expr->type = T_OpExpr;
  flinfo.fn_expr = fn_expr;
  InitFunctionCallInfoData(*fcinfo, &flinfo, nargs, inputcollid, NULL, NULL);
  return fcinfo;
}
