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

/**@file   cons_linear.c
 * @brief  constraint handler for linear constraints
 * @author Tobias Achterberg
 *
 *  Linear constraints are separated with a high priority, because they are easy
 *  to separate. Instead of using the global cut pool, the same effect can be
 *  implemented by adding linear constraints to the root node, such that they are
 *  separated each time, the linear constraints are separated. A constraint
 *  handler, which generates linear constraints in this way should have a lower
 *  separation priority than the linear constraint handler, and it should have a
 *  separation frequency that is a multiple of the frequency of the linear
 *  constraint handler. In this way, it can be avoided to separate the same cut
 *  twice, because if a separation run of the handler is always preceded by a
 *  separation of the linear constraints, the priorily added constraints are
 *  always satisfied.
 *
 *  Linear constraints are enforced and checked with a very low priority. Checking
 *  of (many) linear constraints is much more involved than checking the solution
 *  values for integrality. Because we are separating the linear constraints quite
 *  often, it is only necessary to enforce them for integral solutions. A constraint
 *  handler which generates pool cuts in its enforcing method should have an
 *  enforcing priority smaller than that of the linear constraint handler to avoid
 *  regenerating constraints which already exist.
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <string.h>
#include <limits.h>

#include "cons_linear.h"



#define CONSHDLR_NAME          "linear"
#define CONSHDLR_DESC          "linear constraints of the form  lhs <= a^T x <= rhs"
#define CONSHDLR_SEPAPRIORITY  +1000000
#define CONSHDLR_ENFOPRIORITY  -1000000
#define CONSHDLR_CHECKPRIORITY -1000000
#define CONSHDLR_SEPAFREQ             4
#define CONSHDLR_PROPFREQ             4
#define CONSHDLR_NEEDSCONS         TRUE /**< the constraint handler should only be called, if linear constraints exist */

#define TIGHTENBOUNDSFREQ             5 /**< multiplier on propagation frequency, how often the bounds are tightened */

#define EVENTHDLR_NAME         "linear"
#define EVENTHDLR_DESC         "bound change event handler for linear constraints"


/** linear constraint */
struct LinCons
{
   VAR**            vars;               /**< variables of constraint entries */
   Real*            vals;               /**< coefficients of constraint entries */
   EVENTDATA**      eventdatas;         /**< event datas for bound change events of the variables */
   Real             lhs;                /**< left hand side of row (for ranged rows) */
   Real             rhs;                /**< right hand side of row */
   Real             pseudoactivity;     /**< pseudo activity value in actual pseudo solution */
   Real             minactivity;        /**< minimal value w.r.t. the variable's bounds for the constraint's activity,
                                         *   ignoring the coefficients contributing with infinite value */
   Real             maxactivity;        /**< maximal value w.r.t. the variable's bounds for the constraint's activity,
                                         *   ignoring the coefficients contributing with infinite value */
   int              minactivityinf;     /**< number of coefficients contributing with infinite value to minactivity */
   int              maxactivityinf;     /**< number of coefficients contributing with infinite value to maxactivity */
   int              varssize;           /**< size of the vars- and vals-arrays */
   int              nvars;              /**< number of nonzeros in constraint */
   unsigned int     local:1;            /**< is linear constraint only valid locally? */
   unsigned int     modifiable:1;       /**< is data modifiable during node processing (subject to column generation)? */
   unsigned int     removeable:1;       /**< should the row be removed from the LP due to aging or cleanup? */
   unsigned int     transformed:1;      /**< does the linear constraint data belongs to the transformed problem? */
   unsigned int     validactivities:1;  /**< are the pseudo activity and activity bounds valid? */
   unsigned int     propagated:1;       /**< is constraint already preprocessed/propagated? */
   unsigned int     redchecked:1;       /**< is constraint already checked for redundancy with other constraints? */
   unsigned int     sorted:1;           /**< are the constraint's variables sorted? */
};

/** constraint data for linear constraints */
struct ConsData
{
   LINCONS*         lincons;            /**< linear constraint */
   ROW*             row;                /**< LP row, if constraint is already stored in LP row format */
};

/** event data for bound change event */
struct EventData
{
   LINCONS*         lincons;            /**< linear constraint to process the bound change for */
   int              varpos;             /**< position of variable in vars array */
};

/** constraint handler data */
struct ConsHdlrData
{
   LINCONSUPGRADE** linconsupgrades;    /**< linear constraint upgrade methods for specializing linear constraints */
   int              linconsupgradessize;/**< size of linconsupgrade array */
   int              nlinconsupgrades;   /**< number of linear constraint upgrade methods */
   int              tightenboundsfreq;  /**< multiplier on propagation frequency, how often the bounds are tightened */
};

/** linear constraint update method */
struct LinConsUpgrade
{
   DECL_LINCONSUPGD((*linconsupgd));    /**< method to call for upgrading linear constraint */
   int              priority;           /**< priority of upgrading method */
};




/*
 * memory growing methods for dynamically allocated arrays
 */

/** ensures, that linconsupgrades array can store at least num entries */
static
RETCODE conshdlrdataEnsureLinconsupgradesSize(
   SCIP*            scip,               /**< SCIP data structure */
   CONSHDLRDATA*    conshdlrdata,       /**< linear constraint handler data */
   int              num                 /**< minimum number of entries to store */
   )
{
   assert(conshdlrdata != NULL);
   assert(conshdlrdata->nlinconsupgrades <= conshdlrdata->linconsupgradessize);
   
   if( num > conshdlrdata->linconsupgradessize )
   {
      int newsize;

      newsize = SCIPcalcMemGrowSize(scip, num);
      CHECK_OKAY( SCIPreallocMemoryArray(scip, &conshdlrdata->linconsupgrades, newsize) );
      conshdlrdata->linconsupgradessize = newsize;
   }
   assert(num <= conshdlrdata->linconsupgradessize);

   return SCIP_OKAY;
}

/** ensures, that vars and vals arrays can store at least num entries */
static
RETCODE linconsEnsureVarsSize(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint */
   int              num                 /**< minimum number of entries to store */
   )
{
   assert(lincons != NULL);
   assert(lincons->nvars <= lincons->varssize);
   
   if( num > lincons->varssize )
   {
      int newsize;

      newsize = SCIPcalcMemGrowSize(scip, num);
      CHECK_OKAY( SCIPreallocBlockMemoryArray(scip, &lincons->vars, lincons->varssize, newsize) );
      CHECK_OKAY( SCIPreallocBlockMemoryArray(scip, &lincons->vals, lincons->varssize, newsize) );
      if( lincons->transformed )
      {
         CHECK_OKAY( SCIPreallocBlockMemoryArray(scip, &lincons->eventdatas, lincons->varssize, newsize) );
      }
      else
         assert(lincons->eventdatas == NULL);
      lincons->varssize = newsize;
   }
   assert(num <= lincons->varssize);

   return SCIP_OKAY;
}




/*
 * local methods for managing linear constraint update methods
 */

/** creates a linear constraint upgrade data object */
static
RETCODE linconsupgradeCreate(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONSUPGRADE** linconsupgrade,     /**< pointer to store the linear constraint upgrade */
   DECL_LINCONSUPGD((*linconsupgd)),    /**< method to call for upgrading linear constraint */
   int              priority            /**< priority of upgrading method */
   )
{
   assert(linconsupgrade != NULL);
   assert(linconsupgd != NULL);

   CHECK_OKAY( SCIPallocMemory(scip, linconsupgrade) );
   (*linconsupgrade)->linconsupgd = linconsupgd;
   (*linconsupgrade)->priority = priority;

   return SCIP_OKAY;
}

/** frees a linear constraint upgrade data object */
static
RETCODE linconsupgradeFree(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONSUPGRADE** linconsupgrade      /**< pointer to the linear constraint upgrade */
   )
{
   assert(linconsupgrade != NULL);
   assert(*linconsupgrade != NULL);

   SCIPfreeMemory(scip, linconsupgrade);

   return SCIP_OKAY;
}

/** creates constaint handler data for linear constraint handler */
static
RETCODE conshdlrdataCreate(
   SCIP*            scip,               /**< SCIP data structure */
   CONSHDLRDATA**   conshdlrdata        /**< pointer to store the constraint handler data */
   )
{
   assert(conshdlrdata != NULL);

   CHECK_OKAY( SCIPallocMemory(scip, conshdlrdata) );
   (*conshdlrdata)->linconsupgrades = NULL;
   (*conshdlrdata)->linconsupgradessize = 0;
   (*conshdlrdata)->nlinconsupgrades = 0;
   (*conshdlrdata)->tightenboundsfreq = TIGHTENBOUNDSFREQ;

   return SCIP_OKAY;
}

/** frees constraint handler data for linear constraint handler */
static
void conshdlrdataFree(
   SCIP*            scip,               /**< SCIP data structure */
   CONSHDLRDATA**   conshdlrdata        /**< pointer to the constraint handler data */
   )
{
   int i;

   assert(conshdlrdata != NULL);
   assert(*conshdlrdata != NULL);

   for( i = 0; i < (*conshdlrdata)->nlinconsupgrades; ++i )
   {
      linconsupgradeFree(scip, &(*conshdlrdata)->linconsupgrades[i]);
   }
   SCIPfreeMemoryArrayNull(scip, &(*conshdlrdata)->linconsupgrades);

   SCIPfreeMemory(scip, conshdlrdata);
}

/** adds a linear constraint update method to the constraint handler's data */
static
RETCODE conshdlrdataIncludeUpgrade(
   SCIP*            scip,               /**< SCIP data structure */
   CONSHDLRDATA*    conshdlrdata,       /**< constraint handler data */
   LINCONSUPGRADE*  linconsupgrade      /**< linear constraint upgrade method */
   )
{
   int i;

   assert(conshdlrdata != NULL);
   assert(linconsupgrade != NULL);

   CHECK_OKAY( conshdlrdataEnsureLinconsupgradesSize(scip, conshdlrdata, conshdlrdata->nlinconsupgrades+1) );

   for( i = conshdlrdata->nlinconsupgrades;
        i > 0 && conshdlrdata->linconsupgrades[i-1]->priority < linconsupgrade->priority; --i )
   {
      conshdlrdata->linconsupgrades[i] = conshdlrdata->linconsupgrades[i-1];
   }
   assert(0 <= i && i <= conshdlrdata->nlinconsupgrades);
   conshdlrdata->linconsupgrades[i] = linconsupgrade;
   conshdlrdata->nlinconsupgrades++;

   return SCIP_OKAY;
}




/*
 * lincons local methods
 */

/** creates event data for variable at given position, and catches events */
static
RETCODE linconsCatchEvent(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint object */
   EVENTHDLR*       eventhdlr,          /**< event handler to call for the event processing */
   int              pos                 /**< array position of variable to catch bound change events for */
   )
{
   assert(lincons != NULL);
   assert(eventhdlr != NULL);
   assert(0 <= pos && pos < lincons->nvars);
   assert(lincons->vars != NULL);
   assert(lincons->vars[pos] != NULL);
   assert(lincons->eventdatas != NULL);
   assert(lincons->eventdatas[pos] == NULL);

   CHECK_OKAY( SCIPallocBlockMemory(scip, &lincons->eventdatas[pos]) );
   lincons->eventdatas[pos]->lincons = lincons;
   lincons->eventdatas[pos]->varpos = pos;

   CHECK_OKAY( SCIPcatchVarEvent(scip, lincons->vars[pos], SCIP_EVENTTYPE_BOUNDCHANGED, eventhdlr, 
                  lincons->eventdatas[pos]) );

   return SCIP_OKAY;
}

/** deletes event data for variable at given position, and drops events */
static
RETCODE linconsDropEvent(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint object */
   EVENTHDLR*       eventhdlr,          /**< event handler to call for the event processing */
   int              pos                 /**< array position of variable to catch bound change events for */
   )
{
   assert(lincons != NULL);
   assert(eventhdlr != NULL);
   assert(0 <= pos && pos < lincons->nvars);
   assert(lincons->vars[pos] != NULL);
   assert(lincons->eventdatas[pos] != NULL);
   assert(lincons->eventdatas[pos]->lincons == lincons);
   assert(lincons->eventdatas[pos]->varpos == pos);
   
   CHECK_OKAY( SCIPdropVarEvent(scip, lincons->vars[pos], eventhdlr, lincons->eventdatas[pos]) );

   SCIPfreeBlockMemory(scip, &lincons->eventdatas[pos]);

   return SCIP_OKAY;
}

/** locks the rounding locks associated to the given coefficient in the linear constraint */
static
void linconsForbidRounding(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint object */
   VAR*             var,                /**< variable of constraint entry */
   Real             val                 /**< coefficient of constraint entry */
   )
{
   assert(lincons != NULL);
   assert(!SCIPisZero(scip, val));

   if( !lincons->local )
   {
      if( SCIPisPositive(scip, val) )
      {
         if( !SCIPisInfinity(scip, -lincons->lhs) )
            SCIPvarForbidRoundDown(var);
         if( !SCIPisInfinity(scip, lincons->rhs) )
            SCIPvarForbidRoundUp(var);
      }
      else
      {
         if( !SCIPisInfinity(scip, lincons->rhs) )
            SCIPvarForbidRoundDown(var);
         if( !SCIPisInfinity(scip, -lincons->lhs) )
            SCIPvarForbidRoundUp(var);
      }
   }
}

/** unlocks the rounding locks associated to the given coefficient in the linear constraint */
static
void linconsAllowRounding(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint object */
   VAR*             var,                /**< variable of constraint entry */
   Real             val                 /**< coefficient of constraint entry */
   )
{
   assert(lincons != NULL);
   assert(!SCIPisZero(scip, val));

   if( !lincons->local )
   {
      if( SCIPisPositive(scip, val) )
      {
         if( !SCIPisInfinity(scip, -lincons->lhs) )
            SCIPvarAllowRoundDown(var);
         if( !SCIPisInfinity(scip, lincons->rhs) )
            SCIPvarAllowRoundUp(var);
      }
      else
      {
         if( !SCIPisInfinity(scip, lincons->rhs) )
            SCIPvarAllowRoundDown(var);
         if( !SCIPisInfinity(scip, -lincons->lhs) )
            SCIPvarAllowRoundUp(var);
      }
   }
}

/** catches bound change events and locks rounding for variable at given position in transformed linear constraint */
static
RETCODE linconsLockCoef(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint object */
   EVENTHDLR*       eventhdlr,          /**< event handler for bound change events, or NULL */
   int              pos                 /**< position of variable in linear constraint */
   )
{
   assert(scip != NULL);
   assert(lincons != NULL);
   assert(lincons->transformed);
   assert(0 <= pos && pos < lincons->nvars);

   /*debugMessage("locking coefficient %g<%s> in linear constraint\n", val, SCIPvarGetName(var));*/

   if( eventhdlr == NULL )
   {
      /* get event handler for updating linear constraint activity bounds */
      eventhdlr = SCIPfindEventHdlr(scip, EVENTHDLR_NAME);
      if( eventhdlr == NULL )
      {
         errorMessage("event handler for linear constraints not found");
         return SCIP_PLUGINNOTFOUND;
      }
   }

   /* catch bound change events on variable */
   assert(SCIPvarGetStatus(lincons->vars[pos]) != SCIP_VARSTATUS_ORIGINAL);
   CHECK_OKAY( linconsCatchEvent(scip, lincons, eventhdlr, pos) );
   
   /* forbid rounding of variable */
   linconsForbidRounding(scip, lincons, lincons->vars[pos], lincons->vals[pos]);

   return SCIP_OKAY;
}

/** drops bound change events and unlocks rounding for variable at given position in transformed linear constraint */
static
RETCODE linconsUnlockCoef(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint object */
   EVENTHDLR*       eventhdlr,          /**< event handler for bound change events, or NULL */
   int              pos                 /**< position of variable in linear constraint */
   )
{
   assert(scip != NULL);
   assert(lincons != NULL);
   assert(lincons->transformed);
   assert(0 <= pos && pos < lincons->nvars);

   /*debugMessage("unlocking coefficient %g<%s> in linear constraint\n", val, SCIPvarGetName(var));*/

   if( eventhdlr == NULL )
   {
      /* get event handler for updating linear constraint activity bounds */
      eventhdlr = SCIPfindEventHdlr(scip, EVENTHDLR_NAME);
      if( eventhdlr == NULL )
      {
         errorMessage("event handler for linear constraints not found");
         return SCIP_PLUGINNOTFOUND;
      }
   }
   
   /* drop bound change events on variable */
   assert(SCIPvarGetStatus(lincons->vars[pos]) != SCIP_VARSTATUS_ORIGINAL);
   CHECK_OKAY( linconsDropEvent(scip, lincons, eventhdlr, pos) );

   /* allow rounding of variable */
   linconsAllowRounding(scip, lincons, lincons->vars[pos], lincons->vals[pos]);

   return SCIP_OKAY;
}

/** catches bound change events and locks rounding for all variables in transformed linear constraint */
static
RETCODE linconsLockAllCoefs(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons             /**< linear constraint object */
   )
{
   EVENTHDLR* eventhdlr;
   int i;

   assert(scip != NULL);
   assert(lincons != NULL);
   assert(lincons->transformed);

   /* get event handler for updating linear constraint activity bounds */
   eventhdlr = SCIPfindEventHdlr(scip, EVENTHDLR_NAME);
   if( eventhdlr == NULL )
   {
      errorMessage("event handler for linear constraints not found");
      return SCIP_PLUGINNOTFOUND;
   }

   /* lock every single coefficient */
   for( i = 0; i < lincons->nvars; ++i )
   {
      CHECK_OKAY( linconsLockCoef(scip, lincons, eventhdlr, i) );
   }

   return SCIP_OKAY;
}

/** drops bound change events and unlocks rounding for all variables in transformed linear constraint */
static
RETCODE linconsUnlockAllCoefs(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons             /**< linear constraint object */
   )
{
   EVENTHDLR* eventhdlr;
   int i;

   assert(scip != NULL);
   assert(lincons != NULL);
   assert(lincons->transformed);

   /* get event handler for updating linear constraint activity bounds */
   eventhdlr = SCIPfindEventHdlr(scip, EVENTHDLR_NAME);
   if( eventhdlr == NULL )
   {
      errorMessage("event handler for linear constraints not found");
      return SCIP_PLUGINNOTFOUND;
   }

   /* unlock every single coefficient */
   for( i = 0; i < lincons->nvars; ++i )
   {
      CHECK_OKAY( linconsUnlockCoef(scip, lincons, eventhdlr, i) );
   }

   return SCIP_OKAY;
}

/** creates a linear constraint object of the original problem */
static
RETCODE linconsCreate(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS**        lincons,            /**< pointer to linear constraint object */
   int              nvars,              /**< number of nonzeros in the constraint */
   VAR**            vars,               /**< array with variables of constraint entries */
   Real*            vals,               /**< array with coefficients of constraint entries */
   Real             lhs,                /**< left hand side of row */
   Real             rhs,                /**< right hand side of row */
   Bool             modifiable,         /**< is data modifiable during node processing (subject to column generation)? */
   Bool             removeable          /**< should the row be removed from the LP due to aging or cleanup? */
   )
{
   int i;

   assert(lincons != NULL);
   assert(nvars == 0 || vars != NULL);
   assert(nvars == 0 || vals != NULL);

   if( SCIPisGT(scip, lhs, rhs) )
   {
      char s[MAXSTRLEN];
      errorMessage("left hand side of linear constraint greater than right hand side");
      sprintf(s, "  (lhs=%f, rhs=%f)", lhs, rhs);
      errorMessage(s);
      return SCIP_INVALIDDATA;
   }

   CHECK_OKAY( SCIPallocBlockMemory(scip, lincons) );

   if( nvars > 0 )
   {
      CHECK_OKAY( SCIPduplicateBlockMemoryArray(scip, &(*lincons)->vars, vars, nvars) );
      CHECK_OKAY( SCIPduplicateBlockMemoryArray(scip, &(*lincons)->vals, vals, nvars) );
   }
   else
   {
      (*lincons)->vars = NULL;
      (*lincons)->vals = NULL;
   }
   (*lincons)->eventdatas = NULL;

   (*lincons)->lhs = lhs;
   (*lincons)->rhs = rhs;
   (*lincons)->pseudoactivity = SCIP_INVALID;
   (*lincons)->minactivity = SCIP_INVALID;
   (*lincons)->maxactivity = SCIP_INVALID;
   (*lincons)->minactivityinf = -1;
   (*lincons)->maxactivityinf = -1;
   (*lincons)->varssize = nvars;
   (*lincons)->nvars = nvars;
   (*lincons)->local = FALSE;
   (*lincons)->modifiable = modifiable;
   (*lincons)->removeable = removeable;
   (*lincons)->transformed = FALSE;
   (*lincons)->validactivities = FALSE;
   (*lincons)->propagated = FALSE;
   (*lincons)->redchecked = FALSE;
   (*lincons)->sorted = (nvars <= 1);
   
   return SCIP_OKAY;
}

/** creates a linear constraint object of the transformed problem */
static
RETCODE linconsCreateTransformed(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS**        lincons,            /**< pointer to linear constraint object */
   int              nvars,              /**< number of nonzeros in the constraint */
   VAR**            vars,               /**< array with variables of constraint entries */
   Real*            vals,               /**< array with coefficients of constraint entries */
   Real             lhs,                /**< left hand side of row */
   Real             rhs,                /**< right hand side of row */
   Bool             local,              /**< is linear constraint only valid locally? */
   Bool             modifiable,         /**< is row modifiable during node processing (subject to column generation)? */
   Bool             removeable          /**< should the row be removed from the LP due to aging or cleanup? */
   )
{
   int i;

   assert(lincons != NULL);

   /* create linear constraint data */
   CHECK_OKAY( linconsCreate(scip, lincons, nvars, vars, vals, lhs, rhs, modifiable, removeable) );
   (*lincons)->local = local;
   (*lincons)->transformed = TRUE;

   /* allocate the additional needed eventdatas array */
   assert((*lincons)->eventdatas == NULL);
   CHECK_OKAY( SCIPallocBlockMemoryArray(scip, &(*lincons)->eventdatas, (*lincons)->varssize) );

   /* initialize the eventdatas array, transform the variables */
   for( i = 0; i < (*lincons)->nvars; ++i )
   {
      (*lincons)->eventdatas[i] = NULL;
      if( SCIPvarGetStatus((*lincons)->vars[i]) == SCIP_VARSTATUS_ORIGINAL )
      {
         (*lincons)->vars[i] = SCIPvarGetTransformed((*lincons)->vars[i]);
         assert((*lincons)->vars[i] != NULL);
      }
      assert(SCIPvarGetStatus((*lincons)->vars[i]) != SCIP_VARSTATUS_ORIGINAL);
   }

   /* catch bound change events and lock the rounding of variables */
   CHECK_OKAY( linconsLockAllCoefs(scip, *lincons) );

   return SCIP_OKAY;
}

/** frees a linear constraint object */
static
RETCODE linconsFree(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS**        lincons             /**< pointer to linear constraint object */
   )
{
   assert(lincons != NULL);
   assert(*lincons != NULL);
   assert((*lincons)->varssize >= 0);

   if( (*lincons)->transformed )
   {
      /* drop bound change events and unlock the rounding of variables */
      CHECK_OKAY( linconsUnlockAllCoefs(scip, *lincons) );

      /* free additional eventdatas array */
      SCIPfreeBlockMemoryArrayNull(scip, &(*lincons)->eventdatas, (*lincons)->varssize);
   }
   assert((*lincons)->eventdatas == NULL);

   SCIPfreeBlockMemoryArrayNull(scip, &(*lincons)->vars, (*lincons)->varssize);
   SCIPfreeBlockMemoryArrayNull(scip, &(*lincons)->vals, (*lincons)->varssize);
   SCIPfreeBlockMemory(scip, lincons);

   return SCIP_OKAY;
}

/** updates minimum and maximum activity for a change in lower bound */
static
void linconsUpdateChgLb(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint object */
   VAR*             var,                /**< variable that has been changed */
   Real             oldlb,              /**< old lower bound of variable */
   Real             newlb,              /**< new lower bound of variable */
   Real             val                 /**< coefficient of constraint entry */
   )
{
   assert(lincons != NULL);
   assert(lincons->transformed);

   if( lincons->validactivities )
   {
      assert(lincons->pseudoactivity < SCIP_INVALID);
      assert(lincons->minactivity < SCIP_INVALID);
      assert(lincons->maxactivity < SCIP_INVALID);
      assert(lincons->minactivityinf >= 0);
      assert(lincons->maxactivityinf >= 0);
      assert(!SCIPisInfinity(scip, oldlb));
      assert(!SCIPisInfinity(scip, newlb));

      if( SCIPvarGetBestBoundType(var) == SCIP_BOUNDTYPE_LOWER )
         lincons->pseudoactivity += val * (newlb - oldlb);

      if( val > 0.0 )
      {
         if( SCIPisInfinity(scip, -oldlb) )
         {
            assert(lincons->minactivityinf >= 1);
            lincons->minactivityinf--;
         }
         else
            lincons->minactivity -= val * oldlb;

         if( SCIPisInfinity(scip, -newlb) )
            lincons->minactivityinf++;
         else
            lincons->minactivity += val * newlb;
      }
      else
      {
         if( SCIPisInfinity(scip, -oldlb) )
         {
            assert(lincons->maxactivityinf >= 1);
            lincons->maxactivityinf--;
         }
         else
            lincons->maxactivity -= val * oldlb;

         if( SCIPisInfinity(scip, -newlb) )
            lincons->maxactivityinf++;
         else
            lincons->maxactivity += val * newlb;
      }
   }
}

/** updates minimum and maximum activity for a change in upper bound */
static
void linconsUpdateChgUb(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint object */
   VAR*             var,                /**< variable that has been changed */
   Real             oldub,              /**< old upper bound of variable */
   Real             newub,              /**< new upper bound of variable */
   Real             val                 /**< coefficient of constraint entry */
   )
{
   assert(lincons != NULL);
   assert(lincons->transformed);

   if( lincons->validactivities )
   {
      assert(lincons->pseudoactivity < SCIP_INVALID);
      assert(lincons->minactivity < SCIP_INVALID);
      assert(lincons->maxactivity < SCIP_INVALID);
      assert(!SCIPisInfinity(scip, -oldub));
      assert(!SCIPisInfinity(scip, -newub));

      if( SCIPvarGetBestBoundType(var) == SCIP_BOUNDTYPE_UPPER )
         lincons->pseudoactivity += val * (newub - oldub);

      if( val > 0.0 )
      {
         if( SCIPisInfinity(scip, oldub) )
         {
            assert(lincons->maxactivityinf >= 1);
            lincons->maxactivityinf--;
         }
         else
            lincons->maxactivity -= val * oldub;

         if( SCIPisInfinity(scip, newub) )
            lincons->maxactivityinf++;
         else
            lincons->maxactivity += val * newub;
      }
      else
      {
         if( SCIPisInfinity(scip, oldub) )
         {
            assert(lincons->minactivityinf >= 1);
            lincons->minactivityinf--;
         }
         else
            lincons->minactivity -= val * oldub;

         if( SCIPisInfinity(scip, newub) )
            lincons->minactivityinf++;
         else
            lincons->minactivity += val * newub;
      }
   }
}

/** updates minimum and maximum activity for coefficient addition */
static
void linconsUpdateAddCoef(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint object */
   VAR*             var,                /**< variable of constraint entry */
   Real             val                 /**< coefficient of constraint entry */
   )
{
   assert(lincons != NULL);
   assert(lincons->transformed);

   if( lincons->validactivities )
   {
      assert(lincons->pseudoactivity < SCIP_INVALID);
      assert(lincons->minactivity < SCIP_INVALID);
      assert(lincons->maxactivity < SCIP_INVALID);

      linconsUpdateChgLb(scip, lincons, var, 0.0, SCIPvarGetLbLocal(var), val);
      linconsUpdateChgUb(scip, lincons, var, 0.0, SCIPvarGetUbLocal(var), val);
   }
}

/** updates minimum and maximum activity for coefficient deletion */
static
void linconsUpdateDelCoef(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint object */
   VAR*             var,                /**< variable of constraint entry */
   Real             val                 /**< coefficient of constraint entry */
   )
{
   assert(lincons != NULL);
   assert(lincons->transformed);

   if( lincons->validactivities )
   {
      assert(lincons->pseudoactivity < SCIP_INVALID);
      assert(lincons->minactivity < SCIP_INVALID);
      assert(lincons->maxactivity < SCIP_INVALID);

      linconsUpdateChgLb(scip, lincons, var, SCIPvarGetLbLocal(var), 0.0, val);
      linconsUpdateChgUb(scip, lincons, var, SCIPvarGetUbLocal(var), 0.0, val);
   }
}

/** adds coefficient in linear constraint object */
static
RETCODE linconsAddCoef(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint object */
   VAR*             var,                /**< variable of constraint entry */
   Real             val                 /**< coefficient of constraint entry */
   )
{
   assert(lincons != NULL);
   assert(scip != NULL);
   assert(var != NULL);

   if( lincons->transformed && SCIPvarGetStatus(var) == SCIP_VARSTATUS_ORIGINAL )
   {
      var = SCIPvarGetTransformed(var);
      assert(var != NULL);
   }

   assert(lincons->transformed ^ (SCIPvarGetStatus(var) == SCIP_VARSTATUS_ORIGINAL));

   CHECK_OKAY( linconsEnsureVarsSize(scip, lincons, lincons->nvars+1) );
   lincons->vars[lincons->nvars] = var;
   lincons->vals[lincons->nvars] = val;
   lincons->nvars++;

   if( lincons->transformed )
   {
      /* initialize eventdatas array */
      lincons->eventdatas[lincons->nvars-1] = NULL;

      /* catch bound change events and lock the rounding of variable */
      CHECK_OKAY( linconsLockCoef(scip, lincons, NULL, lincons->nvars-1) );

      /* update minimum and maximum activities */
      linconsUpdateAddCoef(scip, lincons, var, val);
   }

   lincons->propagated = FALSE;
   lincons->redchecked = FALSE;
   if( lincons->nvars == 1 )
      lincons->sorted = TRUE;
   else
      lincons->sorted &= (SCIPvarCmp(lincons->vars[lincons->nvars-2], lincons->vars[lincons->nvars-1]) == -1);

   return SCIP_OKAY;
}

/** deletes coefficient at given position from linear constraint object */
static
RETCODE linconsDelCoefPos(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint object */
   int              pos                 /**< position of coefficient to delete */
   )
{
   VAR* var;
   Real val;

   assert(lincons != NULL);
   assert(0 <= pos && pos < lincons->nvars);

   var = lincons->vars[pos];
   val = lincons->vals[pos];
   assert(var != NULL);
   assert(lincons->transformed ^ (SCIPvarGetStatus(var) == SCIP_VARSTATUS_ORIGINAL));

   if( lincons->transformed )
   {
      /* update minimum and maximum activities */
      linconsUpdateDelCoef(scip, lincons, var, val);

      /* drop bound change events and unlock the rounding of variable */
      CHECK_OKAY( linconsUnlockCoef(scip, lincons, NULL, pos) );
      assert(lincons->eventdatas[pos] == NULL);
   }

   /* move the last variable to the free slot */
   lincons->vars[pos] = lincons->vars[lincons->nvars-1];
   lincons->vals[pos] = lincons->vals[lincons->nvars-1];
   if( lincons->transformed && pos != lincons->nvars-1 )
   {
      lincons->eventdatas[pos] = lincons->eventdatas[lincons->nvars-1];
      assert(lincons->eventdatas[pos] != NULL);
      lincons->eventdatas[pos]->varpos = pos;
      lincons->sorted = FALSE;
   }
   lincons->nvars--;

   lincons->propagated = FALSE;
   lincons->redchecked = FALSE;

   return SCIP_OKAY;
}

/** changes coefficient value at given position of linear constraint object */
static
RETCODE linconsChgCoefPos(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint object */
   int              pos,                /**< position of coefficient to delete */
   Real             newval              /**< new value of coefficient */
   )
{
   VAR* var;
   Real val;

   assert(lincons != NULL);
   assert(0 <= pos && pos < lincons->nvars);
   assert(!SCIPisZero(scip, newval));

   var = lincons->vars[pos];
   val = lincons->vals[pos];
   assert(var != NULL);
   assert(lincons->transformed ^ (SCIPvarGetStatus(var) == SCIP_VARSTATUS_ORIGINAL));

   if( lincons->transformed )
   {
      /* update minimum and maximum activities */
      linconsUpdateDelCoef(scip, lincons, var, val);
      linconsUpdateAddCoef(scip, lincons, var, newval);

      /* update rounding locks */
      if( newval * lincons->vals[pos] < 0.0 )
      {
         linconsAllowRounding(scip, lincons, var, val);
         linconsForbidRounding(scip, lincons, var, newval);
      }
   }

   /* change the value */
   lincons->vals[pos] = newval;

   lincons->propagated = FALSE;
   lincons->redchecked = FALSE;

   return SCIP_OKAY;
}

/** creates an LP row from a linear constraint object */
static
RETCODE linconsToRow(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint object */
   const char*      name,               /**< name of the constraint */
   ROW**            row                 /**< pointer to an LP row data object */
   )
{
   int v;

   assert(lincons != NULL);
   assert(lincons->transformed);
   assert(row != NULL);

   CHECK_OKAY( SCIPcreateRow(scip, row, name, 0, NULL, NULL, lincons->lhs, lincons->rhs,
                  lincons->local, lincons->modifiable, lincons->removeable) );
   
   for( v = 0; v < lincons->nvars; ++v )
   {
      CHECK_OKAY( SCIPaddVarToRow(scip, *row, lincons->vars[v], lincons->vals[v]) );
   }

   return SCIP_OKAY;
}

/** calculates pseudo activity, and minimum and maximum activity for constraint */
static
void linconsCalcActivities(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons             /**< linear constraint object */
   )
{
   int i;
   
   assert(lincons != NULL);
   assert(lincons->transformed);
   assert(!lincons->validactivities);
   assert(lincons->pseudoactivity >= SCIP_INVALID);
   assert(lincons->minactivity >= SCIP_INVALID);
   assert(lincons->maxactivity >= SCIP_INVALID);
   
   lincons->validactivities = TRUE;
   lincons->pseudoactivity = 0.0;
   lincons->minactivity = 0.0;
   lincons->maxactivity = 0.0;
   lincons->minactivityinf = 0;
   lincons->maxactivityinf = 0;

   for( i = 0; i < lincons->nvars; ++ i )
      linconsUpdateAddCoef(scip, lincons, lincons->vars[i], lincons->vals[i]);
}

/** gets activity bounds for constraint */
static
Real linconsGetPseudoActivity(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons             /**< linear constraint */
   )
{
   assert(lincons != NULL);

   if( !lincons->validactivities )
      linconsCalcActivities(scip, lincons);
   assert(lincons->pseudoactivity < SCIP_INVALID);
   assert(lincons->minactivity < SCIP_INVALID);
   assert(lincons->maxactivity < SCIP_INVALID);

   debugMessage("pseudo activity of linear constraint: %g\n", lincons->pseudoactivity);

   return lincons->pseudoactivity;
}

/** calculates the feasibility of the linear constraint for given solution */
static
Real linconsGetPseudoFeasibility(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons             /**< linear constraint object */
   )
{
   Real activity;

   assert(lincons != NULL);
   assert(lincons->transformed);

   activity = linconsGetPseudoActivity(scip, lincons);

   return MIN(lincons->rhs - activity, activity - lincons->lhs);
}

/** gets activity bounds for constraint */
static
void linconsGetActivityBounds(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint */
   Real*            minactivity,        /**< pointer to store the minimal activity */
   Real*            maxactivity         /**< pointer to store the maximal activity */
   )
{
   assert(lincons != NULL);
   assert(scip != NULL);
   assert(minactivity != NULL);
   assert(maxactivity != NULL);

   if( !lincons->validactivities )
      linconsCalcActivities(scip, lincons);
   assert(lincons->pseudoactivity < SCIP_INVALID);
   assert(lincons->minactivity < SCIP_INVALID);
   assert(lincons->maxactivity < SCIP_INVALID);

   if( lincons->minactivityinf > 0 )
      *minactivity = -SCIPinfinity(scip);
   else
      *minactivity = lincons->minactivity;
   if( lincons->maxactivityinf > 0 )
      *maxactivity = SCIPinfinity(scip);
   else
      *maxactivity = lincons->maxactivity;
}

/** gets activity bounds for constraint after setting variable to zero */
static
RETCODE linconsGetActivityResiduals(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint */
   VAR*             var,                /**< variable to calculate activity residual for */
   Real             val,                /**< coefficient value of variable in linear constraint */
   Real*            minresactivity,     /**< pointer to store the minimal residual activity */
   Real*            maxresactivity      /**< pointer to store the maximal residual activity */
   )
{
   Real lb;
   Real ub;
   
   assert(lincons != NULL);
   assert(scip != NULL);
   assert(var != NULL);
   assert(minresactivity != NULL);
   assert(maxresactivity != NULL);

   /* get activity bounds of linear constraint */
   if( !lincons->validactivities )
      linconsCalcActivities(scip, lincons);
   assert(lincons->pseudoactivity < SCIP_INVALID);
   assert(lincons->minactivity < SCIP_INVALID);
   assert(lincons->maxactivity < SCIP_INVALID);
   assert(lincons->minactivityinf >= 0);
   assert(lincons->maxactivityinf >= 0);

   lb = SCIPvarGetLbLocal(var);
   ub = SCIPvarGetUbLocal(var);
   assert(!SCIPisInfinity(scip, lb));
   assert(!SCIPisInfinity(scip, -ub));

   if( val > 0.0 )
   {
      if( SCIPisInfinity(scip, -lb) )
      {
         assert(lincons->minactivityinf >= 1);
         if( lincons->minactivityinf >= 2 )
            *minresactivity = -SCIPinfinity(scip);
         else
            *minresactivity = lincons->minactivity;
      }
      else
      {
         if( lincons->minactivityinf >= 1 )
            *minresactivity = -SCIPinfinity(scip);
         else
            *minresactivity = lincons->minactivity - val * lb;
      }
      if( SCIPisInfinity(scip, ub) )
      {
         assert(lincons->maxactivityinf >= 1);
         if( lincons->maxactivityinf >= 2 )
            *maxresactivity = +SCIPinfinity(scip);
         else
            *maxresactivity = lincons->maxactivity;
      }
      else
      {
         if( lincons->maxactivityinf >= 1 )
            *maxresactivity = +SCIPinfinity(scip);
         else
            *maxresactivity = lincons->maxactivity - val * ub;
      }
   }
   else
   {
      if( SCIPisInfinity(scip, ub) )
      {
         assert(lincons->minactivityinf >= 1);
         if( lincons->minactivityinf >= 2 )
            *minresactivity = -SCIPinfinity(scip);
         else
            *minresactivity = lincons->minactivity;
      }
      else
      {
         if( lincons->minactivityinf >= 1 )
            *minresactivity = -SCIPinfinity(scip);
         else
            *minresactivity = lincons->minactivity - val * ub;
      }
      if( SCIPisInfinity(scip, -lb) )
      {
         assert(lincons->maxactivityinf >= 1);
         if( lincons->maxactivityinf >= 2 )
            *maxresactivity = +SCIPinfinity(scip);
         else
            *maxresactivity = lincons->maxactivity;
      }
      else
      {
         if( lincons->maxactivityinf >= 1 )
            *maxresactivity = +SCIPinfinity(scip);
         else
            *maxresactivity = lincons->maxactivity - val * lb;
      }
   }

   return SCIP_OKAY;
}

/** invalidates pseudo activity and activity bounds, such that they are recalculated in next get */
static
void linconsInvalidateActivities(
   LINCONS*         lincons             /**< linear constraint */
   )
{
   assert(lincons != NULL);

   lincons->validactivities = FALSE;
   lincons->pseudoactivity = SCIP_INVALID;
   lincons->minactivity = SCIP_INVALID;
   lincons->maxactivity = SCIP_INVALID;
   lincons->minactivityinf = -1;
   lincons->maxactivityinf = -1;
}

/** calculates the activity of the linear constraint for given solution */
static
Real linconsGetActivity(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint object */
   SOL*             sol                 /**< solution to get activity for, NULL to actual solution */
   )
{
   Real activity;
   Real infinity;

   assert(lincons != NULL);
   assert(lincons->transformed);

   if( sol == NULL && !SCIPhasActnodeLP(scip) )
   {
      /* for performance reasons, the pseudo activity is updated with each bound change, so we don't have to
       * recalculate it
       */
      activity = linconsGetPseudoActivity(scip, lincons);
   }
   else
   {
      Real solval;
      int v;

      activity = 0.0;
      for( v = 0; v < lincons->nvars; ++v )
      {
         solval = SCIPgetSolVal(scip, sol, lincons->vars[v]);
         activity += lincons->vals[v] * solval;
      }

      debugMessage("activity of linear constraint: %g\n", activity);
   }

   infinity = SCIPinfinity(scip);
   activity = MAX(activity, -infinity);
   activity = MIN(activity, +infinity);

   return activity;
}

/** calculates the feasibility of the linear constraint for given solution */
static
Real linconsGetFeasibility(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint object */
   SOL*             sol                 /**< solution to get feasibility for, NULL to actual solution */
   )
{
   Real activity;

   assert(lincons != NULL);
   assert(lincons->transformed);

   activity = linconsGetActivity(scip, lincons, sol);

   return MIN(lincons->rhs - activity, activity - lincons->lhs);
}

/** tightens bounds of a single variable due to activity bounds */
static
RETCODE linconsTightenVarBounds(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< constraint data */
   VAR*             var,                /**< variable to tighten bounds for */
   Real             val,                /**< coefficient value of variable in linear constraint */
   int*             nchgbds,            /**< pointer to count the total number of tightened bounds */
   RESULT*          result              /**< pointer to store SCIP_CUTOFF, if node is infeasible */
   )
{
   Real lb;
   Real ub;
   Real newlb;
   Real newub;
   Real minresactivity;
   Real maxresactivity;
   Real lhs;
   Real rhs;

   assert(lincons != NULL);
   assert(!lincons->modifiable);
   assert(var != NULL);
   assert(!SCIPisZero(scip, val));
   assert(nchgbds != NULL);
   assert(result != NULL);

   lhs = lincons->lhs;
   rhs = lincons->rhs;
   CHECK_OKAY( linconsGetActivityResiduals(scip, lincons, var, val, &minresactivity, &maxresactivity) );
   assert(!SCIPisInfinity(scip, lhs));
   assert(!SCIPisInfinity(scip, -rhs));
   assert(!SCIPisInfinity(scip, minresactivity));
   assert(!SCIPisInfinity(scip, -maxresactivity));
   
   lb = SCIPvarGetLbLocal(var);
   ub = SCIPvarGetUbLocal(var);
   assert(SCIPisLE(scip, lb, ub));

   if( val > 0.0 )
   {
      /* check, if we can tighten the variable's bounds */
      if( !SCIPisInfinity(scip, -minresactivity) && !SCIPisInfinity(scip, rhs) )
      {
         newub = (rhs - minresactivity)/val;
         if( SCIPisSumLT(scip, newub, ub) )
         {
            /* tighten upper bound */
            debugMessage("linear constraint: tighten <%s>, old bds=[%f,%f], val=%g, resactivity=[%g,%g], sides=[%g,%g]\n",
               SCIPvarGetName(var), lb, ub, val, minresactivity, maxresactivity, lhs, rhs);
            if( SCIPisSumLT(scip, newub, lb) )
            {
               debugMessage("linear constraint: cutoff  <%s>, new bds=[%f,%f]\n", SCIPvarGetName(var), lb, newub);
               *result = SCIP_CUTOFF;
               return SCIP_OKAY;
            }
            CHECK_OKAY( SCIPchgVarUb(scip, var, newub) );
            ub = SCIPvarGetUbLocal(var); /* get bound again, because it may be additionally modified due to integrality */
            assert(SCIPisFeasLE(scip, ub, newub));
            (*nchgbds)++;
            *result = SCIP_REDUCEDDOM;
            debugMessage("linear constraint: tighten <%s>, new bds=[%f,%f]\n", SCIPvarGetName(var), lb, ub);
         }
      }
      if( !SCIPisInfinity(scip, maxresactivity) && !SCIPisInfinity(scip, -lhs) )
      {
         newlb = (lhs - maxresactivity)/val;
         if( SCIPisSumGT(scip, newlb, lb) )
         {
            /* tighten lower bound */
            debugMessage("linear constraint: tighten <%s>, old bds=[%f,%f], val=%g, resactivity=[%g,%g], sides=[%g,%g]\n",
               SCIPvarGetName(var), lb, ub, val, minresactivity, maxresactivity, lhs, rhs);
            if( SCIPisSumGT(scip, newlb, ub) )
            {
               debugMessage("linear constraint: cutoff  <%s>, new bds=[%f,%f]\n", SCIPvarGetName(var), newlb, ub);
               *result = SCIP_CUTOFF;
               return SCIP_OKAY;
            }
            CHECK_OKAY( SCIPchgVarLb(scip, var, newlb) );
            lb = SCIPvarGetLbLocal(var); /* get bound again, because it may be additionally modified due to integrality */
            assert(SCIPisFeasGE(scip, lb, newlb));
            (*nchgbds)++;
            *result = SCIP_REDUCEDDOM;
            debugMessage("linear constraint: tighten <%s>, new bds=[%f,%f]\n", SCIPvarGetName(var), lb, ub);
         }
      }
   }
   else
   {
      /* check, if we can tighten the variable's bounds */
      if( !SCIPisInfinity(scip, -minresactivity) && !SCIPisInfinity(scip, rhs) )
      {
         newlb = (rhs - minresactivity)/val;
         if( SCIPisSumGT(scip, newlb, lb) )
         {
            /* tighten lower bound */
            debugMessage("linear constraint: tighten <%s>, old bds=[%f,%f], val=%g, resactivity=[%g,%g], sides=[%g,%g]\n",
               SCIPvarGetName(var), lb, ub, val, minresactivity, maxresactivity, lhs, rhs);
            if( SCIPisSumGT(scip, newlb, ub) )
            {
               debugMessage("linear constraint: cutoff  <%s>, new bds=[%f,%f]\n", SCIPvarGetName(var), newlb, ub);
               *result = SCIP_CUTOFF;
               return SCIP_OKAY;
            }
            CHECK_OKAY( SCIPchgVarLb(scip, var, newlb) );
            lb = SCIPvarGetLbLocal(var); /* get bound again, because it may be additionally modified due to integrality */
            assert(SCIPisFeasGE(scip, lb, newlb));
            (*nchgbds)++;
            *result = SCIP_REDUCEDDOM;
            debugMessage("linear constraint: tighten <%s>, new bds=[%f,%f]\n", SCIPvarGetName(var), lb, ub);
         }
      }
      if( !SCIPisInfinity(scip, maxresactivity) && !SCIPisInfinity(scip, -lhs) )
      {
         newub = (lhs - maxresactivity)/val;
         if( SCIPisSumLT(scip, newub, ub) )
         {
            /* tighten upper bound */
            debugMessage("linear constraint: tighten <%s>, old bds=[%f,%f], val=%g, resactivity=[%g,%g], sides=[%g,%g]\n",
               SCIPvarGetName(var), lb, ub, val, minresactivity, maxresactivity, lhs, rhs);
            if( SCIPisSumLT(scip, newub, lb) )
            {
               debugMessage("linear constraint: cutoff  <%s>, new bds=[%f,%f]\n", SCIPvarGetName(var), lb, newub);
               *result = SCIP_CUTOFF;
               return SCIP_OKAY;
            }
            CHECK_OKAY( SCIPchgVarUb(scip, var, newub) );
            ub = SCIPvarGetUbLocal(var); /* get bound again, because it may be additionally modified due to integrality */
            assert(SCIPisFeasLE(scip, ub, newub));
            (*nchgbds)++;
            *result = SCIP_REDUCEDDOM;
            debugMessage("linear constraint: tighten <%s>, new bds=[%f,%f]\n", SCIPvarGetName(var), lb, ub);
         }
      }
   }
   
   return SCIP_OKAY;
}

/** tightens variable's bounds due to activity bounds */
static
RETCODE linconsTightenBounds(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint object */
   int*             nchgbds,            /**< pointer to count the total number of tightened bounds */
   RESULT*          result              /**< pointer to store the result of the bound tightening */
   )
{
   VAR** vars;
   Real* vals;
   int nvars;

   assert(lincons != NULL);
   assert(scip != NULL);
   assert(nchgbds != NULL);
   assert(result != NULL);
   assert(*result != SCIP_CUTOFF);

   /* we cannot tighten variables' bounds, if the constraint may be not complete */
   if( lincons->modifiable )
      return SCIP_OKAY;

   nvars = lincons->nvars;
   if( nvars > 0 )
   {
      int lastnchgbds;
      int lastsuccess;
      int v;
   
      vars = lincons->vars;
      vals = lincons->vals;
      assert(vars != NULL);
      assert(vals != NULL);
      lastsuccess = 0;
      v = 0;
      do
      {
         assert(0 <= v && v < nvars);
         lastnchgbds = *nchgbds;
         CHECK_OKAY( linconsTightenVarBounds(scip, lincons, vars[v], vals[v], nchgbds, result) );
         if( *nchgbds > lastnchgbds )
            lastsuccess = v;
         v++;
         if( v == nvars )
            v = 0;
      }
      while( v != lastsuccess && *result != SCIP_CUTOFF );
   }

   return SCIP_OKAY;
}

/** sets left hand side of linear constraint */
static
void linconsChgLhs(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint */
   Real             lhs                 /**< new left hand side */
   )
{
   assert(lincons != NULL);
   assert(lincons->nvars == 0 || (lincons->vars != NULL && lincons->vals != NULL));
   assert(!SCIPisInfinity(scip, lincons->lhs));
   assert(!SCIPisInfinity(scip, lhs));

   /* if necessary, update the rounding locks of variables */
   if( lincons->transformed && !lincons->local )
   {
      if( SCIPisInfinity(scip, -lincons->lhs) && !SCIPisInfinity(scip, -lhs) )
      {
         VAR** vars;
         Real* vals;
         int v;
   
         /* the left hand side switched from -infinity to a non-infinite value -> forbid rounding */
         vars = lincons->vars;
         vals = lincons->vals;
         
         for( v = 0; v < lincons->nvars; ++v )
         {
            assert(vars[v] != NULL);
            
            if( SCIPisPositive(scip, vals[v]) )
               SCIPvarForbidRoundDown(vars[v]);
            else
            {
               assert(SCIPisNegative(scip, vals[v]));
               SCIPvarForbidRoundUp(vars[v]);
            }
         }
      }
      else if( !SCIPisInfinity(scip, -lincons->lhs) && SCIPisInfinity(scip, -lhs) )
      {
         VAR** vars;
         Real* vals;
         int v;
   
         /* the left hand side switched from a non-infinte value to -infinity -> allow rounding */
         vars = lincons->vars;
         vals = lincons->vals;
         
         for( v = 0; v < lincons->nvars; ++v )
         {
            assert(vars[v] != NULL);
            
            if( SCIPisPositive(scip, vals[v]) )
               SCIPvarAllowRoundDown(vars[v]);
            else
            {
               assert(SCIPisNegative(scip, vals[v]));
               SCIPvarAllowRoundUp(vars[v]);
            }
         }
      }
   }

   /* set new left hand side */
   lincons->lhs = lhs;
   lincons->propagated = FALSE;
   lincons->redchecked = FALSE;
}

/** sets right hand side of linear constraint */
static
void linconsChgRhs(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint */
   Real             rhs                 /**< new right hand side */
   )
{
   assert(lincons != NULL);
   assert(lincons->nvars == 0 || (lincons->vars != NULL && lincons->vals != NULL));
   assert(!SCIPisInfinity(scip, -lincons->rhs));
   assert(!SCIPisInfinity(scip, -rhs));

   /* if necessary, update the rounding locks of variables */
   if( lincons->transformed && !lincons->local )
   {
      if( SCIPisInfinity(scip, lincons->rhs) && !SCIPisInfinity(scip, rhs) )
      {
         VAR** vars;
         Real* vals;
         int v;
   
         /* the right hand side switched from infinity to a non-infinite value -> forbid rounding */
         vars = lincons->vars;
         vals = lincons->vals;
         
         for( v = 0; v < lincons->nvars; ++v )
         {
            assert(vars[v] != NULL);
            
            if( SCIPisPositive(scip, vals[v]) )
               SCIPvarForbidRoundUp(vars[v]);
            else
            {
               assert(SCIPisNegative(scip, vals[v]));
               SCIPvarForbidRoundDown(vars[v]);
            }
         }
      }
      else if( !SCIPisInfinity(scip, lincons->rhs) && SCIPisInfinity(scip, rhs) )
      {
         VAR** vars;
         Real* vals;
         int v;
   
         /* the right hand side switched from a non-infinte value to infinity -> allow rounding */
         vars = lincons->vars;
         vals = lincons->vals;
         
         for( v = 0; v < lincons->nvars; ++v )
         {
            assert(vars[v] != NULL);
            
            if( SCIPisPositive(scip, vals[v]) )
               SCIPvarAllowRoundUp(vars[v]);
            else
            {
               assert(SCIPisNegative(scip, vals[v]));
               SCIPvarAllowRoundDown(vars[v]);
            }
         }
      }
   }

   /* set new right hand side */
   lincons->rhs = rhs;
   lincons->propagated = FALSE;
   lincons->redchecked = FALSE;
}

/** prints linear constraint to file stream */
static
void linconsPrint(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint object */
   FILE*            file                /**< output file (or NULL for standard output) */
   )
{
   int v;

   assert(lincons != NULL);

   if( file == NULL )
      file = stdout;

   /* print left hand side for ranged rows */
   if( !SCIPisInfinity(scip, -lincons->lhs)
      && !SCIPisInfinity(scip, lincons->rhs)
      && !SCIPisEQ(scip, lincons->lhs, lincons->rhs) )
      fprintf(file, "%+g <= ", lincons->lhs);

   /* print coefficients */
   if( lincons->nvars == 0 )
      fprintf(file, "0 ");
   for( v = 0; v < lincons->nvars; ++v )
   {
      assert(lincons->vars[v] != NULL);
      fprintf(file, "%+g%s ", lincons->vals[v], SCIPvarGetName(lincons->vars[v]));
   }

   /* print right hand side */
   if( SCIPisEQ(scip, lincons->lhs, lincons->rhs) )
      fprintf(file, "= %+g\n", lincons->rhs);
   else if( !SCIPisInfinity(scip, lincons->rhs) )
      fprintf(file, "<= %+g\n", lincons->rhs);
   else if( !SCIPisInfinity(scip, -lincons->lhs) )
      fprintf(file, ">= %+g\n", lincons->lhs);
   else
      fprintf(file, " [free]\n");
}

/** index comparison method of linear constraints: compares two indices of the variable set in the linear constraint */
static
DECL_SORTINDCOMP(linconsCmpVar)
{
   LINCONS* lincons = (LINCONS*)dataptr;

   assert(lincons != NULL);
   assert(0 <= ind1 && ind1 < lincons->nvars);
   assert(0 <= ind2 && ind2 < lincons->nvars);
   
   return SCIPvarCmp(lincons->vars[ind1], lincons->vars[ind2]);
}

/** sorts linear constraint's variables */
static
RETCODE linconsSort(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons             /**< linear constraint object */
   )
{
   assert(lincons != NULL);

   if( lincons->nvars == 0 )
      lincons->sorted = TRUE;
   else if( !lincons->sorted )
   {
      VAR* varv;
      EVENTDATA* eventdatav;
      Real valv;
      int* perm;
      int v;
      int i;
      int nexti;

      /* get temporary memory to store the sorted permutation */
      CHECK_OKAY( SCIPcaptureBufferArray(scip, &perm, lincons->nvars) );

      /* call bubble sort */
      SCIPbsort((void*)lincons, lincons->nvars, linconsCmpVar, perm);

      /* permute the variables in the linear constraint according to the resulting permutation */
      for( v = 0; v < lincons->nvars; ++v )
      {
         if( perm[v] != v )
         {
            varv = lincons->vars[v];
            valv = lincons->vals[v];
            eventdatav = lincons->eventdatas[v];
            i = v;
            do
            {
               assert(0 <= perm[i] && perm[i] < lincons->nvars);
               assert(perm[i] != i);
               lincons->vars[i] = lincons->vars[perm[i]];
               lincons->vals[i] = lincons->vals[perm[i]];
               lincons->eventdatas[i] = lincons->eventdatas[perm[i]];
               lincons->eventdatas[i]->varpos = i;
               nexti = perm[i];
               perm[i] = i;
               i = nexti;
            }
            while( perm[i] != v );
            lincons->vars[i] = varv;
            lincons->vals[i] = valv;
            lincons->eventdatas[i] = eventdatav;
            lincons->eventdatas[i]->varpos = i;
            perm[i] = i;
         }
      }
      lincons->sorted = TRUE;

#ifdef DEBUG
      /* check sorting */
      for( v = 0; v < lincons->nvars; ++v )
      {
         assert(v == lincons->nvars-1 || SCIPvarCmp(lincons->vars[v], lincons->vars[v+1]) <= 0);
         assert(perm[v] == v);
         assert(lincons->eventdatas[v]->varpos == v);
      }
#endif

      /* free temporary memory */
      CHECK_OKAY( SCIPreleaseBufferArray(scip, &perm) );
   }
   assert(lincons->sorted);

   return SCIP_OKAY;
}




/*
 * local linear constraint handler methods
 */

/* gets linear constraint data from constraint object */
static
LINCONS* consGetLincons(
   CONS*            cons                /**< linear constraint */
   )
{
   CONSDATA* consdata;

   assert(cons != NULL);
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   assert(consdata->lincons != NULL);

   return consdata->lincons;
}

/** checks linear constraint for feasibility of given solution or actual pseudo solution */
static
RETCODE check(
   SCIP*            scip,               /**< SCIP data structure */
   CONS*            cons,               /**< linear constraint */
   SOL*             sol,                /**< solution to be checked, or NULL for actual pseudo solution */
   Bool             checklprows,        /**< has linear constraint to be checked, if it is already in current LP? */
   Real*            violation,          /**< pointer to store the constraint's violation, or NULL */
   Bool*            violated            /**< pointer to store whether the constraint is violated */
   )
{
   CONSDATA* consdata;
   ROW* row;
   Real feasibility;

   assert(cons != NULL);
   assert(violated != NULL);

   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   debugMessage("checking linear constraint <%s>\n", SCIPconsGetName(cons));
   debug(linconsPrint(scip, consdata->lincons, NULL));

   *violated = FALSE;

   row = consdata->row;
   if( row != NULL )
   {
      if( !checklprows && SCIProwIsInLP(row) )
         return SCIP_OKAY;
      else if( sol == NULL && !SCIPhasActnodeLP(scip) )
         feasibility = linconsGetPseudoFeasibility(scip, consdata->lincons);
      else
         feasibility = SCIPgetRowSolFeasibility(scip, row, sol);
   }
   else
      feasibility = linconsGetFeasibility(scip, consdata->lincons, sol);
   
   debugMessage("  lincons feasibility = %g (lhs=%g, rhs=%g, row=%p, checklprows=%d, rowisinlp=%d, sol=%p, hasactnodelp=%d)\n",
      feasibility, consdata->lincons->lhs, consdata->lincons->rhs, 
      row, checklprows, row == NULL ? -1 : SCIProwIsInLP(row), sol, SCIPhasActnodeLP(scip));

   if( SCIPisFeasible(scip, feasibility) )
   {
      *violated = FALSE;
      CHECK_OKAY( SCIPincConsAge(scip, cons) );
   }
   else
   {
      *violated = TRUE;
      CHECK_OKAY( SCIPresetConsAge(scip, cons) );
   }

   if( violation != NULL )
      *violation = -feasibility;
   
   return SCIP_OKAY;
}

/** separates linear constraint: adds linear constraint as cut, if violated by current LP solution */
static
RETCODE separate(
   SCIP*            scip,               /**< SCIP data structure */
   CONS*            cons,               /**< linear constraint */
   RESULT*          result              /**< pointer to store result of separation */
   )
{
   Real violation;
   Bool violated;

   assert(cons != NULL);
   assert(result != NULL);

   CHECK_OKAY( check(scip, cons, NULL, FALSE, &violation, &violated) );

   if( violated )
   {
      CONSDATA* consdata;
      ROW* row;

      consdata = SCIPconsGetData(cons);
      assert(consdata != NULL);

      if( consdata->row == NULL )
      {
         /* convert lincons object into LP row */
         CHECK_OKAY( linconsToRow(scip, consdata->lincons, SCIPconsGetName(cons), &consdata->row) );
      }
      row = consdata->row;
      assert(row != NULL);

      /* insert LP row as cut */
      CHECK_OKAY( SCIPaddCut(scip, row, violation/SCIProwGetNorm(row)/(SCIProwGetNNonz(row)+1)) );
      *result = SCIP_SEPARATED;
   }

   return SCIP_OKAY;
}




/*
 * Callback methods of constraint handler
 */

static
DECL_CONSFREE(consFreeLinear)
{
   CONSHDLRDATA* conshdlrdata;

   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(scip != NULL);

   /* free constraint handler data */
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   conshdlrdataFree(scip, &conshdlrdata);

   SCIPconshdlrSetData(conshdlr, NULL);

   return SCIP_OKAY;
}

static
DECL_CONSDELETE(consDeleteLinear)
{
   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(scip != NULL);
   assert(consdata != NULL);
   assert(*consdata != NULL);

   /* free linear constraint */
   CHECK_OKAY( linconsFree(scip, &(*consdata)->lincons) );

   /* release the row */
   if( (*consdata)->row != NULL )
   {
      CHECK_OKAY( SCIPreleaseRow(scip, &(*consdata)->row) );
   }

   /* free constraint data object */
   SCIPfreeBlockMemory(scip, consdata);

   return SCIP_OKAY;
}

/** scales a linear constraint with a constant scalar */
static
void linconsScale(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint to scale */
   Real             scalar              /**< value to scale constraint with */
   )
{
   Real newval;
   int i;

   assert(lincons != NULL);

   /* scale the sides */
   if( scalar < 0.0 )
   {
      Real lhs;
      lhs = lincons->lhs;
      lincons->lhs = -lincons->rhs;
      lincons->rhs = -lhs;
   }
   if( !SCIPisInfinity(scip, -lincons->lhs) )
   {
      lincons->lhs *= ABS(scalar);
      if( SCIPisIntegral(scip, lincons->lhs) )
         lincons->lhs = SCIPfloor(scip, lincons->lhs);
   }
   if( !SCIPisInfinity(scip, lincons->rhs) )
   {
      lincons->rhs *= ABS(scalar);
      if( SCIPisIntegral(scip, lincons->rhs) )
         lincons->rhs = SCIPfloor(scip, lincons->rhs);
   }

   /* scale the coefficients */
   for( i = 0; i < lincons->nvars; ++i )
   {
      lincons->vals[i] *= scalar;
      if( SCIPisIntegral(scip, lincons->vals[i]) )
         lincons->vals[i] = SCIPfloor(scip, lincons->vals[i]);
   }

   lincons->validactivities = FALSE;
}

/** normalizes a linear constraint with the following rules:
 *  - multiplication with +1 or -1:
 *      Apply the following rules in the given order, until the sign of the factor is determined. Later rules only apply,
 *      if the actual rule doesn't determine the sign):
 *        1. the number of positive coefficients must not be smaller than the number of negative coefficients
 *        2. the right hand side must not be infinite
 *        3. the absolute value of the right hand side must be greater than that of the left hand side
 *        4. multiply with +1
 *  - rationals to integrals
 *      Try to identify a rational representation of the fractional coefficients, and multiply all coefficients
 *      by the smallest common multiple of all denominators to get integral coefficients.
 *      Forbid large denominators due to numerical stability.
 *  - division by greatest common divisor
 *      If all coefficients are integral, divide them by the greatest common divisor.
 */
static
RETCODE linconsNormalize(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons             /**< linear constraint to normalize */
   )
{
   VAR** vars;
   Real* vals;
   Longint scm;
   Longint nominator;
   Longint denominator;
   Longint gcd;
   Longint maxmult;
   Real epsilon;
   Real feastol;
   Bool success;
   int nvars;
   int mult;
   int nposcoeffs;
   int nnegcoeffs;
   int i;

   assert(lincons != NULL);

   /* coefficients of modifiable constraint must not be changed */
   if( lincons->modifiable )
      return SCIP_OKAY;

   vars = lincons->vars;
   vals = lincons->vals;
   nvars = lincons->nvars;
   assert(nvars == 0 || vars != NULL);
   assert(nvars == 0 || vals != NULL);

   /* calculate the maximal multiplier for common divisor calculation:
    *   |p/q - val| < epsilon  and  q < feastol/epsilon  =>  |p - q*val| < feastol
    * which means, a value of feastol/epsilon should be used as maximal multiplier
    */
   epsilon = SCIPepsilon(scip);
   feastol = SCIPfeastol(scip);
   maxmult = (Longint)(feastol/epsilon + feastol);

   /*
    * multiplication with +1 or -1
    */
   mult = 0;
   
   if( mult == 0 )
   {
      /* 1. the number of positive coefficients must not be smaller than the number of negative coefficients */
      nposcoeffs = 0;
      nnegcoeffs = 0;
      for( i = 0; i < nvars; ++i )
      {
         if( vals[i] > 0.0 )
            nposcoeffs++;
         else
            nnegcoeffs++;
      }
      if( nposcoeffs > nnegcoeffs )
         mult = +1;
      else if( nposcoeffs < nnegcoeffs )
         mult = -1;
   }

   if( mult == 0 )
   {
      /* 2. the right hand side must not be infinite */
      if( SCIPisInfinity(scip, -lincons->lhs) )
         mult = +1;
      else if( SCIPisInfinity(scip, lincons->rhs) )
         mult = -1;
   }

   if( mult == 0 )
   {
      /* 3. the absolute value of the right hand side must be greater than that of the left hand side */
      if( SCIPisGT(scip, ABS(lincons->rhs), ABS(lincons->lhs)) )
         mult = +1;
      else if( SCIPisLT(scip, ABS(lincons->rhs), ABS(lincons->lhs)) )
         mult = -1;
   }
   
   if( mult == 0 )
   {
      /* 4. multiply with +1 */
      mult = +1;
   }

   assert(mult == +1 || mult == -1);
   if( mult == -1 )
   {
      /* scale the constraint with -1 */
      debugMessage("multiply linear constraint with -1.0\n");
      debug(linconsPrint(scip, lincons, NULL));
      linconsScale(scip, lincons, -1.0);
   }

   /*
    * rationals to integrals
    */
   success = TRUE;
   scm = 1;
   for( i = 0; i < nvars && success && scm <= maxmult; ++i )
   {
      if( !SCIPisIntegral(scip, vals[i]) )
      {
         success = SCIPrealToRational(vals[i], epsilon, maxmult, &nominator, &denominator);
         if( success )
            scm = SCIPcalcSmaComMul(scm, denominator);
      }
   }
   assert(scm >= 1);
   success &= (scm <= maxmult);
   if( success && scm != 1 )
   {
      /* scale the constraint with the smallest common multiple of all denominators */
      debugMessage("scale linear constraint with %lld to make coefficients integral\n", scm);
      debug(linconsPrint(scip, lincons, NULL));
      linconsScale(scip, lincons, (Real)scm);
   }

   /*
    * division by greatest common divisor
    */
   if( success && nvars >= 1 )
   {
      /* all coefficients are integral: divide them by their greatest common divisor */
      assert(SCIPisIntegral(scip, vals[0]));
      gcd = (Longint)(ABS(vals[0]) + feastol);
      assert(gcd >= 1);
      for( i = 1; i < nvars && gcd > 1; ++i )
      {
         assert(SCIPisIntegral(scip, vals[i]));
         gcd = SCIPcalcGreComDiv(gcd, (Longint)(ABS(vals[i]) + feastol));
      }

      if( gcd > 1 )
      {
         /* divide the constaint by the greatest common divisor of the coefficients */
         debugMessage("divide linear constraint by greatest common divisor %lld\n", gcd);
         debug(linconsPrint(scip, lincons, NULL));
         linconsScale(scip, lincons, 1.0/(Real)gcd);
      }
   }
   
   debugMessage("normalized constraint:\n");
   debug(linconsPrint(scip, lincons, NULL));

   return SCIP_OKAY;
}

static
DECL_CONSTRANS(consTransLinear)
{
   CONSDATA* sourcedata;
   CONSDATA* targetdata;
   LINCONS* lincons;
   CONS* upgdcons;

   /*debugMessage("Trans method of linear constraints\n");*/

   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(scip != NULL);
   assert(SCIPstage(scip) == SCIP_STAGE_INITSOLVE);
   assert(sourcecons != NULL);
   assert(targetcons != NULL);

   sourcedata = SCIPconsGetData(sourcecons);
   assert(sourcedata != NULL);
   assert(sourcedata->lincons != NULL);
   assert(sourcedata->row == NULL);  /* in original problem, there cannot be LP rows */

   /* create constraint data for target constraint */
   CHECK_OKAY( SCIPallocBlockMemory(scip, &targetdata) );
   targetdata->row = NULL;

   /* create linear constraint object */
   lincons = sourcedata->lincons;
   CHECK_OKAY( linconsCreateTransformed(scip, &targetdata->lincons,
                  lincons->nvars, lincons->vars, lincons->vals, lincons->lhs, lincons->rhs,
                  lincons->local, lincons->modifiable, lincons->removeable) );

   /* normalize constraint */
   CHECK_OKAY( linconsNormalize(scip, targetdata->lincons) );

   /* create target constraint */
   CHECK_OKAY( SCIPcreateCons(scip, targetcons, SCIPconsGetName(sourcecons), conshdlr, targetdata,
                  SCIPconsIsSeparated(sourcecons), SCIPconsIsEnforced(sourcecons), SCIPconsIsChecked(sourcecons),
                  SCIPconsIsPropagated(sourcecons)) );

   /* try to upgrade target linear constraint into more specific constraint */
   CHECK_OKAY( SCIPupgradeConsLinear(scip, *targetcons, &upgdcons) );
   
   /* if upgrading was successful, release the old constraint and use the upgraded constraint instead */
   if( upgdcons != NULL )
   {
      CHECK_OKAY( SCIPreleaseCons(scip, targetcons) );
      *targetcons = upgdcons;
   }

   return SCIP_OKAY;
}

static
DECL_CONSSEPA(consSepaLinear)
{
   Bool found;
   int c;

   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(scip != NULL);
   assert(result != NULL);

   /*debugMessage("Sepa method of linear constraints\n");*/

   *result = SCIP_DIDNOTFIND;

   /* step 1: check all useful linear constraints for feasibility */
   for( c = 0; c < nusefulconss; ++c )
   {
      /*debugMessage("separating linear constraint <%s>\n", SCIPconsGetName(conss[c]));*/
      CHECK_OKAY( separate(scip, conss[c], result) );
   }

   /* step 2: combine linear constraints to get more cuts */
   todoMessage("further cuts of linear constraints");

   /* step 3: if no cuts were found and we are in the root node, check remaining linear constraints for feasibility */
   if( SCIPgetActDepth(scip) == 0 )
   {
      for( c = nusefulconss; c < nconss && *result == SCIP_DIDNOTFIND; ++c )
      {
         /*debugMessage("separating linear constraint <%s>\n", SCIPconsGetName(conss[c]));*/
         CHECK_OKAY( separate(scip, conss[c], result) );
      }
   }

   return SCIP_OKAY;
}

static
DECL_CONSENFOLP(consEnfolpLinear)
{
   int c;

   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(scip != NULL);
   assert(result != NULL);

   /*debugMessage("Enfolp method of linear constraints\n");*/

   /* check for violated constraints
    * LP is processed at current node -> we can add violated linear constraints to the LP */

   *result = SCIP_FEASIBLE;

   /* step 1: check all useful linear constraints for feasibility */
   for( c = 0; c < nusefulconss; ++c )
   {
      /*debugMessage("separating linear constraint <%s>\n", SCIPconsGetName(conss[c]));*/
      CHECK_OKAY( separate(scip, conss[c], result) );
   }
   if( *result != SCIP_FEASIBLE )
      return SCIP_OKAY;

   /* step 2: check all obsolete linear constraints for feasibility */
   for( c = nusefulconss; c < nconss && *result == SCIP_FEASIBLE; ++c )
   {
      CHECK_OKAY( separate(scip, conss[c], result) );
   }

   return SCIP_OKAY;
}

static
DECL_CONSENFOPS(consEnfopsLinear)
{
   Bool violated;
   int c;

   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(scip != NULL);
   assert(result != NULL);

   /*debugMessage("Enfops method of linear constraints\n");*/

   /* if the solution is infeasible anyway due to objective value, skip the enforcement */
   if( objinfeasible )
   {
      *result = SCIP_DIDNOTRUN;
      return SCIP_OKAY;
   }

   /* check all linear constraints for feasibility */
   violated = FALSE;
   for( c = 0; c < nconss && !violated; ++c )
   {
      CHECK_OKAY( check(scip, conss[c], NULL, TRUE, NULL, &violated) );
   }

   if( violated )
      *result = SCIP_INFEASIBLE;
   else
      *result = SCIP_FEASIBLE;

   return SCIP_OKAY;
}

static
DECL_CONSCHECK(consCheckLinear)
{
   Bool violated;
   int c;

   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(scip != NULL);
   assert(result != NULL);

   /*debugMessage("Check method of linear constraints\n");*/

   /* check all linear constraints for feasibility */
   violated = FALSE;
   for( c = 0; c < nconss && !violated; ++c )
   {
      CHECK_OKAY( check(scip, conss[c], sol, checklprows, NULL, &violated) );
   }

   if( violated )
      *result = SCIP_INFEASIBLE;
   else
      *result = SCIP_FEASIBLE;

   return SCIP_OKAY;
}

static
DECL_CONSPROP(consPropLinear)
{
   CONSHDLRDATA* conshdlrdata;
   CONS* cons;
   LINCONS* lincons;
   Real minactivity;
   Real maxactivity;
   Bool redundant;
   Bool tightenbounds;
   int propfreq;
   int actdepth;
   int nchgbds;
   int c;

   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(scip != NULL);
   assert(result != NULL);

   /*debugMessage("Prop method of linear constraints\n");*/

   /* check, if we want to tighten variable's bounds */
   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);
   propfreq = SCIPconshdlrGetPropFreq(conshdlr);
   actdepth = SCIPgetActDepth(scip);
   tightenbounds = (conshdlrdata->tightenboundsfreq == 0 && actdepth == 0);
   tightenbounds |= (conshdlrdata->tightenboundsfreq >= 1
      && (actdepth % (propfreq * conshdlrdata->tightenboundsfreq) == 0));
   nchgbds = 0;

   /* process useful constraints */
   *result = SCIP_DIDNOTFIND;
   for( c = 0; c < nusefulconss && *result != SCIP_CUTOFF; ++c )
   {
      cons = conss[c];
      lincons = consGetLincons(cons);

      if( lincons->propagated )
         continue;

      /* we can only infer activity bounds of the linear constraint, if it is not modifiable */
      if( !lincons->modifiable )
      {
         /* tighten the variable's bounds */
         if( tightenbounds )
         {
            CHECK_OKAY( linconsTightenBounds(scip, lincons, &nchgbds, result) );
#ifndef NDEBUG
            {
               Real newminactivity;
               Real newmaxactivity;
               Real recalcminactivity;
               Real recalcmaxactivity;
               
               linconsGetActivityBounds(scip, lincons, &newminactivity, &newmaxactivity);
               linconsInvalidateActivities(lincons);
               linconsGetActivityBounds(scip, lincons, &recalcminactivity, &recalcmaxactivity);

               assert(SCIPisSumRelEQ(scip, newminactivity, recalcminactivity));
               assert(SCIPisSumRelEQ(scip, newmaxactivity, recalcmaxactivity));
            }
#endif
         }
         
         /* check constraint for infeasibility and redundancy */
         linconsGetActivityBounds(scip, lincons, &minactivity, &maxactivity);
         
         if( SCIPisGT(scip, minactivity, lincons->rhs) || SCIPisLT(scip, maxactivity, lincons->lhs) )
         {
            debugMessage("linear constraint <%s> is infeasible: activitybounds=[%g,%g], sides=[%g,%g]\n",
               SCIPconsGetName(cons), minactivity, maxactivity, lincons->lhs, lincons->rhs);
            CHECK_OKAY( SCIPresetConsAge(scip, cons) );
            *result = SCIP_CUTOFF;
         }
         else if( SCIPisGE(scip, minactivity, lincons->lhs) && SCIPisLE(scip, maxactivity, lincons->rhs) )
         {
            debugMessage("linear constraint <%s> is redundant: activitybounds=[%g,%g], sides=[%g,%g]\n",
               SCIPconsGetName(cons), minactivity, maxactivity, lincons->lhs, lincons->rhs);
            CHECK_OKAY( SCIPincConsAge(scip, cons) );
            CHECK_OKAY( SCIPdisableConsLocal(scip, cons) );
         }
      }

      lincons->propagated = TRUE;
   }
   debugMessage("linear constraint propagator tightened %d bounds\n", nchgbds);

   return SCIP_OKAY;
}




/*
 * Presolving
 */

/** replaces multiple occurrences of a variable by a single coefficient */
static
RETCODE linconsMergeMultiples(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons             /**< linear constraint object */
   )
{
   VAR* var;
   Real valsum;
   int v;

   /* sort the constraint */
   CHECK_OKAY( linconsSort(scip, lincons) );
   
   /* go backwards through the constraint looking for multiple occurrences of the same variable;
    * backward direction is necessary, since linconsDelCoefPos() modifies the given position and
    * the subsequent ones
    */
   for( v = lincons->nvars-1; v >= 1; --v )
   {
      var = lincons->vars[v];
      if( lincons->vars[v-1] == var )
      {
         valsum = lincons->vals[v];
         do
         {
            CHECK_OKAY( linconsDelCoefPos(scip, lincons, v) );
            --v;
            valsum += lincons->vals[v];
         }
         while( v >= 1 && lincons->vars[v-1] == var );

         /* modify the last existing occurrence of the variable */
         assert(lincons->vars[v] == var);
         if( SCIPisZero(scip, valsum) )
         {
            CHECK_OKAY( linconsDelCoefPos(scip, lincons, v) );
         }
         else
         {
            CHECK_OKAY( linconsChgCoefPos(scip, lincons, v, valsum) );
         }
      }
   }

   return SCIP_OKAY;
}

/** replaces all fixed and aggregated variables by their non-fixed counterparts */
static
RETCODE linconsApplyFixings(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint object */
   Bool*            conschanged         /**< pointer to store TRUE, if changes were made to the constraint */
   )
{
   VAR* var;
   Real val;
   Real fixedval;
   Real aggrconst;
   Bool cleanup;
   int v;

   assert(lincons != NULL);
   assert(conschanged != NULL);

   cleanup = FALSE;
   v = 0;
   while( v < lincons->nvars )
   {
      var = lincons->vars[v];
      val = lincons->vals[v];
      switch( SCIPvarGetStatus(var) )
      {
      case SCIP_VARSTATUS_ORIGINAL:
      case SCIP_VARSTATUS_LOOSE:
      case SCIP_VARSTATUS_COLUMN:
      case SCIP_VARSTATUS_MULTAGGR:
         ++v;
         break;

      case SCIP_VARSTATUS_FIXED:
         assert(SCIPisEQ(scip, SCIPvarGetLbGlobal(var), SCIPvarGetUbGlobal(var)));
         fixedval = SCIPvarGetLbGlobal(var);
         if( !SCIPisInfinity(scip, -lincons->lhs) )
            linconsChgLhs(scip, lincons, lincons->lhs - val * fixedval);
         if( !SCIPisInfinity(scip, lincons->rhs) )
            linconsChgRhs(scip, lincons, lincons->rhs - val * fixedval);
         CHECK_OKAY( linconsDelCoefPos(scip, lincons, v) );
         *conschanged = TRUE;
         break;

      case SCIP_VARSTATUS_AGGREGATED:
         CHECK_OKAY( linconsAddCoef(scip, lincons, SCIPvarGetAggrVar(var), val * SCIPvarGetAggrScalar(var)) );
         aggrconst = SCIPvarGetAggrConstant(var);
         if( !SCIPisInfinity(scip, -lincons->lhs) )
            linconsChgLhs(scip, lincons, lincons->lhs - val * aggrconst);
         if( !SCIPisInfinity(scip, lincons->rhs) )
            linconsChgRhs(scip, lincons, lincons->rhs - val * aggrconst);
         CHECK_OKAY( linconsDelCoefPos(scip, lincons, v) );
         *conschanged = TRUE;
         cleanup = TRUE;
         break;

      default:
         errorMessage("unknown variable status");
         abort();
      }
   }

   debugMessage("after fixings: ");
   debug(linconsPrint(scip, lincons, NULL));

   /* if aggregated variables have been replaced, multiple entries of the same variable are possible and we have
    * to clean up the constraint
    */
   CHECK_OKAY( linconsMergeMultiples(scip, lincons) );

   debugMessage("after merging: ");
   debug(linconsPrint(scip, lincons, NULL));

   return SCIP_OKAY;
}

/* tightens left and right hand side of constraint due to integrality */
static
RETCODE linconsPresolTightenSides(
   SCIP*            scip,               /**< SCIP data structure */
   CONS*            cons,               /**< linear constraint */
   int*             nchgsides,          /**< pointer to count number of side changes */
   Bool*            conschanged         /**< pointer to store TRUE, if changes were made to the constraint */
   )
{
   LINCONS* lincons;
   Bool integral;
   int i;

   assert(nchgsides != NULL);
   assert(conschanged != NULL);

   lincons = consGetLincons(cons);

   if( !SCIPisIntegral(scip, lincons->lhs) || !SCIPisIntegral(scip, lincons->rhs) )
   {
      integral = TRUE;
      for( i = 0; i < lincons->nvars && integral; ++i )
      {
         integral &= SCIPisIntegral(scip, lincons->vals[i]);
         integral &= (SCIPvarGetType(lincons->vars[i]) != SCIP_VARTYPE_CONTINOUS);
      }
      if( integral )
      {
         debugMessage("linear constraint <%s>: make sides integral: sides=[%g,%g]\n",
            SCIPconsGetName(cons), lincons->lhs, lincons->rhs);
         if( !SCIPisInfinity(scip, -lincons->lhs) && !SCIPisIntegral(scip, lincons->lhs) )
         {
            linconsChgLhs(scip, lincons, SCIPceil(scip, lincons->lhs));
            (*nchgsides)++;
            *conschanged = TRUE;
         }
         if( !SCIPisInfinity(scip, lincons->rhs) && !SCIPisIntegral(scip, lincons->rhs) )
         {
            linconsChgRhs(scip, lincons, SCIPfloor(scip, lincons->rhs));
            (*nchgsides)++;
            *conschanged = TRUE;
         }
      }
   }

   return SCIP_OKAY;
}

/* converts special equalities */
static
RETCODE linconsConvertEquality(
   SCIP*            scip,               /**< SCIP data structure */
   CONS*            cons,               /**< linear constraint */
   int*             nfixedvars,         /**< pointer to count number of fixed variables */
   int*             naggrvars,          /**< pointer to count number of aggregated variables */
   int*             ndelconss,          /**< pointer to count number of deleted constraints */
   RESULT*          result,             /**< pointer to store result for successful conversions */
   Bool*            conschanged,        /**< pointer to store TRUE, if changes were made to the constraint */
   Bool*            consdeleted         /**< pointer to store TRUE, if constraint was deleted */
   )
{
   CONSDATA* consdata;
   LINCONS* lincons;
   Bool infeasible;

   assert(nfixedvars != NULL);
   assert(naggrvars != NULL);
   assert(result != NULL);
   assert(conschanged != NULL);
   assert(consdeleted != NULL);

   lincons = consGetLincons(cons);

   if( SCIPisEQ(scip, lincons->lhs, lincons->rhs) )
   {
      if( lincons->nvars == 1 )
      {
         VAR* var;
         Real val;
         Real fixval;

         /* only one variable: adjust bounds and delete constraint */
         var = lincons->vars[0];
         val = lincons->vals[0];
         assert(!SCIPisZero(scip, val));
         fixval = lincons->rhs/val;

         /* check, if fixing would lead to an infeasibility */
         if( (SCIPvarGetType(var) != SCIP_VARTYPE_CONTINOUS && !SCIPisIntegral(scip, fixval)) )
         {
            debugMessage("linear equality <%s> is integer infeasible: %+g<%s> == %g\n",
               SCIPconsGetName(cons), val, SCIPvarGetName(var), lincons->rhs);
            *result = SCIP_CUTOFF;
            return SCIP_OKAY;
         }
         if( SCIPisLT(scip, fixval, SCIPvarGetLbGlobal(var)) || SCIPisGT(scip, fixval, SCIPvarGetUbGlobal(var)) )
         {
            debugMessage("linear equality <%s> is bound infeasible: %+g<%s> == %g, bounds=[%g,%g]\n",
               SCIPconsGetName(cons), val, SCIPvarGetName(var), lincons->rhs, 
               SCIPvarGetLbGlobal(var), SCIPvarGetUbGlobal(var));
            *result = SCIP_CUTOFF;
            return SCIP_OKAY;
         }

         /* fix variable, if not already fixed */
         if( SCIPvarGetStatus(var) != SCIP_VARSTATUS_FIXED )
         {
            debugMessage("linear equality <%s>: fix <%s> == %g\n",
               SCIPconsGetName(cons), SCIPvarGetName(var), fixval);
            CHECK_OKAY( SCIPfixVar(scip, var, fixval, &infeasible) );
            if( infeasible )
            {
               *result = SCIP_CUTOFF;
               return SCIP_OKAY;
            }
            (*nfixedvars)++;
         }

         /* disable constraint */
         CHECK_OKAY( SCIPdelCons(scip, cons) );
         (*ndelconss)++;
         *result = SCIP_SUCCESS;
         *consdeleted = TRUE;
         return SCIP_OKAY;
      }
      else if( lincons->nvars == 2 )
      {
         VAR** vars;
         Real* vals;
         Real scalar;
         Real constant;
         int agg;

         /* two variables: aggregation may be possible */
         agg = -1;
         vars = lincons->vars;
         vals = lincons->vals;
         assert(!SCIPisZero(scip, vals[0]));
         assert(!SCIPisZero(scip, vals[1]));

         /* vals[0] * vars[0] + vals[1] * vars[1] == rhs
          *  ->  vars[0] == -vals[1]/vals[0] * vars[1] + rhs/vals[0]  (agg=0)
          *  ->  vars[1] == -vals[0]/vals[1] * vars[0] + rhs/vals[1]  (agg=1)
          */
         if( SCIPvarGetType(vars[0]) == SCIP_VARTYPE_CONTINOUS )
            agg = 0;
         else if( SCIPvarGetType(vars[1]) == SCIP_VARTYPE_CONTINOUS )
            agg = 1;
         else if( SCIPvarGetType(vars[0]) == SCIP_VARTYPE_IMPLINT )
            agg = 0;
         else if( SCIPvarGetType(vars[1]) == SCIP_VARTYPE_IMPLINT )
            agg = 1;
         else if( SCIPisIntegral(scip, vals[1]/vals[0]) )
            agg = 0;
         else if( SCIPisIntegral(scip, vals[0]/vals[1]) )
            agg = 1;
         if( agg >= 0 )
         {
            assert(agg == 0 || agg == 1);
            scalar = -vals[1-agg]/vals[agg];
            constant = lincons->rhs/vals[agg];
            if( SCIPvarGetType(vars[0]) != SCIP_VARTYPE_CONTINOUS
               && SCIPvarGetType(vars[1]) != SCIP_VARTYPE_CONTINOUS
               && SCIPisIntegral(scip, scalar) && !SCIPisIntegral(scip, constant) )
            {
               debugMessage("linear constraint <%s>: infeasible integer aggregation <%s> == %g<%s>%+g\n",
                  SCIPconsGetName(cons), SCIPvarGetName(vars[agg]), scalar, SCIPvarGetName(vars[1-agg]), constant);
               *result = SCIP_CUTOFF;
               return SCIP_OKAY;
            }
            else
            {
               debugMessage("linear constraint <%s>: aggregate <%s> == %g<%s>%+g\n",
                  SCIPconsGetName(cons), SCIPvarGetName(vars[agg]), scalar, SCIPvarGetName(vars[1-agg]), constant);
               CHECK_OKAY( SCIPaggregateVar(scip, vars[agg], vars[1-agg], scalar, constant, &infeasible) );
               if( infeasible )
               {
                  debugMessage("linear constraint <%s>: aggregation infeasible <%s> == %g<%s>%+g\n",
                     SCIPconsGetName(cons), SCIPvarGetName(vars[agg]), scalar, SCIPvarGetName(vars[1-agg]), constant);
                  *result = SCIP_CUTOFF;
                  return SCIP_OKAY;
               }

               CHECK_OKAY( SCIPdelCons(scip, cons) );
               (*naggrvars)++;
               (*ndelconss)++;
               *result = SCIP_SUCCESS;
               *consdeleted = TRUE;
               return SCIP_OKAY;
            }
         }
         else if( SCIPisIntegral(scip, vals[0]) && SCIPisIntegral(scip, vals[1]) )
         {
            VAR* aggvar;
            Longint a;
            Longint b;
            Longint c;
            Longint gcd;
            Longint actclass;
            Longint classstep;
            Longint xsol;
            Longint ysol;

            /* Both variables are integers, and their coefficients are not multiples of each other:
             *   a*x + b*y == c    ->   a*x == c - b*y
             * Assume, that a and b don't have any common divisor. Let (x',y') be a solution of the equality.
             * Then x = -b*z + x', y = a*z + y' with z integral gives all solutions to the equality.
             */
            a = (Longint)(SCIPfloor(scip, vals[0]));
            b = (Longint)(SCIPfloor(scip, vals[1]));
            c = (Longint)(SCIPfloor(scip, lincons->rhs));
            assert(a != 0 && b != 0);
            gcd = SCIPcalcGreComDiv(ABS(a), ABS(b));
            a /= gcd;
            b /= gcd;
            c /= gcd;
            if( !SCIPisIntegral(scip, lincons->rhs/gcd) )
            {
               debugMessage("linear equality <%s> is integer infeasible: %+g<%s> %+g<%s> == %g\n",
                  SCIPconsGetName(cons), vals[0], SCIPvarGetName(vars[0]), vals[1], SCIPvarGetName(vars[1]),
                  lincons->rhs);
               *result = SCIP_CUTOFF;
               return SCIP_OKAY;
            }

            /* find initial solution (x',y'):
             *  - find y' such that c - b*y' is a multiple of a
             *    - start in equivalence class c%a
             *    - step through classes, where each step increases class number by (-b)%a
             *    - because a and b don't have a common divisor, each class is visited at most once
             *    - if equivalence class 0 is visited, we are done: y' equals the number of steps taken
             *  - calculate x' with x' = (c - b*y')/a (which must be integral)
             */

            /* search upwards from ysol = 0 */
            ysol = 0;
            actclass = c%a;
            if( actclass < 0 )
               actclass += a;
            classstep = (-b)%a;
            if( classstep < 0 )
               classstep += a;
            assert(0 < classstep && classstep < a);
            while( actclass != 0 )
            {
               assert(0 <= actclass && actclass < a);
               actclass += classstep;
               if( actclass >= a )
                  actclass -= a;
               ysol++;
            }
            assert(((c - b*ysol)%a) == 0);
            xsol = (c - b*ysol)/a;

            /* feasible solutions are (x,y) = (x',y') + z*(-b,a)
             * - create new integer variable z with infinite bounds
             * - aggregate variable x = -b*z + x'
             * - aggregate variable y =  a*z + y'
             * - the bounds of z are calculated automatically during aggregation
             */
            CHECK_OKAY( SCIPcreateVar(scip, &aggvar, NULL, -SCIPinfinity(scip), SCIPinfinity(scip), 0.0,
                           SCIP_VARTYPE_INTEGER, TRUE) );
            CHECK_OKAY( SCIPaddVar(scip, aggvar) );
            CHECK_OKAY( SCIPaggregateVar(scip, vars[0], aggvar, (Real)(-b), (Real)xsol, &infeasible) );
            if( !infeasible )
            {
               CHECK_OKAY( SCIPaggregateVar(scip, vars[1], aggvar, (Real)a, (Real)ysol, &infeasible) );
            }

            debugMessage("linear constraint <%s>: aggregate <%s> == %g<%s>%+g, <%s> == %g<%s>%+g, <%s>: [%g,%g], obj=%g\n",
               SCIPconsGetName(cons), SCIPvarGetName(vars[0]), (Real)(-b), SCIPvarGetName(aggvar), (Real)xsol,
               SCIPvarGetName(vars[1]), (Real)a, SCIPvarGetName(aggvar), (Real)ysol,
               SCIPvarGetName(aggvar), SCIPvarGetLbGlobal(aggvar), SCIPvarGetUbGlobal(aggvar), SCIPvarGetObj(aggvar));

            /* release z */
            CHECK_OKAY( SCIPreleaseVar(scip, &aggvar) );

            /* check for infeasible aggregation */
            if( infeasible )
            {
               debugMessage("linear constraint <%s>: aggregation infeasible\n", SCIPconsGetName(cons));
               *result = SCIP_CUTOFF;
               return SCIP_OKAY;
            }

            /* disable constraint */
            CHECK_OKAY( SCIPdelCons(scip, cons) );
            (*naggrvars)++;  /* count the two aggregations only as one, because an additional variable was created */
            (*ndelconss)++;
            *result = SCIP_SUCCESS;
            *consdeleted = TRUE;
            return SCIP_OKAY;
         }
      }
      else
      {
         VAR** vars;
         Real* vals;
         VAR* var;
         Real val;
         VARTYPE bestslacktype;
         VARTYPE actslacktype;
         Real bestslackdomrng;
         Real actslackdomrng;
         Bool integral;
         int bestslackpos;
         int v;

         /* more than two variables: look for a slack variable s to convert a*x + s == b into lhs <= a*x <= rhs */
         vars = lincons->vars;
         vals = lincons->vals;
         bestslackpos = -1;
         bestslacktype = SCIP_VARTYPE_BINARY;
         bestslackdomrng = 0.0;
         integral = TRUE;
         for( v = 0; v < lincons->nvars; ++v )
         {
            assert(vars != NULL);
            assert(vals != NULL);
            var = vars[v];
            val = vals[v];
            
            actslacktype = SCIPvarGetType(var);
            integral &= (actslacktype != SCIP_VARTYPE_CONTINOUS);
            integral &= SCIPisIntegral(scip, val);

            assert(SCIPvarGetNLocksDown(var) >= 1); /* because variable is locked in this equality */
            assert(SCIPvarGetNLocksUp(var) >= 1);
            if( SCIPvarGetNLocksDown(var) == 1 && SCIPvarGetNLocksUp(var) == 1 )
            {
               /* variable is only locked in this equality: if variable is continous or if the value is 1.0,
                * it is a candidate for being a slack variable
                */
               if( actslacktype == SCIP_VARTYPE_CONTINOUS
                  || actslacktype == SCIP_VARTYPE_IMPLINT
                  || (integral && SCIPisEQ(scip, ABS(val), 1.0))
                   )
               {
                  actslackdomrng = SCIPvarGetUbGlobal(var) - SCIPvarGetLbGlobal(var);
                  if( bestslackpos == -1
                     || actslacktype > bestslacktype
                     || (actslacktype == bestslacktype && actslackdomrng > bestslackdomrng) )
                  {
                     bestslackpos = v;
                     bestslacktype = actslacktype;
                     bestslackdomrng = actslackdomrng;
                  }
               }
            }
         }

         if( integral && !SCIPisIntegral(scip, lincons->rhs) )
         {
            debugMessage("linear equality <%s> is integer infeasible:", SCIPconsGetName(cons));
            debug(linconsPrint(scip, lincons, NULL));
            *result = SCIP_CUTOFF;
            return SCIP_OKAY;
         }

         /* if the slack variable is of integer type, and the constraint itself may not take integral values,
          * we cannot aggregate the variable, because the integrality condition would get lost
          */
         if( bestslackpos >= 0 &&
            (bestslacktype == SCIP_VARTYPE_CONTINOUS || bestslacktype == SCIP_VARTYPE_IMPLINT || integral) )
         {
            VAR* slackvar;
            Real* scalars;
            Real slackcoef;
            Real slackvarlb;
            Real slackvarub;
            Real aggrconst;
            Real newlhs;
            Real newrhs;

            /* we found a slack variable that only occurs in this equality:
             *   a_1*x_1 + ... + a_k*x_k + a'*s == rhs  ->  s == rhs - a_1/a'*x_1 - ... - a_k/a'*x_k
             */

            /* convert equality into inequality by deleting the slack variable:
             *  x + a*s == b, l <= s <= u   ->  b - a*u <= x <= b - a*l
             */
            slackvar = vars[bestslackpos];
            slackcoef = vals[bestslackpos];
            assert(!SCIPisZero(scip, slackcoef));
            aggrconst = lincons->rhs/slackcoef;
            slackvarlb = SCIPvarGetLbGlobal(slackvar);
            slackvarub = SCIPvarGetUbGlobal(slackvar);
            if( slackcoef > 0.0 )
            {
               if( SCIPisInfinity(scip, -slackvarlb) )
                  newrhs = SCIPinfinity(scip);
               else
                  newrhs = lincons->rhs - slackcoef * slackvarlb;
               if( SCIPisInfinity(scip, slackvarub) )
                  newlhs = -SCIPinfinity(scip);
               else
                  newlhs = lincons->lhs - slackcoef * slackvarub;
            }
            else
            {
               if( SCIPisInfinity(scip, -slackvarlb) )
                  newlhs = -SCIPinfinity(scip);
               else
                  newlhs = lincons->rhs - slackcoef * slackvarlb;
               if( SCIPisInfinity(scip, slackvarub) )
                  newrhs = SCIPinfinity(scip);
               else
                  newrhs = lincons->lhs - slackcoef * slackvarub;
            }
            assert(SCIPisLE(scip, newlhs, newrhs));
            linconsChgLhs(scip, lincons, newlhs);
            linconsChgRhs(scip, lincons, newrhs);
            CHECK_OKAY( linconsDelCoefPos(scip, lincons, bestslackpos) );

            /* allocate temporary memory */
            CHECK_OKAY( SCIPcaptureBufferArray(scip, &scalars, lincons->nvars) );

            /* set up the multi-aggregation */
            debugMessage("linear constraint <%s>: multi-aggregate <%s> ==",
               SCIPconsGetName(cons), SCIPvarGetName(slackvar));
            for( v = 0; v < lincons->nvars; ++v )
            {
               scalars[v] = -lincons->vals[v]/slackcoef;
               debug(printf(" %+g<%s>", scalars[v], SCIPvarGetName(lincons->vars[v])));
            }
            debug(printf(" %+g, bounds of <%s>: [%g,%g]\n", 
                         aggrconst, SCIPvarGetName(slackvar), slackvarlb, slackvarub));

            /* perform the multi-aggregation */
            CHECK_OKAY( SCIPmultiaggregateVar(scip, slackvar, lincons->nvars, lincons->vars, scalars, aggrconst,
                           &infeasible) );

            /* free temporary memory */
            CHECK_OKAY( SCIPreleaseBufferArray(scip, &scalars) );

            /* check for infeasible aggregation */
            if( infeasible )
            {
               debugMessage("linear constraint <%s>: infeasible multi-aggregation\n", SCIPconsGetName(cons));
               *result = SCIP_CUTOFF;
               return SCIP_OKAY;
            }

            (*naggrvars)++;
            *result = SCIP_SUCCESS;
            *conschanged = TRUE;
            return SCIP_OKAY;
         }
      }
   }

   return SCIP_OKAY;
}

/** converts all variables with fixed domain into FIXED variables */
static
RETCODE linconsFixVariables(
   SCIP*            scip,               /**< SCIP data structure */
   LINCONS*         lincons,            /**< linear constraint object */
   int*             nfixedvars,         /**< pointer to count the total number of fixed variables */
   RESULT*          result,             /**< pointer to store the result of the variable fixing */
   Bool*            conschanged         /**< pointer to store TRUE, if changes were made to the constraint */
   )
{
   VAR* var;
   VARSTATUS varstatus;
   Real lb;
   Real ub;
   Bool fixed;
   Bool infeasible;
   int v;

   assert(scip != NULL);
   assert(lincons != NULL);
   assert(nfixedvars != NULL);
   assert(result != NULL);
   assert(*result != SCIP_CUTOFF);

   fixed = FALSE;
   for( v = 0; v < lincons->nvars; ++v )
   {
      assert(lincons->vars != NULL);
      var = lincons->vars[v];
      varstatus = SCIPvarGetStatus(var);

      if( varstatus != SCIP_VARSTATUS_FIXED && varstatus != SCIP_VARSTATUS_AGGREGATED )
      {
         lb = SCIPvarGetLbGlobal(var);
         ub = SCIPvarGetUbGlobal(var);
         if( SCIPisEQ(scip, lb, ub) )
         {
            debugMessage("converting variable <%s> with fixed bounds [%g,%g] into fixed variable\n",
               SCIPvarGetName(var), lb, ub);
            CHECK_OKAY( SCIPfixVar(scip, var, lb, &infeasible) );
            if( infeasible )
            {
               *result = SCIP_CUTOFF;
               return SCIP_OKAY;
            }
            (*nfixedvars)++;
            *result = SCIP_SUCCESS;
            fixed = TRUE;
         }
      }
   }

   if( fixed )
   {
      CHECK_OKAY( linconsApplyFixings(scip, lincons, conschanged) );
      assert(*conschanged);
   }
   
   return SCIP_OKAY;
}

/* tries to aggregate two equalities in order to decrease the number of variables in the first equality:
 *   cons0 := a * cons0 + b * cons1, 
 * where a = val1[v] and b = -val0[v] for common variable v which removes most variables;
 * for numerical stability, we will only accept integral a and b
 */
static 
RETCODE linconsAggregateEqualities(
   SCIP*            scip,               /**< SCIP data structure */
   CONS*            cons0,              /**< constraint to modify */
   CONS*            cons1,              /**< constraint to use for aggregation of cons0 */
   int              nvarscommon,        /**< number of variables, that appear in both constraints */
   int*             commonidx0,         /**< array with indices of variables in cons0, that appear also in cons1 */
   int*             commonidx1,         /**< array with indices of variables in cons1, that appear also in cons0 */
   int*             diffidx0minus1,     /**< array with indices of variables in cons0, that don't appear in cons1 */
   int*             diffidx1minus0,     /**< array with indices of variables in cons1, that don't appear in cons0 */
   int*             nchgcoefs,          /**< pointer to count number of changed coefficients */
   RESULT*          result,             /**< pointer to store the result of the aggregation */
   Bool*            aggregated          /**< pointer to store whether an aggregation was made */
   )
{
   LINCONS* lincons0;
   LINCONS* lincons1;
   VAR* var;
   Real a;
   Real b;
   Real aggrcoef;
   Real actscalarsum;
   Real bestscalarsum;
   Bool betterscalarsum;
   int actnvars;
   int bestnvars;
   int bestv;
   int v;
   int i;

   assert(nvarscommon >= 1);
   assert(commonidx0 != NULL);
   assert(commonidx1 != NULL);
   assert(diffidx0minus1 != NULL);
   assert(diffidx1minus0 != NULL);
   assert(nchgcoefs != NULL);
   assert(result != NULL);
   assert(aggregated != NULL);

   assert(SCIPconsIsActive(cons0));
   assert(SCIPconsIsActive(cons1));

   lincons0 = consGetLincons(cons0);
   assert(lincons0->nvars >= 1);
   assert(SCIPisEQ(scip, lincons0->lhs, lincons0->rhs));
   
   lincons1 = consGetLincons(cons1);
   assert(lincons1->nvars >= 1);
   assert(SCIPisEQ(scip, lincons1->lhs, lincons1->rhs));

   *aggregated = FALSE;

   /* search for the best common variable such that
    *  val1[var] * lincons0 - val0[var] * lincons1
    * has least number of variables */
   bestnvars = lincons0->nvars;
   bestv = -1;
   bestscalarsum = 0.0;
   for( v = 0; v < nvarscommon; ++v )
   {
      assert(lincons0->vars[commonidx0[v]] == lincons1->vars[commonidx1[v]]);
      var = lincons0->vars[commonidx0[v]];
      a = lincons1->vals[commonidx1[v]];
      b = -lincons0->vals[commonidx0[v]];

      /* only try aggregation, if coefficients are integral (numerical stability) */
      if( SCIPisIntegral(scip, a) && SCIPisIntegral(scip, b) )
      {
         /* count the number of variables in the potential new constraint  a * lincons0 + b * lincons1 */
         actnvars = lincons0->nvars + lincons1->nvars - 2*nvarscommon;
         actscalarsum = ABS(a) + ABS(b);
         betterscalarsum = (actscalarsum < bestscalarsum);
         for( i = 0; i < nvarscommon && (actnvars < bestnvars || (actnvars == bestnvars && betterscalarsum)); ++i )
         {
            aggrcoef = a * lincons0->vals[commonidx0[i]] + b * lincons1->vals[commonidx1[i]];
            if( !SCIPisZero(scip, aggrcoef) )
               actnvars++;
         }
         if( actnvars < bestnvars || (actnvars == bestnvars && betterscalarsum) )
         {
            bestv = v;
            bestnvars = actnvars;
            bestscalarsum = actscalarsum;
         }
      }
   }

   if( bestv != -1 )
   {
      CONS* newcons;
      VAR** newvars;
      Real* newvals;
      Real newrhs;
      int newnvars;

      /* better aggregation was found: create new constraint and delete old one */
      a = lincons1->vals[commonidx1[bestv]];
      b = -lincons0->vals[commonidx0[bestv]];
      assert(!SCIPisZero(scip, a));
      assert(!SCIPisZero(scip, b));
      debugMessage("aggregate equalities <%s> := %g*<%s> + %g*<%s>  ->  oldnvars=%d, newnvars=%d\n",
         SCIPconsGetName(cons0), a, SCIPconsGetName(cons0), b, SCIPconsGetName(cons1),
         lincons0->nvars, bestnvars);
      debugMessage("<%s>: ", SCIPconsGetName(cons0));
      debug(linconsPrint(scip, lincons0, NULL));
      debugMessage("<%s>: ", SCIPconsGetName(cons1));
      debug(linconsPrint(scip, lincons1, NULL));

      /* get temporary memory for creating the new linear constraint */
      CHECK_OKAY( SCIPcaptureBufferArray(scip, &newvars, bestnvars) );
      CHECK_OKAY( SCIPcaptureBufferArray(scip, &newvals, bestnvars) );

      /* calculate the common coefficients */
      newnvars = 0;
      for( i = 0; i < nvarscommon; ++i )
      {
         assert(0 <= commonidx0[i] && commonidx0[i] < lincons0->nvars);
         assert(0 <= commonidx1[i] && commonidx1[i] < lincons1->nvars);

         aggrcoef = a * lincons0->vals[commonidx0[i]] + b * lincons1->vals[commonidx1[i]];
         if( !SCIPisZero(scip, aggrcoef) )
         {
            assert(newnvars < bestnvars);
            newvars[newnvars] = lincons0->vars[commonidx0[i]];
            newvals[newnvars] = aggrcoef;
            newnvars++;
         }
      }

      /* calculate the coefficients appearing in cons0 but not in cons1 */
      for( i = 0; i < lincons0->nvars - nvarscommon; ++i )
      {
         assert(0 <= diffidx0minus1[i] && diffidx0minus1[i] < lincons0->nvars);

         aggrcoef = a * lincons0->vals[diffidx0minus1[i]];
         assert(!SCIPisZero(scip, aggrcoef));
         assert(newnvars < bestnvars);
         newvars[newnvars] = lincons0->vars[diffidx0minus1[i]];
         newvals[newnvars] = aggrcoef;
         newnvars++;
      }

      /* calculate the coefficients appearing in cons1 but not in cons0 */
      for( i = 0; i < lincons1->nvars - nvarscommon; ++i )
      {
         assert(0 <= diffidx1minus0[i] && diffidx1minus0[i] < lincons1->nvars);

         aggrcoef = b * lincons1->vals[diffidx1minus0[i]];
         assert(!SCIPisZero(scip, aggrcoef));
         assert(newnvars < bestnvars);
         newvars[newnvars] = lincons1->vars[diffidx1minus0[i]];
         newvals[newnvars] = aggrcoef;
         newnvars++;
      }
      assert(newnvars == bestnvars);

      todoMessage("don't aggregate equalities, if max{|coef|} is increased too much");

      /* calculate the new right hand side of the equality */
      newrhs = a * lincons0->rhs + b * lincons1->rhs;

      /* create the new linear constraint */
      CHECK_OKAY( SCIPcreateConsLinear(scip, &newcons, SCIPconsGetName(cons0), newnvars, newvars, newvals, newrhs, newrhs,
                     SCIPconsIsSeparated(cons0), SCIPconsIsEnforced(cons0), 
                     SCIPconsIsChecked(cons0), SCIPconsIsPropagated(cons0),
                     lincons0->local, lincons0->modifiable, lincons0->removeable) );

      /* update the statistics: we changed all coefficients of the old cons0 */
      (*nchgcoefs) += lincons0->nvars;
      *result = SCIP_SUCCESS;
      *aggregated = TRUE;

      /* delete the old constraint */
      CHECK_OKAY( SCIPdelCons(scip, cons0) );
      
      /* add the new constraint */
      CHECK_OKAY( SCIPaddCons(scip, newcons) );

      /* release the new constraint */
      CHECK_OKAY( SCIPreleaseCons(scip, &newcons) );

      /* free temporary memory */
      CHECK_OKAY( SCIPreleaseBufferArray(scip, &newvals) );
      CHECK_OKAY( SCIPreleaseBufferArray(scip, &newvars) );
   }

   return SCIP_OKAY;
}

/** checks redundancy of constraint with given index against all prior constraints in the constraint set,
 *  and removes or changes constraint accordingly
 */
static
RETCODE linconsRemoveRedundancy(
   SCIP*            scip,               /**< SCIP data structure */
   CONS**           conss,              /**< constraint set */
   int              firstredcheck,      /**< first constraint that changed since last redundancy check */
   int              chkind,             /**< index of constraint to check against all prior indices upto startind */
   int*             nfixedvars,         /**< pointer to count number of fixed variables */
   int*             naggrvars,          /**< pointer to count number of aggregated variables */
   int*             ndelconss,          /**< pointer to count number of deleted constraints */
   int*             nchgsides,          /**< pointer to count number of changed left/right hand sides */
   int*             nchgcoefs,          /**< pointer to count number of changed coefficients */
   RESULT*          result              /**< pointer to store result for successful conversions */
   )
{
   CONS* cons0;
   CONS* cons1;
   LINCONS* lincons0;
   LINCONS* lincons1;
   VAR* var;
   int* commonidx0;
   int* commonidx1;
   int* diffidx0minus1;
   int* diffidx1minus0;
   Real val0;
   Real val1;
   Bool cons0dominateslhs;
   Bool cons1dominateslhs;
   Bool cons0dominatesrhs;
   Bool cons1dominatesrhs;
   Bool cons0isequality;
   Bool cons1isequality;
   int diffidx1minus0size;
   int nvarscommon;
   int nvars0minus1;
   int nvars1minus0;
   int varcmp;
   int c;
   int v0;
   int v1;

   assert(conss != NULL);
   assert(firstredcheck <= chkind);
   assert(nfixedvars != NULL);
   assert(naggrvars != NULL);
   assert(ndelconss != NULL);
   assert(nchgsides != NULL);
   assert(result != NULL);

   /* get the constraint to be checked for redundancy */
   cons0 = conss[chkind];
   assert(SCIPconsIsActive(cons0));

   lincons0 = consGetLincons(cons0);
   assert(lincons0->nvars >= 1);
   cons0isequality = SCIPisEQ(scip, lincons0->lhs, lincons0->rhs);

   /* sort the constraint */
   CHECK_OKAY( linconsSort(scip, lincons0) );

   /* get temporary memory for indices of common variables */
   CHECK_OKAY( SCIPcaptureBufferArray(scip, &commonidx0, lincons0->nvars) );
   CHECK_OKAY( SCIPcaptureBufferArray(scip, &commonidx1, lincons0->nvars) );
   CHECK_OKAY( SCIPcaptureBufferArray(scip, &diffidx0minus1, lincons0->nvars) );
   CHECK_OKAY( SCIPcaptureBufferArray(scip, &diffidx1minus0, lincons0->nvars) );
   diffidx1minus0size = lincons0->nvars;

   /* check constraint against all prior constraints */
   for( c = (lincons0->redchecked ? firstredcheck : 0); c < chkind && *result != SCIP_CUTOFF && SCIPconsIsActive(cons0);
        ++c )
   {
      cons1 = conss[c];
      assert(cons1 != NULL);

      /* ignore inactive constraints */
      if( !SCIPconsIsActive(cons1) )
         continue;

      lincons1 = consGetLincons(cons1);

      /* if both constraints didn't change since last redundancy check, we can ignore the pair */
      if( lincons0->redchecked && lincons1->redchecked )
         continue;

      assert(lincons1->nvars >= 1);

      /* sort the constraint */
      CHECK_OKAY( linconsSort(scip, lincons1) );

      cons1isequality = SCIPisEQ(scip, lincons1->lhs, lincons1->rhs);
      
      /* make sure, we have enough memory for the index set of V_1 \ V_0 */
      if( lincons1->nvars > diffidx1minus0size )
      {
         CHECK_OKAY( SCIPreleaseBufferArray(scip, &diffidx1minus0) );
         CHECK_OKAY( SCIPcaptureBufferArray(scip, &diffidx1minus0, lincons1->nvars) );
         diffidx1minus0size = lincons1->nvars;
      }

      todoMessage("normalize constraints (at a different place, but it is important here)");
      /* because both constraints are normalized, a <=-row and a >=-row cannot be redundant */
      if( SCIPisInfinity(scip, -lincons0->lhs) != SCIPisInfinity(scip, -lincons1->lhs)
         && SCIPisInfinity(scip, lincons0->rhs) != SCIPisInfinity(scip, lincons1->rhs) )
         continue;

      /* check lincons0 against lincons1:
       * - if lhs0 >= lhs1 and for each variable v and each solution value x_v val0[v]*x_v <= val1[v]*x_v,
       *   lincons0 dominates lincons1 w.r.t. left hand side
       * - if rhs0 <= rhs1 and for each variable v and each solution value x_v val0[v]*x_v >= val1[v]*x_v,
       *   lincons0 dominates lincons1 w.r.t. right hand side
       * - if both constraints are equalities, count the number of common variables N_c and the number of variable in
       *   the difference sets N_0 = |V_0 \ V_1|, N_1 = |V_1 \ V_0|
       *   - if N_c > N_1, try to aggregate  lincons0 := a * lincons0 + b * lincons1  in order to decrease the number of
       *     variables in lincons0, where a = val1[v] and b = -val0[v] for common v which removes most variables;
       *     for numerical stability, we will only accept integral a and b
       *   - if N_c > N_0, try to aggregate  lincons1 := a * lincons1 + b * lincons0  in order to decrease the number of
       *     variables in lincons1, where a = val0[v] and b = -val1[v] for common v which removes most variables;
       *     for numerical stability, we will only accept integral a and b
       */

      /* check lincons0 against lincons1 for redundancy */
      cons0dominateslhs = SCIPisGE(scip, lincons0->lhs, lincons1->lhs);
      cons1dominateslhs = SCIPisGE(scip, lincons1->lhs, lincons0->lhs);
      cons0dominatesrhs = SCIPisLE(scip, lincons0->rhs, lincons1->rhs);
      cons1dominatesrhs = SCIPisLE(scip, lincons1->rhs, lincons0->rhs);
      nvarscommon = 0;
      nvars0minus1 = 0;
      nvars1minus0 = 0;
      v0 = 0;
      v1 = 0;
      while( (v0 < lincons0->nvars || v1 < lincons1->nvars)
         && (cons0dominateslhs || cons1dominateslhs || cons0dominatesrhs || cons1dominatesrhs
            || (cons0isequality && cons1isequality)) )
      {
         /* test, if variable appears in only one or in both constraints */
         if( v0 < lincons0->nvars && v1 < lincons1->nvars )
            varcmp = SCIPvarCmp(lincons0->vars[v0], lincons1->vars[v1]);
         else if( v0 < lincons0->nvars )
            varcmp = -1;
         else
            varcmp = +1;

         switch( varcmp )
         {
         case -1:
            /* variable doesn't appear in lincons1 */
            var = lincons0->vars[v0];
            val0 = lincons0->vals[v0];
            val1 = 0.0;
            diffidx0minus1[nvars0minus1] = v0;
            nvars0minus1++;
            v0++;
            break;

         case +1:
            /* variable doesn't appear in lincons0 */
            var = lincons1->vars[v1];
            val0 = 0.0;
            val1 = lincons1->vals[v1];
            diffidx1minus0[nvars1minus0] = v1;
            nvars1minus0++;
            v1++;
            break;

         case 0:
            /* variable appears in both constraints */
            assert(lincons0->vars[v0] == lincons1->vars[v1]);
            var = lincons0->vars[v0];
            val0 = lincons0->vals[v0];
            val1 = lincons1->vals[v1];
            commonidx0[nvarscommon] = v0;
            commonidx1[nvarscommon] = v1;
            nvarscommon++;
            v0++;
            v1++;
            break;

         default:
            errorMessage("invalid comparison result");
            abort();
         }
         assert(var != NULL);

         /* update domination criteria w.r.t. the coefficient and the variable's bounds */
         if( SCIPisGT(scip, val0, val1) )
         {
            if( SCIPisNegative(scip, SCIPvarGetLbGlobal(var)) )
            {
               cons0dominatesrhs = FALSE;
               cons1dominateslhs = FALSE;
            }
            if( SCIPisPositive(scip, SCIPvarGetUbGlobal(var)) )
            {
               cons0dominateslhs = FALSE;
               cons1dominatesrhs = FALSE;
            }
         }
         else if( SCIPisLT(scip, val0, val1) )
         {
            if( SCIPisNegative(scip, SCIPvarGetLbGlobal(var)) )
            {
               cons0dominateslhs = FALSE;
               cons1dominatesrhs = FALSE;
            }
            if( SCIPisPositive(scip, SCIPvarGetUbGlobal(var)) )
            {
               cons0dominatesrhs = FALSE;
               cons1dominateslhs = FALSE;
            }
         }
      }

      /* check for domination */
      if( cons1dominateslhs && !SCIPisInfinity(scip, -lincons0->lhs) )
      {
         /* left hand side is dominated by lincons1: delete left hand side of lincons0 */
         debugMessage("left hand side of linear constraint <%s> is dominated by <%s>:\n",
            SCIPconsGetName(cons0), SCIPconsGetName(cons1));
         debug(linconsPrint(scip, lincons0, NULL));
         debug(linconsPrint(scip, lincons1, NULL));
         /* check for infeasibility */
         if( SCIPisGT(scip, lincons1->lhs, lincons0->rhs) )
         {
            debugMessage("linear constraint <%s> is infeasible\n", SCIPconsGetName(cons0));
            *result = SCIP_CUTOFF;
            continue;
         }
         linconsChgLhs(scip, lincons0, -SCIPinfinity(scip));
         (*nchgsides)++;
         *result = SCIP_SUCCESS;
      }
      else if( cons0dominateslhs && !SCIPisInfinity(scip, -lincons1->lhs) )
      {
         /* left hand side is dominated by lincons0: delete left hand side of lincons1 */
         debugMessage("left hand side of linear constraint <%s> is dominated by <%s>:\n",
            SCIPconsGetName(cons1), SCIPconsGetName(cons0));
         debug(linconsPrint(scip, lincons1, NULL));
         debug(linconsPrint(scip, lincons0, NULL));
         /* check for infeasibility */
         if( SCIPisGT(scip, lincons0->lhs, lincons1->rhs) )
         {
            debugMessage("linear constraint <%s> is infeasible\n", SCIPconsGetName(cons1));
            *result = SCIP_CUTOFF;
            continue;
         }
         linconsChgLhs(scip, lincons1, -SCIPinfinity(scip));
         (*nchgsides)++;
         *result = SCIP_SUCCESS;
      }
      if( cons1dominatesrhs && !SCIPisInfinity(scip, lincons0->rhs) )
      {
         /* right hand side is dominated by lincons1: delete right hand side of lincons0 */
         debugMessage("right hand side of linear constraint <%s> is dominated by <%s>:\n",
            SCIPconsGetName(cons0), SCIPconsGetName(cons1));
         debug(linconsPrint(scip, lincons0, NULL));
         debug(linconsPrint(scip, lincons1, NULL));
         /* check for infeasibility */
         if( SCIPisLT(scip, lincons1->rhs, lincons0->lhs) )
         {
            debugMessage("linear constraint <%s> is infeasible\n", SCIPconsGetName(cons0));
            *result = SCIP_CUTOFF;
            continue;
         }
         linconsChgRhs(scip, lincons0, SCIPinfinity(scip));
         (*nchgsides)++;
         *result = SCIP_SUCCESS;
      }
      else if( cons0dominatesrhs && !SCIPisInfinity(scip, lincons1->rhs) )
      {
         /* right hand side is dominated by lincons0: delete right hand side of lincons1 */
         debugMessage("right hand side of linear constraint <%s> is dominated by <%s>:\n",
            SCIPconsGetName(cons1), SCIPconsGetName(cons0));
         debug(linconsPrint(scip, lincons1, NULL));
         debug(linconsPrint(scip, lincons0, NULL));
         /* check for infeasibility */
         if( SCIPisLT(scip, lincons0->rhs, lincons1->lhs) )
         {
            debugMessage("linear constraint <%s> is infeasible\n", SCIPconsGetName(cons1));
            *result = SCIP_CUTOFF;
            continue;
         }
         linconsChgRhs(scip, lincons1, SCIPinfinity(scip));
         (*nchgsides)++;
         *result = SCIP_SUCCESS;
      }

      /* check for now redundant constraints */
      if( SCIPisInfinity(scip, -lincons0->lhs) && SCIPisInfinity(scip, lincons0->rhs) )
      {
         /* lincons0 became redundant */
         debugMessage("linear constraint <%s> is redundant\n", SCIPconsGetName(cons0));
         CHECK_OKAY( SCIPdelCons(scip, cons0) );
         (*ndelconss)++;
         *result = SCIP_SUCCESS;
         continue;
      }
      if( SCIPisInfinity(scip, -lincons1->lhs) && SCIPisInfinity(scip, lincons1->rhs) )
      {
         /* lincons1 became redundant */
         debugMessage("linear constraint <%s> is redundant\n", SCIPconsGetName(cons1));
         CHECK_OKAY( SCIPdelCons(scip, cons1) );
         (*ndelconss)++;
         *result = SCIP_SUCCESS;
         continue;
      }

      /* check, if we want to aggregate equalities:
       *   lincons0 := a * lincons0 + b * lincons1  or  lincons1 := a * lincons1 + b * lincons0
       */
      if( cons0isequality && cons1isequality )
      {
         Bool aggregated;

         assert(lincons0->nvars == nvarscommon + nvars0minus1);
         assert(lincons1->nvars == nvarscommon + nvars1minus0);

         aggregated = FALSE;
         if( nvarscommon > nvars1minus0 )
         {
            /* N_c > N_1: try to aggregate  lincons0 := a * lincons0 + b * lincons1 */
            CHECK_OKAY( linconsAggregateEqualities(scip, cons0, cons1, nvarscommon, commonidx0, commonidx1,
                           diffidx0minus1, diffidx1minus0, nchgcoefs, result, &aggregated) );
         }
         if( !aggregated && nvarscommon > nvars0minus1 )
         {
            /* N_c > N_0: try to aggregate  lincons1 := a * lincons1 + b * lincons0 */
            CHECK_OKAY( linconsAggregateEqualities(scip, cons1, cons0, nvarscommon, commonidx1, commonidx0,
                           diffidx1minus0, diffidx0minus1, nchgcoefs, result, &aggregated) );
         }
      }
   }

   /* free temporary memory */
   CHECK_OKAY( SCIPreleaseBufferArray(scip, &diffidx1minus0) );
   CHECK_OKAY( SCIPreleaseBufferArray(scip, &diffidx0minus1) );
   CHECK_OKAY( SCIPreleaseBufferArray(scip, &commonidx1) );
   CHECK_OKAY( SCIPreleaseBufferArray(scip, &commonidx0) );

   return SCIP_OKAY;
}

static
DECL_CONSPRESOL(consPresolLinear)
{
   CONS* cons;
   CONS* upgdcons;
   LINCONS* lincons;
   Real minactivity;
   Real maxactivity;
   Bool redundant;
   Bool consdeleted;
   Bool conschanged;
   int oldnfixedvars;
   int oldnaggrvars;
   int firstredcheck;
   int c;

   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) == 0);
   assert(scip != NULL);
   assert(result != NULL);

   /*debugMessage("Presol method of linear constraints\n");*/

   *result = SCIP_DIDNOTFIND;

   /* process constraints */
   oldnfixedvars = *nfixedvars;
   oldnaggrvars = *naggrvars;
   firstredcheck = -1;
   for( c = 0; c < nconss && *result != SCIP_CUTOFF; ++c )
   {
      cons = conss[c];
      lincons = consGetLincons(cons);

      /* remember the first constraint that must be checked for redundancy */
      if( firstredcheck == -1 && !lincons->redchecked )
         firstredcheck = c;

      if( lincons->propagated )
         continue;

      debugMessage("presolving linear constraint <%s>: ", SCIPconsGetName(cons));
      debug(linconsPrint(scip, lincons, NULL));

      consdeleted = FALSE;
      conschanged = FALSE;

      /* incorporate fixings and aggregations in constraint */
      if( nnewfixedvars > 0 || nnewaggrvars > 0 || *nfixedvars > oldnfixedvars || *naggrvars > oldnaggrvars )
      {
         CHECK_OKAY( linconsApplyFixings(scip, lincons, &conschanged) );
      }

      /* we can only presolve linear constraints, that are not modifiable */
      if( !lincons->modifiable )
      {
         /* check, if constraint is empty */
         if( lincons->nvars == 0 )
         {
            if( SCIPisPositive(scip, lincons->lhs) || SCIPisNegative(scip, lincons->rhs) )
            {
               debugMessage("linear constraint <%s> is empty and infeasible: sides=[%g,%g]\n",
                  SCIPconsGetName(cons), lincons->lhs, lincons->rhs);
               *result = SCIP_CUTOFF;
               continue;
            }
            else
            {
               debugMessage("linear constraint <%s> is empty and redundant: sides=[%g,%g]\n",
                  SCIPconsGetName(cons), lincons->lhs, lincons->rhs);
               CHECK_OKAY( SCIPdelCons(scip, cons) );
               (*ndelconss)++;
               *result = SCIP_SUCCESS;
               continue;
            }
         }

         /* tighten left and right hand side due to integrality */
         CHECK_OKAY( linconsPresolTightenSides(scip, cons, nchgsides, &conschanged) );

         /* check bounds */
         if( SCIPisGT(scip, lincons->lhs, lincons->rhs) )
         {
            debugMessage("linear constraint <%s> is infeasible: sides=[%g,%g]\n",
               SCIPconsGetName(cons), lincons->lhs, lincons->rhs);
            *result = SCIP_CUTOFF;
            continue;
         }

         /* convert special equalities */
         CHECK_OKAY( linconsConvertEquality(scip, cons, nfixedvars, naggrvars, ndelconss, result,
                        &conschanged, &consdeleted) );
         if( *result == SCIP_CUTOFF || consdeleted )
            continue;

         /* tighten variable's bounds */
         CHECK_OKAY( linconsTightenBounds(scip, lincons, nchgbds, result) );
         if( *result == SCIP_CUTOFF )
            continue;

         /* check for fixed variables */
         CHECK_OKAY( linconsFixVariables(scip, lincons, nfixedvars, result, &conschanged) );
         if( *result == SCIP_CUTOFF )
            continue;

         /* check constraint for infeasibility and redundancy */
         linconsGetActivityBounds(scip, lincons, &minactivity, &maxactivity);
         if( SCIPisGT(scip, minactivity, lincons->rhs) || SCIPisLT(scip, maxactivity, lincons->lhs) )
         {
            debugMessage("linear constraint <%s> is infeasible: activitybounds=[%g,%g], sides=[%g,%g]\n",
               SCIPconsGetName(cons), minactivity, maxactivity, lincons->lhs, lincons->rhs);
            *result = SCIP_CUTOFF;
            continue;
         }
         else if( SCIPisGE(scip, minactivity, lincons->lhs) && SCIPisLE(scip, maxactivity, lincons->rhs) )
         {
            debugMessage("linear constraint <%s> is redundant: activitybounds=[%g,%g], sides=[%g,%g]\n",
               SCIPconsGetName(cons), minactivity, maxactivity, lincons->lhs, lincons->rhs);
            CHECK_OKAY( SCIPdelCons(scip, cons) );
            (*ndelconss)++;
            *result = SCIP_SUCCESS;
            continue;
         }
         else if( SCIPisGE(scip, minactivity, lincons->lhs) && !SCIPisInfinity(scip, -lincons->lhs) )
         {
            debugMessage("linear constraint <%s> left hand side is redundant: activitybounds=[%g,%g], sides=[%g,%g]\n",
               SCIPconsGetName(cons), minactivity, maxactivity, lincons->lhs, lincons->rhs);
            linconsChgLhs(scip, lincons, -SCIPinfinity(scip));
            (*nchgsides)++;
            *result = SCIP_SUCCESS;
            conschanged = TRUE;
         }
         else if( SCIPisLE(scip, maxactivity, lincons->rhs) && !SCIPisInfinity(scip, lincons->rhs) )
         {
            debugMessage("linear constraint <%s> right hand side is redundant: activitybounds=[%g,%g], sides=[%g,%g]\n",
               SCIPconsGetName(cons), minactivity, maxactivity, lincons->lhs, lincons->rhs);
            linconsChgRhs(scip, lincons, SCIPinfinity(scip));
            (*nchgsides)++;
            *result = SCIP_SUCCESS;
            conschanged = TRUE;
         }

         /* if constraint was changed, try to upgrade linear constraint into more specific constraint */
         if( conschanged )
         {
            CHECK_OKAY( SCIPupgradeConsLinear(scip, cons, &upgdcons) );
            if( upgdcons != NULL )
            {
               /* remove the old constraint from the problem, and add the upgraded one */
               CHECK_OKAY( SCIPdelCons(scip, cons) );
               CHECK_OKAY( SCIPaddCons(scip, upgdcons) );
               CHECK_OKAY( SCIPreleaseCons(scip, &upgdcons) );
               (*nupgdconss)++;
               continue;
            }
         }
      }

      lincons->propagated = TRUE;
   }

   /* redundancy checking */
   if( *result != SCIP_CUTOFF && firstredcheck != -1 )
   {
      for( c = firstredcheck; c < nconss; ++c )
      {
         if( SCIPconsIsActive(conss[c]) )
         {
            CHECK_OKAY( linconsRemoveRedundancy(scip, conss, firstredcheck, c,
                           nfixedvars, naggrvars, ndelconss, nchgsides, nchgcoefs, result) );
         }
      }
      for( c = firstredcheck; c < nconss; ++c )
      {
         lincons = consGetLincons(conss[c]);
         lincons->redchecked = TRUE;
      }
   }

   /* modify the result code */
   if( *result == SCIP_REDUCEDDOM )
      *result = SCIP_SUCCESS;

   return SCIP_OKAY;
}



/*
 * Callback methods of event handler
 */

static
DECL_EVENTEXEC(eventExecLinear)
{
   LINCONS* lincons;
   VAR* var;
   Real oldbound;
   Real newbound;
   int varpos;
   EVENTTYPE eventtype;

   assert(eventhdlr != NULL);
   assert(eventdata != NULL);
   assert(strcmp(SCIPeventhdlrGetName(eventhdlr), EVENTHDLR_NAME) == 0);
   assert(scip != NULL);
   assert(event != NULL);

   /*debugMessage("Exec method of bound change event handler for linear constraints\n");*/

   lincons = eventdata->lincons;
   varpos = eventdata->varpos;
   assert(lincons != NULL);
   assert(0 <= varpos && varpos < lincons->nvars);

   eventtype = SCIPeventGetType(event);
   var = SCIPeventGetVar(event);
   oldbound = SCIPeventGetOldbound(event);
   newbound = SCIPeventGetNewbound(event);
   assert(var != NULL);
   assert(lincons->vars[varpos] == var);

   /*debugMessage(" -> eventtype=0x%x, var=<%s>, oldbound=%g, newbound=%g => activity: [%g,%g]", 
     eventtype, SCIPvarGetName(linconsdata->vars[varpos]), oldbound, newbound, linconsdata->minactivity, 
     linconsdata->maxactivity);*/

   if( (eventtype & SCIP_EVENTTYPE_LBCHANGED) != 0 )
      linconsUpdateChgLb(scip, lincons, var, oldbound, newbound, lincons->vals[varpos]);
   else
   {
      assert((eventtype & SCIP_EVENTTYPE_UBCHANGED) != 0);
      linconsUpdateChgUb(scip, lincons, var, oldbound, newbound, lincons->vals[varpos]);
   }

   lincons->propagated = FALSE;

   /*debug(printf(" -> [%g,%g]\n", linconsdata->minactivity, linconsdata->maxactivity));*/

   return SCIP_OKAY;
}



/*
 * constraint specific interface methods
 */

/** creates the handler for linear constraints and includes it in SCIP */
RETCODE SCIPincludeConsHdlrLinear(
   SCIP*            scip                /**< SCIP data structure */
   )
{
   CONSHDLRDATA* conshdlrdata;

   /* create event handler for bound change events */
   CHECK_OKAY( SCIPincludeEventhdlr(scip, EVENTHDLR_NAME, EVENTHDLR_DESC,
                  NULL, NULL, NULL,
                  NULL, eventExecLinear,
                  NULL) );

   /* create constraint handler data */
   CHECK_OKAY( conshdlrdataCreate(scip, &conshdlrdata) );

   /* include constraint handler in SCIP */
   CHECK_OKAY( SCIPincludeConsHdlr(scip, CONSHDLR_NAME, CONSHDLR_DESC,
                  CONSHDLR_SEPAPRIORITY, CONSHDLR_ENFOPRIORITY, CONSHDLR_CHECKPRIORITY, CONSHDLR_SEPAFREQ,
                  CONSHDLR_PROPFREQ, CONSHDLR_NEEDSCONS,
                  consFreeLinear, NULL, NULL,
                  consDeleteLinear, consTransLinear, 
                  consSepaLinear, consEnfolpLinear, consEnfopsLinear, consCheckLinear, consPropLinear, consPresolLinear,
                  NULL, NULL,
                  conshdlrdata) );

   /* add linear constraint handler parameters */
   CHECK_OKAY( SCIPaddIntParam(scip,
                  "conshdlr/linear/tightenboundsfreq",
                  "multiplier on propagation frequency, how often the bounds are tightened (-1: never, 0: only at root)",
                  &conshdlrdata->tightenboundsfreq, TIGHTENBOUNDSFREQ, -1, INT_MAX, NULL, NULL) );

   return SCIP_OKAY;
}

/** includes a linear constraint update method into the linear constraint handler */
RETCODE SCIPincludeLinconsUpgrade(
   SCIP*            scip,               /**< SCIP data structure */
   DECL_LINCONSUPGD((*linconsupgd)),    /**< method to call for upgrading linear constraint */
   int              priority            /**< priority of upgrading method */
   )
{
   CONSHDLR* conshdlr;
   CONSHDLRDATA* conshdlrdata;
   LINCONSUPGRADE* linconsupgrade;

   assert(linconsupgd != NULL);

   /* find the linear constraint handler */
   conshdlr = SCIPfindConsHdlr(scip, CONSHDLR_NAME);
   if( conshdlr == NULL )
   {
      errorMessage("linear constraint handler not found");
      return SCIP_PLUGINNOTFOUND;
   }

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);

   /* create a linear constraint upgrade data object */
   CHECK_OKAY( linconsupgradeCreate(scip, &linconsupgrade, linconsupgd, priority) );

   /* insert linear constraint update method into constraint handler data */
   CHECK_OKAY( conshdlrdataIncludeUpgrade(scip, conshdlrdata, linconsupgrade) );

   return SCIP_OKAY;
}

/** creates and captures a linear constraint */
RETCODE SCIPcreateConsLinear(
   SCIP*            scip,               /**< SCIP data structure */
   CONS**           cons,               /**< pointer to hold the created constraint */
   const char*      name,               /**< name of constraint */
   int              nvars,              /**< number of nonzeros in the constraint */
   VAR**            vars,               /**< array with variables of constraint entries */
   Real*            vals,               /**< array with coefficients of constraint entries */
   Real             lhs,                /**< left hand side of constraint */
   Real             rhs,                /**< right hand side of constraint */
   Bool             separate,           /**< should the constraint be separated during LP processing? */
   Bool             enforce,            /**< should the constraint be enforced during node processing? */
   Bool             check,              /**< should the constraint be checked for feasibility? */
   Bool             propagate,          /**< should the constraint be propagated during node processing? */
   Bool             local,              /**< is linear constraint only valid locally? */
   Bool             modifiable,         /**< is constraint modifiable during node processing (subject to col generation)? */
   Bool             removeable          /**< should the constraint be removed from the LP due to aging or cleanup? */
   )
{
   CONSHDLR* conshdlr;
   CONSDATA* consdata;

   assert(scip != NULL);

   /* find the linear constraint handler */
   conshdlr = SCIPfindConsHdlr(scip, CONSHDLR_NAME);
   if( conshdlr == NULL )
   {
      errorMessage("linear constraint handler not found");
      return SCIP_PLUGINNOTFOUND;
   }

   /* create the constraint specific data */
   CHECK_OKAY( SCIPallocBlockMemory(scip, &consdata) );

   if( SCIPstage(scip) == SCIP_STAGE_PROBLEM )
   {
      if( local )
      {
         errorMessage("problem constraint cannot be local");
         return SCIP_INVALIDDATA;
      }

      /* create constraint in original problem */
      CHECK_OKAY( linconsCreate(scip, &consdata->lincons, nvars, vars, vals, lhs, rhs, modifiable, removeable) );
   }
   else
   {
      /* create constraint in transformed problem */
      CHECK_OKAY( linconsCreateTransformed(scip, &consdata->lincons, nvars, vars, vals, lhs, rhs, 
                     local, modifiable, removeable) );
   }
   consdata->row = NULL;
   
   /* create constraint */
   CHECK_OKAY( SCIPcreateCons(scip, cons, name, conshdlr, consdata, separate, enforce, check, propagate) );

   return SCIP_OKAY;
}

/** adds coefficient in linear constraint */
RETCODE SCIPaddCoefConsLinear(
   SCIP*            scip,               /**< SCIP data structure */
   CONS*            cons,               /**< constraint data */
   VAR*             var,                /**< variable of constraint entry */
   Real             val                 /**< coefficient of constraint entry */
   )
{
   CONSDATA* consdata;

   assert(cons != NULL);
   assert(scip != NULL);
   assert(var != NULL);

   /*debugMessage("adding coefficient %g * <%s> to linear constraint <%s>\n", val, var->name, SCIPconsGetName(cons));*/

   if( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(cons)), CONSHDLR_NAME) != 0 )
   {
      errorMessage("constraint is not linear");
      return SCIP_INVALIDDATA;
   }
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   CHECK_OKAY( linconsAddCoef(scip, consdata->lincons, var, val) );
   if( consdata->row != NULL )
   {
      CHECK_OKAY( SCIPaddVarToRow(scip, consdata->row, var, val) );
   }

   return SCIP_OKAY;
}

/** gets left hand side of linear constraint */
RETCODE SCIPgetLhsConsLinear(
   SCIP*            scip,               /**< SCIP data structure */
   CONS*            cons,               /**< constraint data */
   Real*            lhs                 /**< pointer to store left hand side */
   )
{
   CONSDATA* consdata;

   assert(cons != NULL);
   assert(scip != NULL);
   assert(lhs != NULL);

   if( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(cons)), CONSHDLR_NAME) != 0 )
   {
      errorMessage("constraint is not linear");
      return SCIP_INVALIDDATA;
   }
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   assert(consdata->lincons != NULL);

   *lhs = consdata->lincons->lhs;

   return SCIP_OKAY;
}

/** gets right hand side of linear constraint */
RETCODE SCIPgetRhsConsLinear(
   SCIP*            scip,               /**< SCIP data structure */
   CONS*            cons,               /**< constraint data */
   Real*            rhs                 /**< pointer to store right hand side */
   )
{
   CONSDATA* consdata;

   assert(cons != NULL);
   assert(scip != NULL);
   assert(rhs != NULL);

   if( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(cons)), CONSHDLR_NAME) != 0 )
   {
      errorMessage("constraint is not linear");
      return SCIP_INVALIDDATA;
   }
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);
   assert(consdata->lincons != NULL);

   *rhs = consdata->lincons->rhs;

   return SCIP_OKAY;
}

/** changes left hand side of linear constraint */
RETCODE SCIPchgLhsConsLinear(
   SCIP*            scip,               /**< SCIP data structure */
   CONS*            cons,               /**< constraint data */
   Real             lhs                 /**< new left hand side */
   )
{
   CONSDATA* consdata;

   assert(cons != NULL);
   assert(scip != NULL);

   if( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(cons)), CONSHDLR_NAME) != 0 )
   {
      errorMessage("constraint is not linear");
      return SCIP_INVALIDDATA;
   }
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   linconsChgLhs(scip, consdata->lincons, lhs);
   if( consdata->row != NULL )
   {
      CHECK_OKAY( SCIPchgRowLhs(scip, consdata->row, lhs) );
   }

   return SCIP_OKAY;
}

/** changes right hand side of linear constraint */
RETCODE SCIPchgRhsConsLinear(
   SCIP*            scip,               /**< SCIP data structure */
   CONS*            cons,               /**< constraint data */
   Real             rhs                 /**< new right hand side */
   )
{
   CONSDATA* consdata;

   assert(cons != NULL);
   assert(scip != NULL);

   if( strcmp(SCIPconshdlrGetName(SCIPconsGetHdlr(cons)), CONSHDLR_NAME) != 0 )
   {
      errorMessage("constraint is not linear");
      return SCIP_INVALIDDATA;
   }
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   linconsChgRhs(scip, consdata->lincons, rhs);
   if( consdata->row != NULL )
   {
      CHECK_OKAY( SCIPchgRowRhs(scip, consdata->row, rhs) );
   }

   return SCIP_OKAY;
}

/** tries to automatically convert a linear constraint into a more specific and more specialized constraint */
RETCODE SCIPupgradeConsLinear(
   SCIP*            scip,               /**< SCIP data structure */
   CONS*            cons,               /**< source constraint to try to convert */
   CONS**           upgdcons            /**< pointer to store upgraded constraint, or NULL if not successful */
   )
{
   CONSHDLR* conshdlr;
   CONSHDLRDATA* conshdlrdata;
   CONSDATA* consdata;
   LINCONS* lincons;
   VAR* var;
   Real val;
   Real lb;
   Real ub;
   Real poscoeffsum;
   Real negcoeffsum;
   Bool integral;
   Bool upgraded;
   int nposbin;
   int nnegbin;
   int nposint;
   int nnegint;
   int nposimpl;
   int nnegimpl;
   int nposcont;
   int nnegcont;
   int ncoeffspone;
   int ncoeffsnone;
   int ncoeffspint;
   int ncoeffsnint;
   int ncoeffspfrac;
   int ncoeffsnfrac;
   int i;

   assert(scip != NULL);
   assert(cons != NULL);
   assert(upgdcons != NULL);

   *upgdcons = NULL;

   conshdlr = SCIPconsGetHdlr(cons);
   if( strcmp(SCIPconshdlrGetName(conshdlr), CONSHDLR_NAME) != 0 )
   {
      errorMessage("constraint is not linear");
      return SCIP_INVALIDDATA;
   }

   conshdlrdata = SCIPconshdlrGetData(conshdlr);
   assert(conshdlrdata != NULL);
   
   consdata = SCIPconsGetData(cons);
   assert(consdata != NULL);

   if( consdata->row != NULL )
   {
      errorMessage("cannot upgrade linear constraint that is already stored as LP row");
      return SCIP_INVALIDDATA;
   }

   lincons = consdata->lincons;
   assert(lincons != NULL);

   /* we cannot upgrade a modifiable linear constraint, since we don't know what additional coefficients to expect */
   if( lincons->modifiable )
      return SCIP_OKAY;


   /* 
    * calculate some statistics on linear constraint
    */

   nposbin = 0;
   nnegbin = 0;
   nposint = 0;
   nnegint = 0;
   nposimpl = 0;
   nnegimpl = 0;
   nposcont = 0;
   nnegcont = 0;
   ncoeffspone = 0;
   ncoeffsnone = 0;
   ncoeffspint = 0;
   ncoeffsnint = 0;
   ncoeffspfrac = 0;
   ncoeffsnfrac = 0;
   integral = TRUE;
   poscoeffsum = 0.0;
   negcoeffsum = 0.0;
   for( i = 0; i < lincons->nvars; ++i )
   {
      var = lincons->vars[i];
      val = lincons->vals[i];
      lb = SCIPvarGetLbLocal(var);
      ub = SCIPvarGetUbLocal(var);
      assert(!SCIPisZero(scip, val));

      switch( SCIPvarGetType(var) )
      {
      case SCIP_VARTYPE_BINARY:
         if( !SCIPisZero(scip, lb) || !SCIPisZero(scip, ub) )
            integral &= SCIPisIntegral(scip, val);
         if( val >= 0.0 )
            nposbin++;
         else
            nnegbin++;
         break;
      case SCIP_VARTYPE_INTEGER:
         if( !SCIPisZero(scip, lb) || !SCIPisZero(scip, ub) )
            integral &= SCIPisIntegral(scip, val);
         if( val >= 0.0 )
            nposint++;
         else
            nnegint++;
         break;
      case SCIP_VARTYPE_IMPLINT:
         if( !SCIPisZero(scip, lb) || !SCIPisZero(scip, ub) )
            integral &= SCIPisIntegral(scip, val);
         if( val >= 0.0 )
            nposimpl++;
         else
            nnegimpl++;
         break;
      case SCIP_VARTYPE_CONTINOUS:
         integral &= (SCIPisEQ(scip, lb, ub) && SCIPisIntegral(scip, val * lb));
         if( val >= 0.0 )
            nposcont++;
         else
            nnegcont++;
         break;
      default:
         errorMessage("unknown variable type");
         return SCIP_INVALIDDATA;
      }
      if( SCIPisEQ(scip, val, 1.0) )
         ncoeffspone++;
      else if( SCIPisEQ(scip, val, -1.0) )
         ncoeffsnone++;
      else if( SCIPisIntegral(scip, val) )
      {
         if( SCIPisPositive(scip, val) )
            ncoeffspint++;
         else
            ncoeffsnint++;
      }
      else
      {
         if( SCIPisPositive(scip, val) )
            ncoeffspfrac++;
         else
            ncoeffsnfrac++;
      }
      if( SCIPisPositive(scip, val) )
         poscoeffsum += val;
      else
         negcoeffsum += val;
   }



   /*
    * call the upgrading methods
    */

   debugMessage("upgrading linear constraint <%s> (%d upgrade methods):\n", 
      SCIPconsGetName(cons), conshdlrdata->nlinconsupgrades);
   debugMessage(" +bin=%d -bin=%d +int=%d -int=%d +impl=%d -impl=%d +cont=%d -cont=%d +1=%d -1=%d +I=%d -I=%d +F=%d -F=%d possum=%g negsum=%g integral=%d\n",
      nposbin, nnegbin, nposint, nnegint, nposimpl, nnegimpl, nposcont, nnegcont,
      ncoeffspone, ncoeffsnone, ncoeffspint, ncoeffsnint, ncoeffspfrac, ncoeffsnfrac,
      poscoeffsum, negcoeffsum, integral);

   /* try all upgrading methods in priority order */
   for( i = 0; i < conshdlrdata->nlinconsupgrades && *upgdcons == NULL; ++i )
   {
      CHECK_OKAY( conshdlrdata->linconsupgrades[i]->linconsupgd(scip, cons, lincons->nvars, 
                     lincons->vars, lincons->vals, lincons->lhs, lincons->rhs, 
                     lincons->local, lincons->removeable,
                     nposbin, nnegbin, nposint, nnegint, nposimpl, nnegimpl, nposcont, nnegcont,
                     ncoeffspone, ncoeffsnone, ncoeffspint, ncoeffsnint, ncoeffspfrac, ncoeffsnfrac,
                     poscoeffsum, negcoeffsum, integral,
                     upgdcons) );
   }

#ifdef DEBUG
   if( *upgdcons != NULL )
   {
      conshdlr = SCIPconsGetHdlr(*upgdcons);
      debugMessage(" -> upgraded to constraint type <%s>\n", SCIPconshdlrGetName(conshdlr));
   }
#endif

   return SCIP_OKAY;
}
