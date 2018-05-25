/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2018 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not visit scip.zib.de.         */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   benderscut_nogood.c
 * @brief  Generates a no good cut for integer solutions that are infeasible for the subproblems
 * @author Stephen J. Maher
 *
 * The no-good cut is generated for the Benders' decomposition master problem if an integer solution is identified as
 * infeasible in at least one CIP subproblems. The no-good cut is required, because the classical Benders' decomposition
 * feasibility cuts (see benderscut_feas.c) will only cut off the solution \f$\bar{x}\f$ if the LP relaxation of the CIP
 * is infeasible.
 *
 * Consider a Benders' decomposition subproblem that is a CIP and it infeasible. Let \f$S_{r}\f$ be the set of indices
 * for master problem variables that are 1 in \f$\bar{x}\f$. The no-good cut is given by
 *
 * \f[
 * 1 \leq \sum_{i \in S_{r}}(1 - x_{i}) + \sum_{i \notin S_{r}}x_{i}
 * \f]
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "scip/benderscut_nogood.h"
#include "scip/pub_benders.h"
#include "scip/pub_benderscut.h"

#include "scip/cons_linear.h"
#include "scip/pub_lp.h"


#define BENDERSCUT_NAME             "nogood"
#define BENDERSCUT_DESC             "no good Benders' decomposition integer cut"
#define BENDERSCUT_PRIORITY       500
#define BENDERSCUT_LPCUT        FALSE



#define SCIP_DEFAULT_ADDCUTS             FALSE  /** Should cuts be generated, instead of constraints */

/*
 * Data structures
 */

/** Benders' decomposition cuts data */
struct SCIP_BenderscutData
{
   SCIP_BENDERS*         benders;            /**< the Benders' decomposition data structure */
   int                   curriter;           /**< the current Benders' decomposition subproblem solve iteration */
   SCIP_Bool             addcuts;            /**< should cuts be generated instead of constraints */
   SCIP_Bool             cutadded;           /**< has a cut been added in the current iteration. Only one cut per round */
};


/*
 * Local methods
 */

/** compute no good cut */
static
SCIP_RETCODE computeNogoodCut(
   SCIP*                 masterprob,         /**< the SCIP instance of the master problem */
   SCIP_BENDERS*         benders,            /**< the benders' decomposition structure */
   SCIP_SOL*             sol,                /**< primal CIP solution */
   SCIP_CONS*            cons,               /**< the constraint for the generated cut, can be NULL */
   SCIP_ROW*             row,                /**< the row for the generated cut, can be NULL */
   SCIP_Bool             addcut              /**< indicates whether a cut is created instead of a constraint */
   )
{
   SCIP_VAR** vars;
   int nvars;
   SCIP_Real lhs;
   int i;
#ifndef NDEBUG
   SCIP_Real verifycons;
#endif

   assert(masterprob != NULL);
   assert(benders != NULL);
   assert(cons != NULL || addcut);
   assert(row != NULL || !addcut);

   nvars = SCIPgetNVars(masterprob);
   vars = SCIPgetVars(masterprob);

   /* adding the subproblem objective function value to the lhs */
   if( addcut )
      lhs = SCIProwGetLhs(row);
   else
      lhs = SCIPgetLhsLinear(masterprob, cons);

   /* adding the violation to the lhs */
   lhs += 1.0;

   /* looping over all master problem variables to update the coefficients in the computed cut. */
   for( i = 0; i < nvars; i++ )
   {
      SCIP_Real coef;

      if( !SCIPvarIsBinary(vars[i]) )
         continue;

      /* if the variable is on its upper bound, then the subproblem objective value is added to the cut */
      if( SCIPisFeasEQ(masterprob, SCIPgetSolVal(masterprob, sol, vars[i]), 1.0) )
      {
         coef = -1.0;
         lhs -= 1.0;
      }
      else
         coef = 1.0;

      /* adding the variable to the cut. The coefficient is the subproblem objective value */
      if( addcut )
      {
         SCIP_CALL( SCIPaddVarToRow(masterprob, row, vars[i], coef) );
      }
      else
      {
         SCIP_CALL( SCIPaddCoefLinear(masterprob, cons, vars[i], coef) );
      }
   }

   /* Update the lhs of the cut */
   if( addcut )
   {
      SCIP_CALL( SCIPchgRowLhs(masterprob, row, lhs) );
   }
   else
   {
      SCIP_CALL( SCIPchgLhsLinear(masterprob, cons, lhs) );
   }


#ifndef NDEBUG
   if( addcut )
      verifycons = SCIPgetRowSolActivity(masterprob, row, sol);
   else
      verifycons = SCIPgetActivityLinear(masterprob, cons, sol);
#endif

   assert(SCIPisFeasEQ(masterprob, verifycons, lhs - 1));

   return SCIP_OKAY;
}



/** generates and applies Benders' cuts */
static
SCIP_RETCODE generateAndApplyBendersNogoodCut(
   SCIP*                 masterprob,         /**< the SCIP instance of the master problem */
   SCIP_BENDERS*         benders,            /**< the benders' decomposition */
   SCIP_BENDERSCUT*      benderscut,         /**< the benders' decomposition cut method */
   SCIP_SOL*             sol,                /**< primal CIP solution */
   SCIP_BENDERSENFOTYPE  type,               /**< the enforcement type calling this function */
   SCIP_RESULT*          result              /**< the result from solving the subproblems */
   )
{
   SCIP_BENDERSCUTDATA* benderscutdata;
   SCIP_CONSHDLR* consbenders;
   SCIP_CONS* cons;
   SCIP_ROW* row;
   char cutname[SCIP_MAXSTRLEN];
   SCIP_Bool addcut;

   assert(masterprob != NULL);
   assert(benders != NULL);
   assert(benderscut != NULL);
   assert(result != NULL);

   row = NULL;
   cons = NULL;

   /* retrieving the Benders' cut data */
   benderscutdata = SCIPbenderscutGetData(benderscut);

   /* if the cuts are generated prior to the solving stage, then rows can not be generated. So constraints must be
    * added to the master problem.
    */
   if( SCIPgetStage(masterprob) < SCIP_STAGE_INITSOLVE )
      addcut = FALSE;
   else
      addcut = benderscutdata->addcuts;

   /* retrieving the Benders' decomposition constraint handler */
   consbenders = SCIPfindConshdlr(masterprob, "benders");

   /* setting the name of the generated cut */
   (void) SCIPsnprintf(cutname, SCIP_MAXSTRLEN, "nogoodcut_%d", SCIPbenderscutGetNFound(benderscut) );

   /* creating an empty row or constraint for the Benders' cut */
   if( addcut )
   {
      SCIP_CALL( SCIPcreateEmptyRowCons(masterprob, &row, consbenders, cutname, 0.0, SCIPinfinity(masterprob), FALSE,
            FALSE, TRUE) );
   }
   else
   {
      SCIP_CALL( SCIPcreateConsBasicLinear(masterprob, &cons, cutname, 0, NULL, NULL, 0.0, SCIPinfinity(masterprob)) );
      SCIP_CALL( SCIPsetConsDynamic(masterprob, cons, TRUE) );
      SCIP_CALL( SCIPsetConsRemovable(masterprob, cons, TRUE) );
   }

   /* computing the coefficients of the optimality cut */
   SCIP_CALL( computeNogoodCut(masterprob, benders, sol, cons, row, addcut) );

   /* adding the constraint to the master problem */
   if( addcut )
   {
      SCIP_Bool infeasible;

      if( type == SCIP_BENDERSENFOTYPE_LP || type == SCIP_BENDERSENFOTYPE_RELAX )
      {
         SCIP_CALL( SCIPaddRow(masterprob, row, FALSE, &infeasible) );
         assert(!infeasible);
      }
      else
      {
         assert(type == SCIP_BENDERSENFOTYPE_CHECK || type == SCIP_BENDERSENFOTYPE_PSEUDO);
         SCIP_CALL( SCIPaddPoolCut(masterprob, row) );
      }

#ifdef SCIP_DEBUG
      SCIP_CALL( SCIPprintRow(masterprob, row, NULL) );
      SCIPinfoMessage(masterprob, NULL, ";\n");
#endif

      /* release the row */
      SCIP_CALL( SCIPreleaseRow(masterprob, &row) );

      (*result) = SCIP_SEPARATED;
   }
   else
   {
      SCIP_CALL( SCIPaddCons(masterprob, cons) );

      SCIPdebugPrintCons(masterprob, cons, NULL);

      SCIP_CALL( SCIPreleaseCons(masterprob, &cons) );

      (*result) = SCIP_CONSADDED;
   }

   /* updating the cut added flag */
   benderscutdata->cutadded = TRUE;

   return SCIP_OKAY;
}

/*
 * Callback methods of Benders' decomposition cuts
 */

/** destructor of Benders' decomposition cuts to free user data (called when SCIP is exiting) */
static
SCIP_DECL_BENDERSCUTFREE(benderscutFreeNogood)
{  /*lint --e{715}*/
   SCIP_BENDERSCUTDATA* benderscutdata;

   assert( benderscut != NULL );
   assert( strcmp(SCIPbenderscutGetName(benderscut), BENDERSCUT_NAME) == 0 );

   /* free Benders' cut data */
   benderscutdata = SCIPbenderscutGetData(benderscut);
   assert( benderscutdata != NULL );

   SCIPfreeBlockMemory(scip, &benderscutdata);

   SCIPbenderscutSetData(benderscut, NULL);

   return SCIP_OKAY;
}


/** execution method of Benders' decomposition cuts */
static
SCIP_DECL_BENDERSCUTEXEC(benderscutExecNogood)
{  /*lint --e{715}*/
   SCIP_BENDERSCUTDATA* benderscutdata;

   assert(scip != NULL);
   assert(benders != NULL);
   assert(benderscut != NULL);
   assert(result != NULL);

   benderscutdata = SCIPbenderscutGetData(benderscut);
   assert(benderscutdata != NULL);

   /* if the curriter is less than the number of Benders' decomposition calls, then we are in a new round.
    * So the cutadded flag must be set to FALSE
    */
   if( benderscutdata->curriter < SCIPbendersGetNCalls(benders) )
   {
      benderscutdata->curriter = SCIPbendersGetNCalls(benders);
      benderscutdata->cutadded = FALSE;
   }

   /* if a cut has been added in this Benders' decomposition call, then no more must be added */
   if( benderscutdata->cutadded )
      return SCIP_OKAY;

   /* it is only possible to generate the no-good cut for pure binary master problems */
   if( SCIPgetNBinVars(scip) != (SCIPgetNVars(scip) - SCIPbendersGetNSubproblems(benders)) )
   {
      SCIPinfoMessage(scip, NULL, "The no-good cuts can only be applied to problems with a pure binary master problem. "
         "The no-good Benders' decomposition cuts will be disabled.\n");

      SCIPbenderscutSetEnabled(benderscut, FALSE);

      return SCIP_OKAY;
   }

   /* We can not rely on complete recourse for the subproblems. As such, the subproblems may be feasible for the LP, but
    * infeasible for the IP. As such, if one subproblem is infeasible, then a no good cut is generated.
    */
   if( SCIPgetStatus(SCIPbendersSubproblem(benders, probnumber)) == SCIP_STATUS_INFEASIBLE )
   {
      /* generating a cut */
      SCIP_CALL( generateAndApplyBendersNogoodCut(scip, benders, benderscut, sol, type, result) );
   }

   return SCIP_OKAY;
}


/*
 * Benders' decomposition cuts specific interface methods
 */

/** creates the nogood Benders' decomposition cuts and includes it in SCIP */
SCIP_RETCODE SCIPincludeBenderscutNogood(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   )
{
   SCIP_BENDERSCUTDATA* benderscutdata;
   SCIP_BENDERSCUT* benderscut;
   char paramname[SCIP_MAXSTRLEN];

   assert(benders != NULL);

   /* create nogood Benders' decomposition cuts data */
   SCIP_CALL( SCIPallocBlockMemory(scip, &benderscutdata) );
   benderscutdata->benders = benders;
   benderscutdata->curriter = -1;
   benderscutdata->cutadded = FALSE;

   benderscut = NULL;

   /* include Benders' decomposition cuts */
   SCIP_CALL( SCIPincludeBenderscutBasic(scip, benders, &benderscut, BENDERSCUT_NAME, BENDERSCUT_DESC,
         BENDERSCUT_PRIORITY, BENDERSCUT_LPCUT, benderscutExecNogood, benderscutdata) );

   assert(benderscut != NULL);

   /* set non fundamental callbacks via setter functions */
   SCIP_CALL( SCIPsetBenderscutFree(scip, benderscut, benderscutFreeNogood) );

   /* add nogood Benders' decomposition cuts parameters */
   (void) SCIPsnprintf(paramname, SCIP_MAXSTRLEN, "benders/%s/benderscut/%s/addcuts",
      SCIPbendersGetName(benders), BENDERSCUT_NAME);
   SCIP_CALL( SCIPaddBoolParam(scip, paramname,
         "should cuts be generated and added to the cutpool instead of global constraints directly added to the problem.",
         &benderscutdata->addcuts, FALSE, SCIP_DEFAULT_ADDCUTS, NULL, NULL) );

   return SCIP_OKAY;
}
