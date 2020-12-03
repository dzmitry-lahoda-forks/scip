/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2020 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not visit scip.zib.de.         */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   nlhdlr_default.c
 * @brief  default nonlinear handler that calls expression handler methods
 * @author Stefan Vigerske
 */

#include <string.h>

#include "scip/nlhdlr_default.h"
#include "scip/pub_nlhdlr.h"
#include "scip/cons_nonlinear.h"
#include "scip/struct_misc.h" //FIXME

/* fundamental nonlinear handler properties */
#define NLHDLR_NAME            "default"
#define NLHDLR_DESC            "default handler for expressions"
#define NLHDLR_DETECTPRIORITY  0
#define NLHDLR_ENFOPRIORITY    0

/** evaluates an expression w.r.t. the values in the auxiliary variables */
static
SCIP_RETCODE evalExprInAux(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_EXPR*            expr,               /**< expression to be evaluated */
   SCIP_Real*            val,                /**< buffer to store value of expression */
   SCIP_SOL*             sol                 /**< solution to be evaluated */
)
{
   SCIP_Real* childvals;
   SCIP_VAR* childvar;
   int c;

   assert(scip != NULL);
   assert(expr != NULL);
   assert(val != NULL);
   assert(SCIPexprGetNChildren(expr) > 0);

   SCIP_CALL( SCIPallocBufferArray(scip, &childvals, SCIPexprGetNChildren(expr)) );

   for( c = 0; c < SCIPexprGetNChildren(expr); ++c )
   {
      childvar = SCIPgetExprAuxVarNonlinear(SCIPexprGetChildren(expr)[c]);
      /* there should be an auxiliary variable, because we created them in detect for every child if we said that we will separate;
       * at the moment, EVALAUX should only be called for nlhdlrs that said they will separate
       * if that changes, then we should handle this here, e.g., via *val = SCIPexprGetEvalValue(expr); break;
       */
      assert(childvar != NULL);

      childvals[c] = SCIPgetSolVal(scip, sol, childvar);
   }

   SCIP_CALL( SCIPevalExprShallow(scip, expr, childvals, val) );

   SCIPfreeBufferArray(scip, &childvals);

   return SCIP_OKAY;
}

static
SCIP_DECL_NLHDLRDETECT(nlhdlrDetectDefault)
{ /*lint --e{715}*/
   SCIP_EXPRHDLR* exprhdlr;
   SCIP_Bool estimatebelowusesactivity = FALSE;
   SCIP_Bool estimateaboveusesactivity = FALSE;
   int c;

   assert(scip != NULL);
   assert(nlhdlr != NULL);
   assert(expr != NULL);
   assert(enforcing != NULL);
   assert(participating != NULL);
   assert(nlhdlrexprdata != NULL);

   exprhdlr = SCIPexprGetHdlr(expr);
   assert(exprhdlr != NULL);

   if( (*enforcing & SCIP_NLHDLR_METHOD_ACTIVITY) == 0 )
   {
      /* expr handlers having reverseprop but no inteval is something that we don't support at the moment for simplicity */
      assert(!SCIPexprhdlrHasReverseProp(exprhdlr) || SCIPexprhdlrHasIntEval(exprhdlr));

      /* participate in inteval and/or reverseprop if that is not yet provided in enforcing and we have inteval */
      if( SCIPexprhdlrHasIntEval(exprhdlr) )
         *participating = SCIP_NLHDLR_METHOD_ACTIVITY;
   }

   /* participate in sepa if exprhdlr for expr has an estimate callback and sepa below or above is still missing */
   if( ((*enforcing & SCIP_NLHDLR_METHOD_SEPABOTH) != SCIP_NLHDLR_METHOD_SEPABOTH) && SCIPexprhdlrHasEstimate(exprhdlr) )
   {
      /* communicate back that the nlhdlr will provide the separation on the currently missing sides */
      if( (*enforcing & SCIP_NLHDLR_METHOD_SEPABELOW) == 0 )
         *participating |= SCIP_NLHDLR_METHOD_SEPABELOW;

      if( (*enforcing & SCIP_NLHDLR_METHOD_SEPAABOVE) == 0 )
         *participating |= SCIP_NLHDLR_METHOD_SEPAABOVE;
   }

   if( !*participating )
      return SCIP_OKAY;

   /* since this is the default handler, we enforce where we participate */
   *enforcing |= *participating;

   /* increment activity usage counter and create auxiliary variables if necessary
    * if separating, first guess whether we will use activities in estimate (distinguish under- and overestimation)
    * we assume that the exprhdlr will use activity on all children iff we are estimating on a nonconvex side
    * TODO it would be better to request this information directly from the exprhdlr than inferring it from curvature, but with the currently available exprhdlr that wouldn't make a difference
    */
   if( *participating & SCIP_NLHDLR_METHOD_SEPABOTH )
   {
      SCIP_EXPRCURV* childcurv;

      /* allocate memory to store the required curvature of the children (though we don't use it) */
      SCIP_CALL( SCIPallocBufferArray(scip, &childcurv, SCIPexprGetNChildren(expr)) );

      if( *participating & SCIP_NLHDLR_METHOD_SEPABELOW )
      {
         /* check whether the expression is convex */
         SCIP_Bool isconvex;
         SCIP_CALL( SCIPexprhdlrCurvatureExpr(scip, expr, SCIP_EXPRCURV_CONVEX, &isconvex, childcurv) );
         estimatebelowusesactivity = !isconvex;
      }

      if( *participating & SCIP_NLHDLR_METHOD_SEPAABOVE )
      {
         /* check whether the expression is concave */
         SCIP_Bool isconcave;
         SCIP_CALL( SCIPexprhdlrCurvatureExpr(scip, expr, SCIP_EXPRCURV_CONCAVE, &isconcave, childcurv) );
         estimateaboveusesactivity = !isconcave;
      }

      /* free memory */
      SCIPfreeBufferArray(scip, &childcurv);
   }

   /* indicate enforcement methods required in children:
    * - if separating, make sure that (auxiliary) variable will exist
    * - if activity computation, then register activity usage
    * - if estimating on a non-convex side, then indicate activity usage for separation for that side
    */
   for( c = 0; c < SCIPexprGetNChildren(expr); ++c )
   {
      /* todo skip auxvarusage for value-expressions? would then need update in evalExprInAux, too */
      SCIP_CALL( SCIPregisterExprUsageNonlinear(scip, SCIPexprGetChildren(expr)[c],
         *participating & SCIP_NLHDLR_METHOD_SEPABOTH,
         *participating & SCIP_NLHDLR_METHOD_ACTIVITY, estimatebelowusesactivity, estimateaboveusesactivity) );
   }

   return SCIP_OKAY;
}

static
SCIP_DECL_NLHDLREVALAUX(nlhdlrEvalAuxDefault)
{ /*lint --e{715}*/
   assert(expr != NULL);
   assert(auxvalue != NULL);

   SCIP_CALL( evalExprInAux(scip, expr, auxvalue, sol) );

   return SCIP_OKAY;
}

static
SCIP_DECL_NLHDLRINITSEPA(nlhdlrInitSepaDefault)
{ /*lint --e{715}*/
   SCIP_INTERVAL* childrenbounds;
   SCIP_Real* coefs[SCIP_EXPR_MAXINITESTIMATES];
   SCIP_Real constant[SCIP_EXPR_MAXINITESTIMATES];
   SCIP_VAR* auxvar;
   int nreturned;
   int i;

   assert(scip != NULL);
   assert(expr != NULL);

   SCIP_CALL( SCIPallocBufferArray(scip, &childrenbounds, SCIPexprGetNChildren(expr)) );

   for( i = 0; i < SCIPexprGetNChildren(expr); ++i )
   {
      auxvar = SCIPgetExprAuxVarNonlinear(SCIPexprGetChildren(expr)[i]);
      assert(auxvar != NULL);

      // TODO infty to infty
      SCIPintervalSetBounds(&childrenbounds[i], SCIPvarGetLbGlobal(auxvar), SCIPvarGetUbGlobal(auxvar));
   }

   /* call the separation initialization callback of the expression handler */
   if( underestimate )
   {
      SCIP_CALL( SCIPexprhdlrInitEstimatesExpr(scip, expr, childrenbounds, FALSE, coefs, constant, &nreturned) );

      // TODO create rowprep, cleanup, create row, add row
   }

   if( overestimate )
   {
      SCIP_CALL( SCIPexprhdlrInitEstimatesExpr(scip, expr, childrenbounds, TRUE, coefs, constant, &nreturned) );

      // TODO create rowprep, cleanup, create row, add row
   }

   SCIPfreeBufferArray(scip, &childrenbounds);

   return SCIP_OKAY;
}

static
SCIP_DECL_NLHDLRESTIMATE(nlhdlrEstimateDefault)
{ /*lint --e{715}*/
   SCIP_Real constant;
   SCIP_Bool* branchcand = NULL;
   int nchildren;
   int c;
   SCIP_INTERVAL* localbounds;
   SCIP_INTERVAL* globalbounds;
   SCIP_Real* refpoint;
   SCIP_ROWPREP* rowprep;
   SCIP_VAR* auxvar;

   assert(scip != NULL);
   assert(expr != NULL);
   assert(rowpreps != NULL);
   assert(success != NULL);

   *addedbranchscores = FALSE;

   nchildren = SCIPexprGetNChildren(expr);

   SCIP_CALL( SCIPallocBufferArray(scip, &localbounds, SCIPexprGetNChildren(expr)) );
   SCIP_CALL( SCIPallocBufferArray(scip, &globalbounds, SCIPexprGetNChildren(expr)) );
   SCIP_CALL( SCIPallocBufferArray(scip, &refpoint, SCIPexprGetNChildren(expr)) );

   SCIP_CALL( SCIPcreateRowprep(scip, &rowprep, overestimate ? SCIP_SIDETYPE_LEFT : SCIP_SIDETYPE_RIGHT, TRUE) );

   /* make sure enough space is available in rowprep arrays */
   SCIP_CALL( SCIPensureRowprepSize(scip, rowprep, nchildren) );
   assert(rowprep->varssize >= nchildren);

   /* we need to pass a branchcand array to exprhdlr's estimate also if not asked to add branching scores */
   SCIP_CALL( SCIPallocBufferArray(scip, &branchcand, nchildren) );

   for( c = 0; c < nchildren; ++c )
   {
      auxvar = SCIPgetExprAuxVarNonlinear(SCIPexprGetChildren(expr)[c]);
      assert(auxvar != NULL);

      // TODO infty to infty
      SCIPintervalSetBounds(&localbounds[c], SCIPvarGetLbGlobal(auxvar), SCIPvarGetUbGlobal(auxvar));
      SCIPintervalSetBounds(&globalbounds[c], SCIPvarGetLbGlobal(auxvar), SCIPvarGetUbGlobal(auxvar));

      refpoint[c] = SCIPgetSolVal(scip, sol, auxvar);

      branchcand[c] = TRUE;
   }

   /* call the estimation callback of the expression handler */
   SCIP_CALL( SCIPexprhdlrEstimateExpr(scip, expr, localbounds, globalbounds, refpoint, overestimate, targetvalue, rowprep->coefs, &constant, &rowprep->local, success, branchcand) );

   if( *success )
   {
      int i;

      /* add variables to rowprep */
      rowprep->nvars = nchildren;
      for( i = 0; i < rowprep->nvars; ++i )
      {
         rowprep->vars[i] = SCIPgetExprAuxVarNonlinear(SCIPexprGetChildren(expr)[i]);
         assert(rowprep->vars[i] != NULL);
      }

      rowprep->side = -constant;

      SCIP_CALL( SCIPsetPtrarrayVal(scip, rowpreps, 0, rowprep) );

      (void) SCIPsnprintf(rowprep->name, SCIP_MAXSTRLEN, "%sestimate_%s%p_%s%d",
         overestimate ? "over" : "under",
         SCIPexprhdlrGetName(SCIPexprGetHdlr(expr)),
         (void*)expr,
         sol != NULL ? "sol" : "lp",
         sol != NULL ? SCIPsolGetIndex(sol) : SCIPgetNLPs(scip));
   }
   else
   {
      SCIPfreeRowprep(scip, &rowprep);
   }

   if( addbranchscores )
   {
      SCIP_Real violation;

#ifndef BRSCORE_ABSVIOL
      SCIP_CALL( SCIPgetExprRelAuxViolationNonlinear(scip, expr, auxvalue, sol, &violation, NULL, NULL) );
#else
      SCIP_CALL( SCIPgetExprAbsAuxViolationNonlinear(scip, expr, auxvalue, sol, &violation, NULL, NULL) );
#endif
      assert(violation > 0.0);  /* there should be a violation if we were called to enforce */

      if( nchildren == 1 )
      {
         if( branchcand[0] )
         {
            SCIP_CALL( SCIPaddExprsViolScoreNonlinear(scip, SCIPexprGetChildren(expr), 1, violation, sol, addedbranchscores) );
         }
      }
      else
      {
         SCIP_EXPR** exprs;
         int nexprs = 0;

         /* get list of those children that have the branchcand-flag set */
         SCIP_CALL( SCIPallocBufferArray(scip, &exprs, nchildren) );

         for( c = 0; c < nchildren; ++c )
            if( branchcand[c] )
               exprs[nexprs++] = SCIPexprGetChildren(expr)[c];

         SCIP_CALL( SCIPaddExprsViolScoreNonlinear(scip, exprs, nexprs, violation, sol, addedbranchscores) );

         SCIPfreeBufferArray(scip, &exprs);
      }

      if( *addedbranchscores )
      {
         /* count this branchscore as belonging to the exprhdlr, too
          * thus, it will be counted for the default nlhdlr, but also for this exprhdlr
          */
         SCIPexprhdlrIncrementNBranchScore(SCIPexprGetHdlr(expr));
      }
   }

   SCIPfreeBufferArray(scip, &branchcand);
   SCIPfreeBufferArray(scip, &refpoint);
   SCIPfreeBufferArray(scip, &globalbounds);
   SCIPfreeBufferArray(scip, &localbounds);

   return SCIP_OKAY;
}

static
SCIP_DECL_NLHDLREXITSEPA(nlhdlrExitSepaDefault)
{ /*lint --e{715}*/
   assert(scip != NULL);
   assert(expr != NULL);

   /* call the separation deinitialization callback of the expression handler */
   SCIP_CALL( SCIPexitsepaConsExprExprHdlr(scip, expr) );

   return SCIP_OKAY;
}

static
SCIP_DECL_NLHDLRINTEVAL(nlhdlrIntevalDefault)
{ /*lint --e{715}*/
   assert(scip != NULL);
   assert(expr != NULL);

   /* call the interval evaluation callback of the expression handler */
   SCIP_CALL( SCIPevalExprInterval(scip, expr, interval, intevalvar, intevalvardata) );

   return SCIP_OKAY;
}

static
SCIP_DECL_NLHDLRREVERSEPROP(nlhdlrReversepropDefault)
{ /*lint --e{715}*/
   SCIP_INTERVAL* childrenbounds;
   int c;

   assert(scip != NULL);
   assert(expr != NULL);
   assert(infeasible != NULL);
   assert(nreductions != NULL);

   *nreductions = 0;

   SCIP_CALL( SCIPallocBufferArray(scip, &childrenbounds, SCIPexprGetNChildren(expr)) );
   for( c = 0; c < SCIPexprGetNChildren(expr); ++c )
      childrenbounds[c] = SCIPgetExprBoundsNonlinear(scip, SCIPexprGetChildren(expr)[c]);

   /* call the reverse propagation callback of the expression handler */
   SCIP_CALL( SCIPreversepropExpr(scip, expr, bounds, childrenbounds, infeasible) );

   if( !*infeasible )
      for( c = 0; c < SCIPexprGetNChildren(expr); ++c )
      {
         SCIP_CALL( SCIPtightenExprIntervalNonlinear(scip, SCIPexprGetChildren(expr)[c], childrenbounds[c], infeasible, nreductions) );
      }

   SCIPfreeBufferArray(scip, &childrenbounds);

   return SCIP_OKAY;
}

static
SCIP_DECL_NLHDLRCOPYHDLR(nlhdlrCopyhdlrDefault)
{ /*lint --e{715}*/
   assert(targetscip != NULL);
   assert(sourcenlhdlr != NULL);
   assert(strcmp(SCIPnlhdlrGetName(sourcenlhdlr), NLHDLR_NAME) == 0);

   SCIP_CALL( SCIPincludeNlhdlrDefault(targetscip) );

   return SCIP_OKAY;
}

/** includes default nonlinear handler in nonlinear constraint handler */
SCIP_RETCODE SCIPincludeNlhdlrDefault(
   SCIP*                 scip                /**< SCIP data structure */
   )
{
   SCIP_CONSHDLR* conshdlr;
   SCIP_NLHDLR* nlhdlr;

   assert(scip != NULL);

   conshdlr = SCIPfindConshdlr(scip, "nonlinear");
   if( conshdlr == NULL )
   {
      SCIPerrorMessage("nonlinear constraint handler not found");
      return SCIP_PLUGINNOTFOUND;
   }

   SCIP_CALL( SCIPincludeNlhdlrNonlinear(scip, conshdlr, &nlhdlr, NLHDLR_NAME, NLHDLR_DESC, NLHDLR_DETECTPRIORITY, NLHDLR_ENFOPRIORITY, nlhdlrDetectDefault, nlhdlrEvalAuxDefault, NULL) );
   assert(nlhdlr != NULL);

   SCIPnlhdlrSetCopyHdlr(nlhdlr, nlhdlrCopyhdlrDefault);
   SCIPnlhdlrSetSepa(nlhdlr, nlhdlrInitSepaDefault, NULL, nlhdlrEstimateDefault, nlhdlrExitSepaDefault);
   SCIPnlhdlrSetProp(nlhdlr, nlhdlrIntevalDefault, nlhdlrReversepropDefault);

   return SCIP_OKAY;
}
