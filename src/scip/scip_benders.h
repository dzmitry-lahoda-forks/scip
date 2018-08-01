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

/**@file   scip_benders.h
 * @ingroup PUBLICCOREAPI
 * @brief  public methods for Benders decomposition
 * @author Tobias Achterberg
 * @author Timo Berthold
 * @author Thorsten Koch
 * @author Alexander Martin
 * @author Marc Pfetsch
 * @author Kati Wolter
 * @author Gregor Hendel
 * @author Robert Lion Gottwald
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_SCIP_BENDERS_H__
#define __SCIP_SCIP_BENDERS_H__


#include "scip/def.h"
#include "scip/type_benderscut.h"
#include "scip/type_benders.h"
#include "scip/type_cons.h"
#include "scip/type_lp.h"
#include "scip/type_misc.h"
#include "scip/type_result.h"
#include "scip/type_retcode.h"
#include "scip/type_scip.h"
#include "scip/type_sol.h"
#include "scip/type_var.h"

/* In debug mode, we include the SCIP's structure in scip.c, such that no one can access
 * this structure except the interface methods in scip.c.
 * In optimized mode, the structure is included in scip.h, because some of the methods
 * are implemented as defines for performance reasons (e.g. the numerical comparisons).
 * Additionally, the internal "set.h" is included, such that the defines in set.h are
 * available in optimized mode.
 */
#ifdef NDEBUG
#include "scip/struct_scip.h"
#include "scip/struct_stat.h"
#include "scip/set.h"
#include "scip/tree.h"
#include "scip/misc.h"
#include "scip/var.h"
#include "scip/cons.h"
#include "scip/solve.h"
#include "scip/debug.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**@addtogroup PublicBendersMethods
 *
 * @{
 */

/** creates a Benders' decomposition and includes it in SCIP
 *
 *  To use the Benders' decomposition for solving a problem, it first has to be activated with a call to SCIPactivateBenders().
 *  This should be done during the problem creation stage.
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 *
 *  @note method has all Benders' decomposition callbacks as arguments and is thus changed every time a new callback is
 *        added in future releases; consider using SCIPincludeBendersBasic() and setter functions
 *        if you seek for a method which is less likely to change in future releases
 */
EXTERN
SCIP_RETCODE SCIPincludeBenders(
   SCIP*                 scip,               /**< SCIP data structure */
   const char*           name,               /**< name of Benders' decomposition */
   const char*           desc,               /**< description of Benders' decomposition */
   int                   priority,           /**< priority of the Benders' decomposition */
   SCIP_Bool             cutlp,              /**< should Benders' cuts be generated for LP solutions */
   SCIP_Bool             cutpseudo,          /**< should Benders' cuts be generated for pseudo solutions */
   SCIP_Bool             cutrelax,           /**< should Benders' cuts be generated for relaxation solutions */
   SCIP_Bool             shareauxvars,       /**< should this Benders' use the highest priority Benders aux vars */
   SCIP_DECL_BENDERSCOPY ((*benderscopy)),   /**< copy method of Benders' decomposition or NULL if you don't want to copy your plugin into sub-SCIPs */
   SCIP_DECL_BENDERSFREE ((*bendersfree)),   /**< destructor of Benders' decomposition */
   SCIP_DECL_BENDERSINIT ((*bendersinit)),   /**< initialize Benders' decomposition */
   SCIP_DECL_BENDERSEXIT ((*bendersexit)),   /**< deinitialize Benders' decomposition */
   SCIP_DECL_BENDERSINITPRE((*bendersinitpre)),/**< presolving initialization method for Benders' decomposition */
   SCIP_DECL_BENDERSEXITPRE((*bendersexitpre)),/**< presolving deinitialization method for Benders' decomposition */
   SCIP_DECL_BENDERSINITSOL((*bendersinitsol)),/**< solving process initialization method of Benders' decomposition */
   SCIP_DECL_BENDERSEXITSOL((*bendersexitsol)),/**< solving process deinitialization method of Benders' decomposition */
   SCIP_DECL_BENDERSGETVAR((*bendersgetvar)),/**< returns the master variable for a given subproblem variable */
   SCIP_DECL_BENDERSCREATESUB((*benderscreatesub)),/**< creates a Benders' decomposition subproblem */
   SCIP_DECL_BENDERSPRESUBSOLVE((*benderspresubsolve)),/**< the execution method of the Benders' decomposition algorithm */
   SCIP_DECL_BENDERSSOLVESUBCONVEX((*benderssolvesubconvex)),/**< the solving method for convex Benders' decomposition subproblems */
   SCIP_DECL_BENDERSSOLVESUB((*benderssolvesub)),/**< the solving method for the Benders' decomposition subproblems */
   SCIP_DECL_BENDERSPOSTSOLVE((*benderspostsolve)),/**< called after the subproblems are solved. */
   SCIP_DECL_BENDERSFREESUB((*bendersfreesub)),/**< the freeing method for the Benders' decomposition subproblems */
   SCIP_BENDERSDATA*     bendersdata         /**< Benders' decomposition data */
   );

/** creates a Benders' decomposition and includes it in SCIP with all non-fundamental callbacks set to NULL
 *
 *  If needed, the non-fundamental callbacks can be added afterwards via setter functions SCIPsetBendersCopy(),
 *  SCIPsetBendersFree(), SCIPsetBendersInity(), SCIPsetBendersExit(), SCIPsetBendersInitsol(), SCIPsetBendersExitsol(),
 *  SCIPsetBendersFarkas().
 *
 *  To use the Benders' decomposition for solving a problem, it first has to be activated with a call to SCIPactivateBenders().
 *  This should be done during the problem creation stage.
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 *
 *  @note if you want to set all callbacks with a single method call, consider using SCIPincludeBenders() instead
 */
EXTERN
SCIP_RETCODE SCIPincludeBendersBasic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS**        bendersptr,         /**< reference to a benders, or NULL */
   const char*           name,               /**< name of Benders' decomposition */
   const char*           desc,               /**< description of Benders' decomposition */
   int                   priority,           /**< priority of the Benders' decomposition */
   SCIP_Bool             cutlp,              /**< should Benders' cuts be generated for LP solutions */
   SCIP_Bool             cutpseudo,          /**< should Benders' cuts be generated for pseudo solutions */
   SCIP_Bool             cutrelax,           /**< should Benders' cuts be generated for relaxation solutions */
   SCIP_Bool             shareauxvars,       /**< should this Benders' use the highest priority Benders aux vars */
   SCIP_DECL_BENDERSGETVAR((*bendersgetvar)),/**< returns the master variable for a given subproblem variable */
   SCIP_DECL_BENDERSCREATESUB((*benderscreatesub)),/**< creates a Benders' decomposition subproblem */
   SCIP_BENDERSDATA*     bendersdata         /**< Benders' decomposition data */
   );

/** sets copy method of benders
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 */
EXTERN
SCIP_RETCODE SCIPsetBendersCopy(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSCOPY((*benderscopy))     /**< copy method of Benders' decomposition or NULL if you don't want to copy your plugin into sub-SCIPs */
   );

/** sets destructor method of benders
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 */
EXTERN
SCIP_RETCODE SCIPsetBendersFree(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSFREE((*bendersfree))     /**< destructor of Benders' decomposition */
   );

/** sets initialization method of benders
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 */
EXTERN
SCIP_RETCODE SCIPsetBendersInit(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSINIT ((*bendersinit))    /**< initialize Benders' decomposition */
   );

/** sets deinitialization method of benders
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 */
EXTERN
SCIP_RETCODE SCIPsetBendersExit(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSEXIT ((*bendersexit))    /**< deinitialize Benders' decomposition */
   );

/** sets presolving initialization method of benders
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 */
EXTERN
SCIP_RETCODE SCIPsetBendersInitpre(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSINITPRE((*bendersinitpre))/**< presolving initialization method of Benders' decomposition */
   );

/** sets presolving deinitialization method of benders
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 */
EXTERN
SCIP_RETCODE SCIPsetBendersExitpre(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSEXITPRE((*bendersexitpre))/**< presolving deinitialization method of Benders' decomposition */
   );

/** sets solving process initialization method of benders
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 */
EXTERN
SCIP_RETCODE SCIPsetBendersInitsol(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSINITSOL((*bendersinitsol))/**< solving process initialization method of Benders' decomposition */
   );

/** sets solving process deinitialization method of benders
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 */
EXTERN
SCIP_RETCODE SCIPsetBendersExitsol(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSEXITSOL((*bendersexitsol))/**< solving process deinitialization method of Benders' decomposition */
   );

/** sets the method called prior to solving the subproblems for benders
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 */
EXTERN
SCIP_RETCODE SCIPsetBendersPresubsolve(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSPRESUBSOLVE((*benderspresubsolve))/**< method called prior to solving the subproblems */
   );

/** sets the subproblem solving and freeing methods for Benders' decomposition
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 */
EXTERN
SCIP_RETCODE SCIPsetBendersSolveAndFreesub(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSSOLVESUBCONVEX((*benderssolvesubconvex)),/**< the solving method for convex Benders' decomposition subproblems */
   SCIP_DECL_BENDERSSOLVESUB((*benderssolvesub)),/**< solving method for a Benders' decomposition subproblem */
   SCIP_DECL_BENDERSFREESUB((*bendersfreesub))/**< the subproblem freeing method for Benders' decomposition */
   );

/** sets the post solving methods for benders
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 */
EXTERN
SCIP_RETCODE SCIPsetBendersPostsolve(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_DECL_BENDERSPOSTSOLVE((*benderspostsolve))/**< solving process deinitialization method of Benders' decomposition */
   );

/** returns the Benders' decomposition of the given name, or NULL if not existing */
EXTERN
SCIP_BENDERS* SCIPfindBenders(
   SCIP*                 scip,               /**< SCIP data structure */
   const char*           name                /**< name of Benders' decomposition */
   );

/** returns the array of currently available Benders' decomposition; active Benders' decomposition are in the first
 * slots of the array
 */
EXTERN
SCIP_BENDERS** SCIPgetBenders(
   SCIP*                 scip                /**< SCIP data structure */
   );

/** returns the number of currently available Benders' decomposition */
EXTERN
int SCIPgetNBenders(
   SCIP*                 scip                /**< SCIP data structure */
   );

/** returns the number of currently active Benders' decomposition */
int SCIPgetNActiveBenders(
   SCIP*                 scip                /**< SCIP data structure */
   );

/** activates the Benders' decomposition to be used for the current problem
 *
 *  This method should be called during the problem creation stage for all pricers that are necessary to solve
 *  the problem model.
 *
 *  @note The Benders' decompositions are automatically deactivated when the problem is freed.
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_PROBLEM
 */
SCIP_RETCODE SCIPactivateBenders(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< the Benders' decomposition structure */
   int                   nsubproblems        /**< the number of subproblems in the Benders' decomposition */
   );

/** deactivates the Benders' decomposition
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_PROBLEM
 *       - \ref SCIP_STAGE_EXITSOLVE
 */
SCIP_RETCODE SCIPdeactivateBenders(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders             /**< the Benders' decomposition structure */
   );

/** sets the priority of a Benders' decomposition */
EXTERN
void SCIPsetBendersPriority(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   int                   priority            /**< new priority of the Benders' decomposition */
   );

/** calls the exec method of Benders' decomposition to solve the subproblems
 *
 *  The checkint flag indicates whether integer feasibility can be assumed. If it is not assumed, i.e. checkint ==
 *  FALSE, then only the convex relaxations of the subproblems are solved. If integer feasibility is assumed, i.e.
 *  checkint == TRUE, then the convex relaxations and the full CIP are solved to generate Benders' cuts and check
 *  solution feasibility.
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVING
 *       - \ref SCIP_STAGE_EXITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVED
 *       - \ref SCIP_STAGE_INITSOLVE
 *       - \ref SCIP_STAGE_SOLVING
 *       - \ref SCIP_STAGE_SOLVED
 */
EXTERN
SCIP_RETCODE SCIPsolveBendersSubproblems(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SOL*             sol,                /**< primal CIP solution, can be NULL */
   SCIP_RESULT*          result,             /**< result of the pricing process */
   SCIP_Bool*            infeasible,         /**< is the master problem infeasible with respect to the Benders' cuts? */
   SCIP_Bool*            auxviol,            /**< set to TRUE only if the solution is feasible but the aux vars are violated */
   SCIP_BENDERSENFOTYPE  type,               /**< the type of solution being enforced */
   SCIP_Bool             checkint            /**< should the integer solution be checked by the subproblems */
   );

/** returns the master problem variable for the given subproblem variable
 *
 *  This function is used as part of the cut generation process.
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVING
 *       - \ref SCIP_STAGE_EXITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVED
 *       - \ref SCIP_STAGE_INITSOLVE
 *       - \ref SCIP_STAGE_SOLVING
 *       - \ref SCIP_STAGE_SOLVED
 */
EXTERN
SCIP_RETCODE SCIPgetBendersMasterVar(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_VAR*             var,                /**< the subproblem variable */
   SCIP_VAR**            mappedvar           /**< pointer to store the master variable that var is mapped to */
   );

/** returns the subproblem problem variable for the given master variable
 *
 *  This function is used as part of the cut generation process.
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVING
 *       - \ref SCIP_STAGE_EXITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVED
 *       - \ref SCIP_STAGE_INITSOLVE
 *       - \ref SCIP_STAGE_SOLVING
 *       - \ref SCIP_STAGE_SOLVED
 */
EXTERN
SCIP_RETCODE SCIPgetBendersSubproblemVar(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_VAR*             var,                /**< the master variable */
   SCIP_VAR**            mappedvar,          /**< pointer to store the subproblem variable that var is mapped to */
   int                   probnumber          /**< the subproblem number */
   );

/** returns the number of subproblems that are stored in the given Benders' decomposition
 *
 *  @return the number of subproblems in the Benders' decomposition
 */
EXTERN
int SCIPgetBendersNSubproblems(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders             /**< Benders' decomposition */
   );

/** registers the Benders' decomposition subproblem with the Benders' decomposition struct.
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 */
EXTERN
SCIP_RETCODE SCIPaddBendersSubproblem(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP*                 subproblem          /**< Benders' decomposition subproblem */
   );

/** calls the generic subproblem setup method for a Benders' decomposition subproblem
 *
 *  This is called if the user requires to solve the Benders' decomposition subproblem separately from the main Benders'
 *  solving loop. This could be in the case of enhancement techniques.
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVING
 *       - \ref SCIP_STAGE_EXITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVED
 *       - \ref SCIP_STAGE_INITSOLVE
 *       - \ref SCIP_STAGE_SOLVING
 *       - \ref SCIP_STAGE_SOLVED
 */
EXTERN
SCIP_RETCODE SCIPsetupBendersSubproblem(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< the Benders' decomposition data structure */
   SCIP_SOL*             sol,                /**< primal solution used to setup tht problem, NULL for LP solution */
   int                   probnumber          /**< the subproblem number */
   );

/** calls the solving method for a single Benders' decomposition subproblem
 *
 *  The method either calls the users solve subproblem method or calls the generic method. In the case of the generic
 *  method, the user must set up the subproblem prior to calling this method.
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVING
 *       - \ref SCIP_STAGE_EXITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVED
 *       - \ref SCIP_STAGE_INITSOLVE
 *       - \ref SCIP_STAGE_SOLVING
 *       - \ref SCIP_STAGE_SOLVED
 */
EXTERN
SCIP_RETCODE SCIPsolveBendersSubproblem(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_SOL*             sol,                /**< primal CIP solution, can be NULL for the current LP/Pseudo solution */
   int                   probnumber,         /**< the subproblem number */
   SCIP_Bool*            infeasible,         /**< returns whether the current subproblem is infeasible */
   SCIP_BENDERSENFOTYPE  type,               /**< the enforcement type calling this function */
   SCIP_Bool             solvecip,           /**< directly solve the CIP subproblem */
   SCIP_Real*            objective           /**< the objective function value of the subproblem, can be NULL */
   );

/** frees the subproblem after calling the solve subproblem method
 *
 *  This will either call the user defined free
 *  subproblem callback for Benders' decomposition or the default freeing methods. In the default case, if the
 *  subproblem is an LP, then SCIPendProbing is called. If the subproblem is a MIP, then SCIPfreeTransform is called.
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVING
 *       - \ref SCIP_STAGE_EXITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVED
 *       - \ref SCIP_STAGE_INITSOLVE
 *       - \ref SCIP_STAGE_SOLVING
 *       - \ref SCIP_STAGE_SOLVED
 *       - \ref SCIP_STAGE_EXITSOLVE
 *       - \ref SCIP_STAGE_FREETRANS
 */
EXTERN
SCIP_RETCODE SCIPfreeBendersSubproblem(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   int                   probnumber          /**< the subproblem number */
   );

/** checks the optimality of a Benders' decomposition subproblem by comparing the objective function value against the
 *  value of the corresponding auxiliary variable
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_PRESOLVING
 *       - \ref SCIP_STAGE_SOLVING
 *       - \ref SCIP_STAGE_SOLVED
 *
 *  @pre This method can be called if requested subproblem is in one of the following stages:
 *       - \ref SCIP_STAGE_SOLVING
 *       - \ref SCIP_STAGE_SOLVED
 */
EXTERN
SCIP_RETCODE SCIPcheckBendersSubproblemOptimality(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< the benders' decomposition structure */
   SCIP_SOL*             sol,                /**< primal CIP solution, can be NULL for the current LP solution */
   int                   probnumber,         /**< the number of the pricing problem */
   SCIP_Bool*            optimal             /**< flag to indicate whether the current subproblem is optimal for the master */
   );

/** returns the value of the auxiliary variable for a given subproblem */
EXTERN
SCIP_Real SCIPgetBendersAuxiliaryVarVal(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< the benders' decomposition structure */
   SCIP_SOL*             sol,                /**< primal CIP solution, can be NULL for the current LP solution */
   int                   probnumber          /**< the number of the pricing problem */
   );

/** solves an independent subproblem to identify its lower bound and updates the lower bound of the corresponding
 *  auxiliary variable
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVING
 *       - \ref SCIP_STAGE_EXITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVED
 *       - \ref SCIP_STAGE_INITSOLVE
 *       - \ref SCIP_STAGE_SOLVING
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 */
SCIP_RETCODE SCIPcomputeBendersSubproblemLowerbound(
   SCIP*                 scip,               /**< the SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   int                   probnumber,         /**< the subproblem to be evaluated */
   SCIP_Real*            lowerbound,         /**< the lowerbound for the subproblem */
   SCIP_Bool*            infeasible          /**< was the subproblem found to be infeasible? */
   );

/** merges a subproblem into the master problem.
 *
 *  This process just adds a copy of the subproblem variables and constraints to the master problem, but keeps the
 *  subproblem stored in the Benders' decomposition data structure.  The reason for keeping the subproblem available is
 *  for when it is queried for solutions after the problem is solved.
 *
 *  Once the subproblem is merged into the master problem, then the subproblem is flagged as disabled. This means that
 *  it will not be solved in the subsequent subproblem solving loops.
 *
 *  The associated auxiliary variables are kept in the master problem. The objective function of the merged subproblem
 *  is added as an underestimator constraint.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVING
 *       - \ref SCIP_STAGE_EXITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVED
 *       - \ref SCIP_STAGE_INITSOLVE
 *       - \ref SCIP_STAGE_SOLVING
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 */
EXTERN
SCIP_RETCODE SCIPmergeBendersSubproblemIntoMaster(
   SCIP*                 scip,               /**< the SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_HASHMAP*         varmap,             /**< a hashmap to store the mapping of subproblem variables corresponding
                                              *   to the newly created master variables, or NULL */
   SCIP_HASHMAP*         consmap,            /**< a hashmap to store the mapping of subproblem constraints to the
                                                  corresponding newly created constraints, or NULL */
   int                   probnumber          /**< the number of the subproblem that will be merged into the master problem*/
   );

/** adds constraints, which are Benders' cuts, that are generated in LNS heuristics to the main SCIP instance.
 *
 *  This method is required because constraints must be added at the correct stage in the solving process. This is
 *  typically in the enforcement of the LP or relaxation solutions. This function can be called from the Benders'
 *  decomposition constraint handler to add the transferred constraints.
 *
 *  A success flag is returned if there were any constraints transferred to the main SCIP instance.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVING
 *       - \ref SCIP_STAGE_EXITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVED
 *       - \ref SCIP_STAGE_INITSOLVE
 *       - \ref SCIP_STAGE_SOLVING
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 */
EXTERN
SCIP_RETCODE SCIPaddBendersTransferConss(
   SCIP*                 scip,               /**< the SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_Bool*            success             /**< flag to indicate whether any constraints are added to the main SCIP */
   );

/* @} */

/**@addtogroup PublicBenderscutsMethods
 *
 * @{
 */

/** creates a Benders' cut algorithms and includes it in the associated Benders' decomposition
 *
 *  This should be called from the SCIPincludeBendersXyz for the associated Benders' decomposition. It is only possible
 *  to include a Benders' cut algorithm if a Benders' decomposition has already been included
 *  This should be done during the problem creation stage.
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 *
 *  @note method has all Benders' decomposition callbacks as arguments and is thus changed every time a new callback is
 *        added in future releases; consider using SCIPincludeBendersBasic() and setter functions
 *        if you seek for a method which is less likely to change in future releases
 */
EXTERN
SCIP_RETCODE SCIPincludeBenderscut(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   const char*           name,               /**< name of Benders' decomposition cuts */
   const char*           desc,               /**< description of Benders' decomposition cuts */
   int                   priority,           /**< priority of the Benders' decomposition cuts */
   SCIP_Bool             islpcut,            /**< indicates whether the cut is generated from the LP solution */
   SCIP_DECL_BENDERSCUTCOPY((*benderscutcopy)),/**< copy method of Benders' decomposition cuts or NULL if you don't want to copy your plugin into sub-SCIPs */
   SCIP_DECL_BENDERSCUTFREE((*benderscutfree)),/**< destructor of Benders' decomposition cuts */
   SCIP_DECL_BENDERSCUTINIT((*benderscutinit)),/**< initialize Benders' decomposition cuts */
   SCIP_DECL_BENDERSCUTEXIT((*benderscutexit)),/**< deinitialize Benders' decomposition cuts */
   SCIP_DECL_BENDERSCUTINITSOL((*benderscutinitsol)),/**< solving process initialization method of Benders' decomposition cuts */
   SCIP_DECL_BENDERSCUTEXITSOL((*benderscutexitsol)),/**< solving process deinitialization method of Benders' decomposition cuts */
   SCIP_DECL_BENDERSCUTEXEC((*benderscutexec)),/**< execution method of Benders' decomposition cuts */
   SCIP_BENDERSCUTDATA*  benderscutdata      /**< Benders' decomposition cuts data */
   );

/** creates a Benders' cut and includes it an associated Benders' decomposition with all non-fundamental callbacks set to NULL
 *
 *  If needed, the non-fundamental callbacks can be added afterwards via setter functions SCIPsetBenderscutCopy(),
 *  SCIPsetBenderscutFree(), SCIPsetBenderscutInit(), SCIPsetBenderscutExit(), SCIPsetBenderscutInitsol(),
 *  SCIPsetBenderscutExitsol().
 *
 *  This should be done during the problem creation stage.
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 *
 *  @note if you want to set all callbacks with a single method call, consider using SCIPincludeBenders() instead
 */
EXTERN
SCIP_RETCODE SCIPincludeBenderscutBasic(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERS*         benders,            /**< Benders' decomposition */
   SCIP_BENDERSCUT**     benderscutptr,      /**< reference to a Benders' decomposition cut, or NULL */
   const char*           name,               /**< name of Benders' decomposition */
   const char*           desc,               /**< description of Benders' decomposition */
   int                   priority,           /**< priority of the Benders' decomposition */
   SCIP_Bool             islpcut,            /**< indicates whether the cut is generated from the LP solution */
   SCIP_DECL_BENDERSCUTEXEC((*benderscutexec)),/**< the execution method of the Benders' cut algorithm */
   SCIP_BENDERSCUTDATA*  benderscutdata      /**< Benders' cut data */
   );

/** sets copy method of Benders' decomposition cut
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 */
EXTERN
SCIP_RETCODE SCIPsetBenderscutCopy(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' decomposition cut */
   SCIP_DECL_BENDERSCUTCOPY((*benderscutcopy))/**< copy method of benderscut or NULL if you don't want to copy your plugin into sub-SCIPs */
   );

/** sets destructor method of benderscut
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 */
EXTERN
SCIP_RETCODE SCIPsetBenderscutFree(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERSCUT*      benderscut,         /**< benderscut */
   SCIP_DECL_BENDERSCUTFREE((*benderscutfree))/**< destructor of benderscut */
   );

/** sets initialization method of benderscut
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 */
EXTERN
SCIP_RETCODE SCIPsetBenderscutInit(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERSCUT*      benderscut,         /**< benderscut */
   SCIP_DECL_BENDERSCUTINIT((*benderscutinit))/**< initialize benderscut */
   );

/** sets deinitialization method of benderscut
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 */
EXTERN
SCIP_RETCODE SCIPsetBenderscutExit(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERSCUT*      benderscut,         /**< benderscut */
   SCIP_DECL_BENDERSCUTEXIT((*benderscutexit))/**< deinitialize benderscut */
   );

/** sets solving process initialization method of benderscut
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 */
EXTERN
SCIP_RETCODE SCIPsetBenderscutInitsol(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERSCUT*      benderscut,         /**< benderscut */
   SCIP_DECL_BENDERSCUTINITSOL((*benderscutinitsol))/**< solving process initialization method of benderscut */
   );

/** sets solving process deinitialization method of benderscut
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 */
EXTERN
SCIP_RETCODE SCIPsetBenderscutExitsol(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERSCUT*      benderscut,         /**< benderscut */
   SCIP_DECL_BENDERSCUTEXITSOL((*benderscutexitsol))/**< solving process deinitialization method of benderscut */
   );

/** sets the priority of a Benders' decomposition cut algorithm
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INIT
 *       - \ref SCIP_STAGE_PROBLEM
 */
EXTERN
SCIP_RETCODE SCIPsetBenderscutPriority(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_BENDERSCUT*      benderscut,         /**< benderscut */
   int                   priority            /**< new priority of the Benders' decomposition */
   );

/** adds the generated constraint to the Benders' decomposition cut storage
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVING
 *       - \ref SCIP_STAGE_EXITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVED
 *       - \ref SCIP_STAGE_INITSOLVE
 *       - \ref SCIP_STAGE_SOLVING
 */
EXTERN
SCIP_RETCODE SCIPstoreBenderscutCons(
   SCIP*                 scip,               /**< the SCIP data structure */
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' decomposition cuts */
   SCIP_CONS*            cons                /**< the constraint to be added to the Benders' cut storage */
   );

/** adds the generated cuts to the Benders' cut storage
 *
 *  @return \ref SCIP_OKAY is returned if everything worked. Otherwise a suitable error code is passed. See \ref
 *          SCIP_Retcode "SCIP_RETCODE" for a complete list of error codes.
 *
 *  @pre This method can be called if SCIP is in one of the following stages:
 *       - \ref SCIP_STAGE_INITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVING
 *       - \ref SCIP_STAGE_EXITPRESOLVE
 *       - \ref SCIP_STAGE_PRESOLVED
 *       - \ref SCIP_STAGE_INITSOLVE
 *       - \ref SCIP_STAGE_SOLVING
 */
EXTERN
SCIP_RETCODE SCIPstoreBenderscutCut(
   SCIP*                 scip,               /**< the SCIP data structure */
   SCIP_BENDERSCUT*      benderscut,         /**< Benders' decomposition cuts */
   SCIP_ROW*             cut                 /**< the cut to be added to the Benders' cut storage */
   );

/* @} */

#ifdef __cplusplus
}
#endif

#endif
