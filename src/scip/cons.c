/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2003 Tobias Achterberg                              */
/*                            Thorsten Koch                                  */
/*                  2002-2003 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the SCIP Academic Licence.        */
/*                                                                           */
/*  You should have received a copy of the SCIP Academic License             */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file   cons.c
 * @brief  datastructures and methods for managing constraints
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>

#include "cons.h"


/** constraint handler */
struct ConsHdlr
{
   char*            name;               /**< name of constraint handler */
   char*            desc;               /**< description of constraint handler */
   int              sepapriority;       /**< priority of the constraint handler for separation */
   int              enfopriority;       /**< priority of the constraint handler for constraint enforcing */
   int              chckpriority;       /**< priority of the constraint handler for checking infeasibility */
   int              propfreq;           /**< frequency for propagating domains; zero means only preprocessing propagation */
   DECL_CONSFREE((*consfree));          /**< destructor of constraint handler */
   DECL_CONSINIT((*consinit));          /**< initialise constraint handler */
   DECL_CONSEXIT((*consexit));          /**< deinitialise constraint handler */
   DECL_CONSDELE((*consdele));          /**< frees specific constraint data */
   DECL_CONSTRAN((*constran));          /**< transforms constraint data into data belonging to the transformed problem */
   DECL_CONSSEPA((*conssepa));          /**< separates cutting planes */
   DECL_CONSENLP((*consenlp));          /**< enforcing constraints for LP solutions */
   DECL_CONSENPS((*consenps));          /**< enforcing constraints for pseudo solutions */
   DECL_CONSCHCK((*conschck));          /**< check feasibility of primal solution */
   DECL_CONSPROP((*consprop));          /**< propagate variable domains */
   CONSHDLRDATA*    conshdlrdata;       /**< constraint handler data */
   CONS**           sepaconss;          /**< array with active constraints that must be separated during LP processing */
   int              sepaconsssize;      /**< size of sepaconss array */
   int              nsepaconss;         /**< number of active constraints that must be separated during LP processing */
   CONS**           enfoconss;          /**< array with active constraints that must be enforced during node processing */
   int              enfoconsssize;      /**< size of enfoconss array */
   int              nenfoconss;         /**< number of active constraints that must be enforced during node processing */
   CONS**           chckconss;          /**< array with active constraints that must be checked for feasibility */
   int              chckconsssize;      /**< size of chckconss array */
   int              nchckconss;         /**< number of active constraints that must be checked for feasibility */
   CONS**           propconss;          /**< array with active constraints that must be propagated during node processing */
   int              propconsssize;      /**< size of propconss array */
   int              npropconss;         /**< number of active constraints that must be propagated during node processing */
   int              nconss;             /**< total number of active constraints of the handler */
   int              lastnsepaconss;     /**< number of already separated constraints after last conshdlrResetSepa() call */
   int              lastnenfoconss;     /**< number of already enforced constraints after last conshdlrResetEnfo() call */
   unsigned int     needscons:1;        /**< should the constraint handler be skipped, if no constraints are available? */
   unsigned int     initialized:1;      /**< is constraint handler initialized? */
};

/** constraint data structure */
struct Cons
{
   char*            name;               /**< name of the constraint */
   CONSHDLR*        conshdlr;           /**< constraint handler for this constraint */
   CONSDATA*        consdata;           /**< data for this specific constraint */
   int              nuses;              /**< number of times, this constraint is referenced */
   int              sepaconsspos;       /**< position of constraint in the handler's sepaconss array */
   int              enfoconsspos;       /**< position of constraint in the handler's enfoconss array */
   int              chckconsspos;       /**< position of constraint in the handler's chckconss array */
   int              propconsspos;       /**< position of constraint in the handler's propconss array */
   unsigned int     separate:1;         /**< TRUE iff constraint should be separated during LP processing */
   unsigned int     enforce:1;          /**< TRUE iff constraint should be enforced during node processing */
   unsigned int     check:1;            /**< TRUE iff constraint should be checked for feasibility */
   unsigned int     propagate:1;        /**< TRUE iff constraint should be propagated during node processing */
   unsigned int     original:1;         /**< TRUE iff constraint belongs to original problem */
   unsigned int     active:1;           /**< TRUE iff constraint is active in the active node */
};




/*
 * dynamic memory arrays
 */


/** resizes sepaconss array to be able to store at least num constraints */
static
RETCODE conshdlrEnsureSepaconssMem(
   CONSHDLR*        conshdlr,           /**< constraint handler */
   const SET*       set,                /**< global SCIP settings */
   int              num                 /**< minimal number of node slots in array */
   )
{
   assert(conshdlr != NULL);
   assert(set != NULL);

   if( num > conshdlr->sepaconsssize )
   {
      int newsize;

      newsize = SCIPsetCalcMemGrowSize(set, num);
      ALLOC_OKAY( reallocMemoryArray(&conshdlr->sepaconss, newsize) );
      conshdlr->sepaconsssize = newsize;
   }
   assert(num <= conshdlr->sepaconsssize);

   return SCIP_OKAY;
}

/** resizes enfoconss array to be able to store at least num constraints */
static
RETCODE conshdlrEnsureEnfoconssMem(
   CONSHDLR*        conshdlr,           /**< constraint handler */
   const SET*       set,                /**< global SCIP settings */
   int              num                 /**< minimal number of node slots in array */
   )
{
   assert(conshdlr != NULL);
   assert(set != NULL);

   if( num > conshdlr->enfoconsssize )
   {
      int newsize;

      newsize = SCIPsetCalcMemGrowSize(set, num);
      ALLOC_OKAY( reallocMemoryArray(&conshdlr->enfoconss, newsize) );
      conshdlr->enfoconsssize = newsize;
   }
   assert(num <= conshdlr->enfoconsssize);

   return SCIP_OKAY;
}

/** resizes chckconss array to be able to store at least num constraints */
static
RETCODE conshdlrEnsureChckconssMem(
   CONSHDLR*        conshdlr,           /**< constraint handler */
   const SET*       set,                /**< global SCIP settings */
   int              num                 /**< minimal number of node slots in array */
   )
{
   assert(conshdlr != NULL);
   assert(set != NULL);

   if( num > conshdlr->chckconsssize )
   {
      int newsize;

      newsize = SCIPsetCalcMemGrowSize(set, num);
      ALLOC_OKAY( reallocMemoryArray(&conshdlr->chckconss, newsize) );
      conshdlr->chckconsssize = newsize;
   }
   assert(num <= conshdlr->chckconsssize);

   return SCIP_OKAY;
}

/** resizes propconss array to be able to store at least num constraints */
static
RETCODE conshdlrEnsurePropconssMem(
   CONSHDLR*        conshdlr,           /**< constraint handler */
   const SET*       set,                /**< global SCIP settings */
   int              num                 /**< minimal number of node slots in array */
   )
{
   assert(conshdlr != NULL);
   assert(set != NULL);

   if( num > conshdlr->propconsssize )
   {
      int newsize;

      newsize = SCIPsetCalcMemGrowSize(set, num);
      ALLOC_OKAY( reallocMemoryArray(&conshdlr->propconss, newsize) );
      conshdlr->propconsssize = newsize;
   }
   assert(num <= conshdlr->propconsssize);

   return SCIP_OKAY;
}


/*
 * Constraint handler methods
 */

/** activates and adds constraint to constraint handler's constraint arrays */
static
RETCODE conshdlrAddCons(
   CONSHDLR*        conshdlr,           /**< constraint handler */
   const SET*       set,                /**< global SCIP settings */
   CONS*            cons                /**< constraint to add */
   )
{
   assert(conshdlr != NULL);
   assert(set != NULL);
   assert(cons != NULL);
   assert(cons->conshdlr == conshdlr);
   assert(!cons->active);
   assert(cons->sepaconsspos == -1);
   assert(cons->enfoconsspos == -1);
   assert(cons->chckconsspos == -1);
   assert(cons->propconsspos == -1);

   debugMessage("add constraint <%s> to constraint handler <%s>\n", cons->name, conshdlr->name);

   /* activate constraint */
   cons->active = TRUE;
   conshdlr->nconss++;

   /* add constraint to the separation array */
   if( cons->separate )
   {
      CHECK_OKAY( conshdlrEnsureSepaconssMem(conshdlr, set, conshdlr->nsepaconss+1) );
      cons->sepaconsspos = conshdlr->nsepaconss;
      conshdlr->sepaconss[conshdlr->nsepaconss] = cons;
      conshdlr->nsepaconss++;
   }
      
   /* add constraint to the enforcement array */
   if( cons->enforce )
   {
      CHECK_OKAY( conshdlrEnsureEnfoconssMem(conshdlr, set, conshdlr->nenfoconss+1) );
      cons->enfoconsspos = conshdlr->nenfoconss;
      conshdlr->enfoconss[conshdlr->nenfoconss] = cons;
      conshdlr->nenfoconss++;
   }

   /* add constraint to the check array */
   if( cons->check )
   {
      CHECK_OKAY( conshdlrEnsureChckconssMem(conshdlr, set, conshdlr->nchckconss+1) );
      cons->chckconsspos = conshdlr->nchckconss;
      conshdlr->chckconss[conshdlr->nchckconss] = cons;
      conshdlr->nchckconss++;
   }

   /* add constraint to the propagation array */
   if( cons->propagate )
   {
      CHECK_OKAY( conshdlrEnsurePropconssMem(conshdlr, set, conshdlr->npropconss+1) );
      cons->propconsspos = conshdlr->npropconss;
      conshdlr->propconss[conshdlr->npropconss] = cons;
      conshdlr->npropconss++;
   }

   return SCIP_OKAY;
}

/** deactivates and removes constraint from constraint handler's conss array */
static
RETCODE conshdlrDelCons(
   CONSHDLR*        conshdlr,           /**< constraint handler */
   CONS*            cons                /**< constraint to remove */
   )
{
   int delpos;

   assert(conshdlr != NULL);
   assert(cons != NULL);
   assert(cons->conshdlr == conshdlr);
   assert(cons->active);
   assert((cons->separate) ^ (cons->sepaconsspos == -1));
   assert((cons->enforce) ^ (cons->enfoconsspos == -1));
   assert((cons->check) ^ (cons->chckconsspos == -1));
   assert((cons->propagate) ^ (cons->propconsspos == -1));

   debugMessage("delete constraint <%s> from constraint handler <%s>\n", cons->name, conshdlr->name);

   /* delete constraint from the separation array */
   if( cons->separate )
   {
      delpos = cons->sepaconsspos;
      assert(0 <= delpos && delpos < conshdlr->nsepaconss);
      conshdlr->sepaconss[delpos] = conshdlr->sepaconss[conshdlr->nsepaconss-1];
      conshdlr->sepaconss[delpos]->sepaconsspos = delpos;
      conshdlr->nsepaconss--;
      cons->sepaconsspos = -1;
   }

   /* delete constraint from the enforcement array */
   if( cons->enforce )
   {
      delpos = cons->enfoconsspos;
      assert(0 <= delpos && delpos < conshdlr->nenfoconss);
      conshdlr->enfoconss[delpos] = conshdlr->enfoconss[conshdlr->nenfoconss-1];
      conshdlr->enfoconss[delpos]->enfoconsspos = delpos;
      conshdlr->nenfoconss--;
      cons->enfoconsspos = -1;
   }

   /* delete constraint from the check array */
   if( cons->check )
   {
      delpos = cons->chckconsspos;
      assert(0 <= delpos && delpos < conshdlr->nchckconss);
      conshdlr->chckconss[delpos] = conshdlr->chckconss[conshdlr->nchckconss-1];
      conshdlr->chckconss[delpos]->chckconsspos = delpos;
      conshdlr->nchckconss--;
      cons->chckconsspos = -1;
   }

   /* delete constraint from the propagation array */
   if( cons->propagate )
   {
      delpos = cons->propconsspos;
      assert(0 <= delpos && delpos < conshdlr->npropconss);
      conshdlr->propconss[delpos] = conshdlr->propconss[conshdlr->npropconss-1];
      conshdlr->propconss[delpos]->propconsspos = delpos;
      conshdlr->npropconss--;
      cons->propconsspos = -1;
   }

   assert(cons->sepaconsspos == -1);
   assert(cons->enfoconsspos == -1);
   assert(cons->chckconsspos == -1);
   assert(cons->propconsspos == -1);

   /* deactivate constraint */
   cons->active = FALSE;
   conshdlr->nconss--;

   return SCIP_OKAY;
}

DECL_SORTPTRCOMP(SCIPconshdlrCompSepa)  /**< compares two constraint handlers w. r. to their separation priority */
{
   return ((CONSHDLR*)elem2)->sepapriority - ((CONSHDLR*)elem1)->sepapriority;
}

DECL_SORTPTRCOMP(SCIPconshdlrCompEnfo)  /**< compares two constraint handlers w. r. to their enforcing priority */
{
   return ((CONSHDLR*)elem2)->enfopriority - ((CONSHDLR*)elem1)->enfopriority;
}

DECL_SORTPTRCOMP(SCIPconshdlrCompChck)  /**< compares two constraint handlers w. r. to their feasibility check priority */
{
   return ((CONSHDLR*)elem2)->chckpriority - ((CONSHDLR*)elem1)->chckpriority;
}

/** creates a constraint handler */
RETCODE SCIPconshdlrCreate(
   CONSHDLR**       conshdlr,           /**< pointer to constraint handler data structure */
   const char*      name,               /**< name of constraint handler */
   const char*      desc,               /**< description of constraint handler */
   int              sepapriority,       /**< priority of the constraint handler for separation */
   int              enfopriority,       /**< priority of the constraint handler for constraint enforcing */
   int              chckpriority,       /**< priority of the constraint handler for checking infeasibility */
   int              propfreq,           /**< frequency for propagating domains; zero means only preprocessing propagation */
   Bool             needscons,          /**< should the constraint handler be skipped, if no constraints are available? */
   DECL_CONSFREE((*consfree)),          /**< destructor of constraint handler */
   DECL_CONSINIT((*consinit)),          /**< initialise constraint handler */
   DECL_CONSEXIT((*consexit)),          /**< deinitialise constraint handler */
   DECL_CONSDELE((*consdele)),          /**< free specific constraint data */
   DECL_CONSTRAN((*constran)),          /**< transform constraint data into data belonging to the transformed problem */
   DECL_CONSSEPA((*conssepa)),          /**< separate cutting planes */
   DECL_CONSENLP((*consenlp)),          /**< enforcing constraints for LP solutions */
   DECL_CONSENPS((*consenps)),          /**< enforcing constraints for pseudo solutions */
   DECL_CONSCHCK((*conschck)),          /**< check feasibility of primal solution */
   DECL_CONSPROP((*consprop)),          /**< propagate variable domains */
   CONSHDLRDATA*    conshdlrdata        /**< constraint handler data */
   )
{
   assert(conshdlr != NULL);
   assert(name != NULL);
   assert(desc != NULL);
   assert((propfreq >= 0) ^ (consprop == NULL));

   ALLOC_OKAY( allocMemory(conshdlr) );
   ALLOC_OKAY( duplicateMemoryArray(&(*conshdlr)->name, name, strlen(name)+1) );
   ALLOC_OKAY( duplicateMemoryArray(&(*conshdlr)->desc, desc, strlen(desc)+1) );
   (*conshdlr)->sepapriority = sepapriority;
   (*conshdlr)->enfopriority = enfopriority;
   (*conshdlr)->chckpriority = chckpriority;
   (*conshdlr)->propfreq = propfreq;
   (*conshdlr)->consfree = consfree;
   (*conshdlr)->consinit = consinit;
   (*conshdlr)->consexit = consexit;
   (*conshdlr)->consdele = consdele;
   (*conshdlr)->constran = constran;
   (*conshdlr)->conssepa = conssepa;
   (*conshdlr)->consenlp = consenlp;
   (*conshdlr)->consenps = consenps;
   (*conshdlr)->conschck = conschck;
   (*conshdlr)->consprop = consprop;
   (*conshdlr)->conshdlrdata = conshdlrdata;
   (*conshdlr)->sepaconss = NULL;
   (*conshdlr)->sepaconsssize = 0;
   (*conshdlr)->nsepaconss = 0;
   (*conshdlr)->enfoconss = NULL;
   (*conshdlr)->enfoconsssize = 0;
   (*conshdlr)->nenfoconss = 0;
   (*conshdlr)->chckconss = NULL;
   (*conshdlr)->chckconsssize = 0;
   (*conshdlr)->nchckconss = 0;
   (*conshdlr)->propconss = NULL;
   (*conshdlr)->propconsssize = 0;
   (*conshdlr)->npropconss = 0;
   (*conshdlr)->nconss = 0;
   (*conshdlr)->lastnsepaconss = 0;
   (*conshdlr)->lastnenfoconss = 0;
   (*conshdlr)->needscons = needscons;
   (*conshdlr)->initialized = FALSE;

   return SCIP_OKAY;
}

/** calls destructor and frees memory of constraint handler */
RETCODE SCIPconshdlrFree(
   CONSHDLR**       conshdlr,           /**< pointer to constraint handler data structure */
   SCIP*            scip                /**< SCIP data structure */   
   )
{
   assert(conshdlr != NULL);
   assert(*conshdlr != NULL);
   assert(!(*conshdlr)->initialized);
   assert(scip != NULL);

   /* call destructor of constraint handler */
   if( (*conshdlr)->consfree != NULL )
   {
      CHECK_OKAY( (*conshdlr)->consfree(scip, *conshdlr) );
   }

   freeMemoryArray(&(*conshdlr)->name);
   freeMemoryArray(&(*conshdlr)->desc);
   freeMemoryArrayNull(&(*conshdlr)->sepaconss);
   freeMemoryArrayNull(&(*conshdlr)->enfoconss);
   freeMemoryArrayNull(&(*conshdlr)->chckconss);
   freeMemoryArrayNull(&(*conshdlr)->propconss);
   freeMemory(conshdlr);

   return SCIP_OKAY;
}

/** initializes constraint handler */
RETCODE SCIPconshdlrInit(
   CONSHDLR*        conshdlr,           /**< constraint handler */
   SCIP*            scip                /**< SCIP data structure */   
   )
{
   assert(conshdlr != NULL);
   assert(scip != NULL);

   if( conshdlr->initialized )
   {
      char s[255];
      sprintf(s, "Constraint handler <%s> already initialized", conshdlr->name);
      errorMessage(s);
      return SCIP_INVALIDCALL;
   }

   if( conshdlr->consinit != NULL )
   {
      CHECK_OKAY( conshdlr->consinit(scip, conshdlr) );
   }
   conshdlr->initialized = TRUE;

   return SCIP_OKAY;
}

/** calls exit method of constraint handler */
RETCODE SCIPconshdlrExit(
   CONSHDLR*        conshdlr,           /**< constraint handler */
   SCIP*            scip                /**< SCIP data structure */   
   )
{
   assert(conshdlr != NULL);
   assert(scip != NULL);

   if( !conshdlr->initialized )
   {
      char s[255];
      sprintf(s, "Constraint handler <%s> not initialized", conshdlr->name);
      errorMessage(s);
      return SCIP_INVALIDCALL;
   }

   if( conshdlr->consexit != NULL )
   {
      CHECK_OKAY( conshdlr->consexit(scip, conshdlr) );
   }
   conshdlr->initialized = FALSE;

   return SCIP_OKAY;
}

/** calls separator method of constraint handler to separate all constraints added after last conshdlrResetSepa() call */
RETCODE SCIPconshdlrSeparate(
   CONSHDLR*        conshdlr,           /**< constraint handler */
   const SET*       set,                /**< global SCIP settings */
   RESULT*          result              /**< pointer to store the result of the callback method */
   )
{
   assert(conshdlr != NULL);
   assert(0 <= conshdlr->lastnsepaconss && conshdlr->lastnsepaconss <= conshdlr->nsepaconss);
   assert(set != NULL);
   assert(set->scip != NULL);
   assert(result != NULL);

   *result = SCIP_DIDNOTRUN;

   if( conshdlr->conssepa != NULL )
   {
      int nconss;

      nconss = conshdlr->nsepaconss - conshdlr->lastnsepaconss;

      if( !conshdlr->needscons || nconss > 0 )
      {
         CONS** conss;
         
         debugMessage("separating constraints %d to %d of %d constraints of handler <%s>\n",
            conshdlr->lastnsepaconss, conshdlr->lastnsepaconss + nconss - 1, conshdlr->nsepaconss, conshdlr->name);

         conss = &(conshdlr->sepaconss[conshdlr->lastnsepaconss]);
         conshdlr->lastnsepaconss = conshdlr->nsepaconss;

         CHECK_OKAY( conshdlr->conssepa(set->scip, conshdlr, conss, nconss, result) );
         if( *result != SCIP_SEPARATED
            && *result != SCIP_CONSADDED
            && *result != SCIP_DIDNOTFIND
            && *result != SCIP_DIDNOTRUN )
         {
            char s[255];
            sprintf(s, "separation method of constraint handler <%s> returned invalid result <%d>", 
               conshdlr->name, *result);
            errorMessage(s);
            return SCIP_INVALIDRESULT;
         }
      }
   }

   return SCIP_OKAY;
}

/** calls enforcing method of constraint handler for LP solution for all constraints added after last
 *  conshdlrResetEnfo() call
 */
RETCODE SCIPconshdlrEnforceLPSol(
   CONSHDLR*        conshdlr,           /**< constraint handler */
   const SET*       set,                /**< global SCIP settings */
   RESULT*          result              /**< pointer to store the result of the callback method */
   )
{
   assert(conshdlr != NULL);
   assert(0 <= conshdlr->lastnenfoconss && conshdlr->lastnenfoconss <= conshdlr->nenfoconss);
   assert(set != NULL);
   assert(set->scip != NULL);
   assert(result != NULL);

   *result = SCIP_FEASIBLE;

   if( conshdlr->consenlp != NULL )
   {
      int nconss;

      nconss = conshdlr->nenfoconss - conshdlr->lastnenfoconss;

      if( !conshdlr->needscons || nconss > 0 )
      {
         CONS** conss;
         
         debugMessage("enforcing constraints %d to %d of %d constraints of handler <%s>\n",
            conshdlr->lastnenfoconss, conshdlr->lastnenfoconss + nconss - 1, conshdlr->nenfoconss, conshdlr->name);

         conss = &(conshdlr->enfoconss[conshdlr->lastnenfoconss]);
         conshdlr->lastnenfoconss = conshdlr->nenfoconss;

         CHECK_OKAY( conshdlr->consenlp(set->scip, conshdlr, conss, nconss, result) );
         if( *result != SCIP_CUTOFF
            && *result != SCIP_BRANCHED
            && *result != SCIP_REDUCEDDOM
            && *result != SCIP_SEPARATED
            && *result != SCIP_CONSADDED
            && *result != SCIP_INFEASIBLE
            && *result != SCIP_FEASIBLE )
         {
            char s[255];
            sprintf(s, "enforcing method of constraint handler <%s> for LP solutions returned invalid result <%d>", 
               conshdlr->name, *result);
            errorMessage(s);
            return SCIP_INVALIDRESULT;
         }
      }
   }

   return SCIP_OKAY;
}

/** calls enforcing method of constraint handler for pseudo solution for all constraints added after last
 *  conshdlrResetEnfo() call
 */
RETCODE SCIPconshdlrEnforcePseudoSol(
   CONSHDLR*        conshdlr,           /**< constraint handler */
   const SET*       set,                /**< global SCIP settings */
   RESULT*          result              /**< pointer to store the result of the callback method */
   )
{
   assert(conshdlr != NULL);
   assert(0 <= conshdlr->lastnenfoconss && conshdlr->lastnenfoconss <= conshdlr->nenfoconss);
   assert(set != NULL);
   assert(set->scip != NULL);
   assert(result != NULL);

   *result = SCIP_FEASIBLE;

   if( conshdlr->consenps != NULL )
   {
      int nconss;

      nconss = conshdlr->nenfoconss - conshdlr->lastnenfoconss;

      if( !conshdlr->needscons || nconss > 0 )
      {
         CONS** conss;
         
         debugMessage("enforcing constraints %d to %d of %d constraints of handler <%s>\n",
            conshdlr->lastnenfoconss, conshdlr->lastnenfoconss + nconss - 1, conshdlr->nenfoconss, conshdlr->name);

         conss = &(conshdlr->enfoconss[conshdlr->lastnenfoconss]);
         conshdlr->lastnenfoconss = conshdlr->nenfoconss;

         CHECK_OKAY( conshdlr->consenps(set->scip, conshdlr, conss, nconss, result) );
         if( *result != SCIP_CUTOFF
            && *result != SCIP_BRANCHED
            && *result != SCIP_REDUCEDDOM
            && *result != SCIP_CONSADDED
            && *result != SCIP_INFEASIBLE
            && *result != SCIP_FEASIBLE )
         {
            char s[255];
            sprintf(s, "enforcing method of constraint handler <%s> for pseudo solutions returned invalid result <%d>", 
               conshdlr->name, *result);
            errorMessage(s);
            return SCIP_INVALIDRESULT;
         }
      }
   }

   return SCIP_OKAY;
}

/** calls feasibility check method of constraint handler */
RETCODE SCIPconshdlrCheck(
   CONSHDLR*        conshdlr,           /**< constraint handler */
   const SET*       set,                /**< global SCIP settings */
   SOL*             sol,                /**< primal CIP solution */
   Bool             chckintegrality,    /**< has integrality to be checked? */
   Bool             chcklprows,         /**< have current LP rows to be checked? */
   RESULT*          result              /**< pointer to store the result of the callback method */
   )
{
   assert(conshdlr != NULL);
   assert(set != NULL);
   assert(set->scip != NULL);
   assert(result != NULL);

   *result = SCIP_FEASIBLE;

   if( conshdlr->conschck != NULL && (!conshdlr->needscons || conshdlr->nchckconss > 0) )
   {
      debugMessage("checking %d constraints of handler <%s>\n", conshdlr->nchckconss, conshdlr->name);
      CHECK_OKAY( conshdlr->conschck(set->scip, conshdlr, conshdlr->chckconss, conshdlr->nchckconss, 
                     sol, chckintegrality, chcklprows, result) );
      if( *result != SCIP_INFEASIBLE
         && *result != SCIP_FEASIBLE )
      {
         char s[255];
         sprintf(s, "feasibility check of constraint handler <%s> returned invalid result <%d>", 
            conshdlr->name, *result);
         errorMessage(s);
         return SCIP_INVALIDRESULT;
      }
   }

   return SCIP_OKAY;
}

/** calls propagation method of constraint handler */
RETCODE SCIPconshdlrPropagate(
   CONSHDLR*        conshdlr,           /**< constraint handler */
   const SET*       set,                /**< global SCIP settings */
   int              actdepth,           /**< depth of active node; -1 if preprocessing domain propagation */
   RESULT*          result              /**< pointer to store the result of the callback method */
   )
{
   assert(conshdlr != NULL);
   assert(set != NULL);
   assert(set->scip != NULL);
   assert(result != NULL);

   *result = SCIP_DIDNOTRUN;

   if( conshdlr->consprop != NULL
      && (!conshdlr->needscons || conshdlr->npropconss > 0)
      && (actdepth == -1 || (conshdlr->propfreq > 0 && actdepth % conshdlr->propfreq == 0)) )
   {
      debugMessage("propagating %d constraints of handler <%s>\n", conshdlr->npropconss, conshdlr->name);
      CHECK_OKAY( conshdlr->consprop(set->scip, conshdlr, conshdlr->propconss, conshdlr->npropconss, result) );
      if( *result != SCIP_CUTOFF
         && *result != SCIP_REDUCEDDOM
         && *result != SCIP_DIDNOTFIND
         && *result != SCIP_DIDNOTRUN )
      {
         char s[255];
         sprintf(s, "propagation method of constraint handler <%s> returned invalid result <%d>", 
            conshdlr->name, *result);
         errorMessage(s);
         return SCIP_INVALIDRESULT;
      }
   }

   return SCIP_OKAY;
}

/** resets separation to start with first constraint in the next call */
void SCIPconshdlrResetSepa(
   CONSHDLR*        conshdlr            /**< constraint handler */
   )
{
   assert(conshdlr != NULL);

   conshdlr->lastnsepaconss = 0;
}

/** resets enforcement to start with first constraint in the next call */
void SCIPconshdlrResetEnfo(
   CONSHDLR*        conshdlr            /**< constraint handler */
   )
{
   assert(conshdlr != NULL);

   conshdlr->lastnenfoconss = 0;
}

/** gets name of constraint handler */
const char* SCIPconshdlrGetName(
   CONSHDLR*        conshdlr            /**< constraint handler */
   )
{
   assert(conshdlr != NULL);

   return conshdlr->name;
}

/** gets user data of constraint handler */
CONSHDLRDATA* SCIPconshdlrGetData(
   CONSHDLR*        conshdlr            /**< constraint handler */
   )
{
   assert(conshdlr != NULL);

   return conshdlr->conshdlrdata;
}

/** sets user data of constraint handler; user has to free old data in advance! */
void SCIPconshdlrSetData(
   CONSHDLR*        conshdlr,           /**< constraint handler */
   CONSHDLRDATA*    conshdlrdata        /**< new constraint handler user data */
   )
{
   assert(conshdlr != NULL);

   conshdlr->conshdlrdata = conshdlrdata;
}

/** gets number of active constraints of constraint handler */
int SCIPconshdlrGetNConss(
   CONSHDLR*        conshdlr            /**< constraint handler */
   )
{
   assert(conshdlr != NULL);

   return conshdlr->nconss;
}

/** gets checking priority of constraint handler */
int SCIPconshdlrGetChckPriority(
   CONSHDLR*        conshdlr            /**< constraint handler */
   )
{
   assert(conshdlr != NULL);

   return conshdlr->chckpriority;
}

/** gets propagation frequency of constraint handler */
int SCIPconshdlrGetPropFreq(
   CONSHDLR*        conshdlr            /**< constraint handler */
   )
{
   assert(conshdlr != NULL);

   return conshdlr->propfreq;
}

/** is constraint handler initialized? */
Bool SCIPconshdlrIsInitialized(
   CONSHDLR*        conshdlr            /**< constraint handler */
   )
{
   assert(conshdlr != NULL);

   return conshdlr->initialized;
}




/*
 * Constraint methods
 */

/** creates and captures a constraint
 *  Warning! If a constraint is marked to be checked for feasibility but not to be enforced, a LP or pseudo solution
 *  may be declared feasible even if it violates this particular constraint.
 *  This constellation should only be used, if no LP or pseudo solution can violate the constraint -- e.g. if a
 *  local constraint is redundant due to the variable's local bounds.
 */
RETCODE SCIPconsCreate(
   CONS**           cons,               /**< pointer to constraint */
   MEMHDR*          memhdr,             /**< block memory */
   const char*      name,               /**< name of constraint */
   CONSHDLR*        conshdlr,           /**< constraint handler for this constraint */
   CONSDATA*        consdata,           /**< data for this specific constraint */
   Bool             separate,           /**< should the constraint be separated during LP processing? */
   Bool             enforce,            /**< should the constraint be enforced during node processing? */
   Bool             check,              /**< should the constraint be checked for feasibility? */
   Bool             propagate,          /**< should the constraint be propagated during node processing? */
   Bool             original            /**< is constraint belonging to the original problem? */
   )
{
   assert(cons != NULL);
   assert(memhdr != NULL);
   assert(conshdlr != NULL);

   /* create constraint data */
   ALLOC_OKAY( allocBlockMemory(memhdr, cons) );
   ALLOC_OKAY( duplicateBlockMemoryArray(memhdr, &(*cons)->name, name, strlen(name)+1) );
   (*cons)->conshdlr = conshdlr;
   (*cons)->consdata = consdata;
   (*cons)->nuses = 0;
   (*cons)->sepaconsspos = -1;
   (*cons)->enfoconsspos = -1;
   (*cons)->chckconsspos = -1;
   (*cons)->propconsspos = -1;
   (*cons)->separate = separate;
   (*cons)->enforce = enforce;
   (*cons)->check = check;
   (*cons)->propagate = propagate;
   (*cons)->original = original;
   (*cons)->active = FALSE;

   /* capture constraint */
   SCIPconsCapture(*cons);

   return SCIP_OKAY;
}

/** frees a constraint */
RETCODE SCIPconsFree(
   CONS**           cons,               /**< constraint to free */
   MEMHDR*          memhdr,             /**< block memory buffer */
   const SET*       set                 /**< global SCIP settings */
   )
{
   assert(cons != NULL);
   assert(*cons != NULL);
   assert((*cons)->nuses == 0);
   assert((*cons)->conshdlr != NULL);
   assert(memhdr != NULL);
   assert(set != NULL);

   /* free constraint data */
   if( (*cons)->conshdlr->consdele != NULL )
   {
      CHECK_OKAY( (*cons)->conshdlr->consdele(set->scip, (*cons)->conshdlr, &(*cons)->consdata) );
   }
   freeBlockMemoryArray(memhdr, &(*cons)->name, strlen((*cons)->name)+1);
   freeBlockMemory(memhdr, cons);

   return SCIP_OKAY;
}

/** increases usage counter of constraint */
void SCIPconsCapture(
   CONS*            cons                /**< constraint */
   )
{
   assert(cons != NULL);
   assert(cons->nuses >= 0);

   debugMessage("capture constraint <%s> with nuses=%d\n", cons->name, cons->nuses);
   cons->nuses++;
}

/** decreases usage counter of constraint, and frees memory if necessary */
RETCODE SCIPconsRelease(
   CONS**           cons,               /**< pointer to constraint */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set                 /**< global SCIP settings */
   )
{
   assert(memhdr != NULL);
   assert(cons != NULL);
   assert(*cons != NULL);
   assert((*cons)->nuses >= 1);

   debugMessage("release constraint <%s> with nuses=%d\n", (*cons)->name, (*cons)->nuses);
   (*cons)->nuses--;
   if( (*cons)->nuses == 0 )
   {
      CHECK_OKAY( SCIPconsFree(cons, memhdr, set) );
   }
   *cons  = NULL;

   return SCIP_OKAY;
}

/** activates constraint */
RETCODE SCIPconsActivate(
   CONS*            cons,               /**< constraint */
   const SET*       set                 /**< global SCIP settings */
   )
{
   assert(cons != NULL);
   assert(!cons->active);
   assert(set != NULL);
   
   CHECK_OKAY( conshdlrAddCons(cons->conshdlr, set, cons) );
   assert(cons->active);

   return SCIP_OKAY;
}

/** deactivates constraint */
RETCODE SCIPconsDeactivate(
   CONS*            cons                /**< constraint */
   )
{
   assert(cons != NULL);
   assert(cons->active);
   
   CHECK_OKAY( conshdlrDelCons(cons->conshdlr, cons) );
   assert(!cons->active);

   return SCIP_OKAY;
}

/** copies original constraint into transformed constraint, that is captured */
RETCODE SCIPconsTransform(
   CONS**           transcons,          /**< pointer to store the transformed constraint */
   MEMHDR*          memhdr,             /**< block memory buffer */
   const SET*       set,                /**< global SCIP settings */
   CONS*            origcons            /**< original constraint */
   )
{
   CONSDATA* consdata;

   assert(transcons != NULL);
   assert(memhdr != NULL);
   assert(origcons != NULL);

   /* transform constraint data */
   consdata = NULL;
   if( origcons->conshdlr->constran != NULL )
   {
      CHECK_OKAY( origcons->conshdlr->constran(set->scip, origcons->conshdlr, origcons->consdata, &consdata) );
   }

   /* create new constraint with transformed data */
   CHECK_OKAY( SCIPconsCreate(transcons, memhdr, origcons->name, origcons->conshdlr, consdata,
                  origcons->separate, origcons->enforce, origcons->check, origcons->propagate, FALSE) );

   return SCIP_OKAY;
}

/** returns the name of the constraint */
const char* SCIPconsGetName(
   CONS*            cons                /**< constraint */
   )
{
   assert(cons != NULL);

   return cons->name;
}

/** returns the constraint handler of the constraint */
CONSHDLR* SCIPconsGetConsHdlr(
   CONS*            cons                /**< constraint */
   )
{
   assert(cons != NULL);

   return cons->conshdlr;
}

/** returns the constraint data field of the constraint */
CONSDATA* SCIPconsGetConsData(
   CONS*            cons                /**< constraint */
   )
{
   assert(cons != NULL);

   return cons->consdata;
}

/** returns TRUE iff constraint is belonging to original problem */
Bool SCIPconsIsOriginal(
   CONS*            cons                /**< constraint */
   )
{
   assert(cons != NULL);

   return cons->original;
}




/*
 * Hash functions
 */

/** gets the key (i.e. the name) of the given constraint */
DECL_HASHGETKEY(SCIPhashGetKeyCons)
{
   CONS* cons = (CONS*)elem;

   assert(cons != NULL);
   return cons->name;
}



/*
 * Constraint list methods
 */

/** adds constraint to a list of constraints and captures it */
RETCODE SCIPconslistAdd(
   CONSLIST**       conslist,           /**< constraint list to extend */
   MEMHDR*          memhdr,             /**< block memory */
   CONS*            cons                /**< constraint to add */
   )
{
   CONSLIST* newlist;

   assert(conslist != NULL);
   assert(memhdr != NULL);
   assert(cons != NULL);
   
   ALLOC_OKAY( allocBlockMemory(memhdr, &newlist) );
   newlist->cons = cons;
   newlist->next = *conslist;
   *conslist = newlist;

   SCIPconsCapture(cons);

   return SCIP_OKAY;
}

/** partially unlinks and releases the constraints in the list */
RETCODE SCIPconslistFreePart(
   CONSLIST**       conslist,           /**< constraint list to delete from */
   MEMHDR*          memhdr,             /**< block memory buffer */
   const SET*       set,                /**< global SCIP settings */
   CONSLIST*        firstkeep           /**< first constraint list entry to keep */
   )
{
   CONSLIST* next;

   assert(conslist != NULL);
   assert(memhdr != NULL);
   
   while(*conslist != NULL && *conslist != firstkeep)
   {
      CHECK_OKAY( SCIPconsRelease(&(*conslist)->cons, memhdr, set) );
      next = (*conslist)->next;
      freeBlockMemory(memhdr, conslist);
      *conslist = next;
   }
   assert(*conslist == firstkeep); /* firstkeep should be part of conslist */

   return SCIP_OKAY;
}

/** unlinks and releases all the constraints in the list */
RETCODE SCIPconslistFree(
   CONSLIST**       conslist,           /**< constraint list to delete from */
   MEMHDR*          memhdr,             /**< block memory buffer */
   const SET*       set                 /**< global SCIP settings */
   )
{
   return SCIPconslistFreePart(conslist, memhdr, set, NULL);
}

