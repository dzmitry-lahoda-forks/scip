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

/**@file   lp.c
 * @brief  LP management and variable's domains datastructures and methods
 * @author Tobias Achterberg
 */

/** The main datastructures for storing an LP are the rows and the columns.
 *  A row can live on its own (if it was created by a separator), or as LP
 *  relaxation of a constraint. Thus, it has a nuses-counter, and is
 *  deleted, if not needed any more.
 *  A column cannot live on its own. It is always connected to a problem
 *  variable. Because pricing is always problem specific, it cannot create
 *  LP columns without introducing new variables. Thus, each column is
 *  connected to exactly one variable, and is deleted, if the variable
 *  is deleted.
 *
 *  In LP management, we have to differ between the actual LP and the LP
 *  stored in the LP solver. All LP methods affect the actual LP only. 
 *  Before solving the actual LP with the LP solver or setting an LP state,
 *  the LP solvers data has to be updated to the actual LP with a call to
 *  lpFlush().
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <assert.h>
#include <math.h>
#include <limits.h>
#include <string.h>

#include "sort.h"
#include "lp.h"


/** list of columns */
struct ColList
{
   COL*             col;                /**< pointer to this column */
   COLLIST*         next;               /**< pointer to next collist entry */
};

/** list of rows */
struct RowList
{
   ROW*             row;                /**< pointer to this row */
   ROWLIST*         next;               /**< pointer to next rowlist entry */
};


/*
 * memory growing methods for dynamically allocated arrays
 */

/** ensures, that chgcols array can store at least num entries */
static
RETCODE ensureChgcolsSize(
   LP*              lp,                 /**< actual LP data */
   const SET*       set,                /**< global SCIP settings */
   int              num                 /**< minimum number of entries to store */
   )
{
   assert(lp->nchgcols <= lp->chgcolssize);
   
   if( num > lp->chgcolssize )
   {
      int newsize;

      newsize = SCIPsetCalcMemGrowSize(set, num);
      ALLOC_OKAY( reallocMemoryArray(&lp->chgcols, newsize) );
      lp->chgcolssize = newsize;
   }
   assert(num <= lp->chgcolssize);

   return SCIP_OKAY;
}

/** ensures, that chgrows array can store at least num entries */
static
RETCODE ensureChgrowsSize(
   LP*              lp,                 /**< actual LP data */
   const SET*       set,                /**< global SCIP settings */
   int              num                 /**< minimum number of entries to store */
   )
{
   assert(lp->nchgrows <= lp->chgrowssize);
   
   if( num > lp->chgrowssize )
   {
      int newsize;

      newsize = SCIPsetCalcMemGrowSize(set, num);
      ALLOC_OKAY( reallocMemoryArray(&lp->chgrows, newsize) );
      lp->chgrowssize = newsize;
   }
   assert(num <= lp->chgrowssize);

   return SCIP_OKAY;
}

/** ensures, that lpicols array can store at least num entries */
static
RETCODE ensureLpicolsSize(
   LP*              lp,                 /**< actual LP data */
   const SET*       set,                /**< global SCIP settings */
   int              num                 /**< minimum number of entries to store */
   )
{
   assert(lp->nlpicols <= lp->lpicolssize);
   
   if( num > lp->lpicolssize )
   {
      int newsize;

      newsize = SCIPsetCalcMemGrowSize(set, num);
      ALLOC_OKAY( reallocMemoryArray(&lp->lpicols, newsize) );
      lp->lpicolssize = newsize;
   }
   assert(num <= lp->lpicolssize);

   return SCIP_OKAY;
}

/** ensures, that lpirows array can store at least num entries */
static
RETCODE ensureLpirowsSize(
   LP*              lp,                 /**< actual LP data */
   const SET*       set,                /**< global SCIP settings */
   int              num                 /**< minimum number of entries to store */
   )
{
   assert(lp->nlpirows <= lp->lpirowssize);
   
   if( num > lp->lpirowssize )
   {
      int newsize;

      newsize = SCIPsetCalcMemGrowSize(set, num);
      ALLOC_OKAY( reallocMemoryArray(&lp->lpirows, newsize) );
      lp->lpirowssize = newsize;
   }
   assert(num <= lp->lpirowssize);

   return SCIP_OKAY;
}

/** ensures, that cols array can store at least num entries */
static
RETCODE ensureColsSize(
   LP*              lp,                 /**< actual LP data */
   const SET*       set,                /**< global SCIP settings */
   int              num                 /**< minimum number of entries to store */
   )
{
   assert(lp->ncols <= lp->colssize);
   
   if( num > lp->colssize )
   {
      int newsize;

      newsize = SCIPsetCalcMemGrowSize(set, num);
      ALLOC_OKAY( reallocMemoryArray(&lp->cols, newsize) );
      lp->colssize = newsize;
   }
   assert(num <= lp->colssize);

   return SCIP_OKAY;
}

/** ensures, that rows array can store at least num entries */
static
RETCODE ensureRowsSize(
   LP*              lp,                 /**< actual LP data */
   const SET*       set,                /**< global SCIP settings */
   int              num                 /**< minimum number of entries to store */
   )
{
   assert(lp->nrows <= lp->rowssize);
   
   if( num > lp->rowssize )
   {
      int newsize;

      newsize = SCIPsetCalcMemGrowSize(set, num);
      ALLOC_OKAY( reallocMemoryArray(&lp->rows, newsize) );
      lp->rowssize = newsize;
   }
   assert(num <= lp->rowssize);

   return SCIP_OKAY;
}

/** ensures, that row array of column can store at least num entries */
static
RETCODE ensureColSize(
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   COL*             col,                /**< LP column */
   int              num                 /**< minimum number of entries to store */
   )
{
   assert(col->len <= col->size);
   
   if( num > col->size )
   {
      int newsize;

      newsize = SCIPsetCalcMemGrowSize(set, num);
      ALLOC_OKAY( reallocBlockMemoryArray(memhdr, &col->rows, col->size, newsize) );
      ALLOC_OKAY( reallocBlockMemoryArray(memhdr, &col->vals, col->size, newsize) );
      ALLOC_OKAY( reallocBlockMemoryArray(memhdr, &col->linkpos, col->size, newsize) );
      col->size = newsize;
   }
   assert(num <= col->size);

   return SCIP_OKAY;
}

/** ensures, that column array of row can store at least num entries */
static
RETCODE ensureRowSize(
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   ROW*             row,                /**< LP row */
   int              num                 /**< minimum number of entries to store */
   )
{
   assert(row->len <= row->size);
   
   if( num > row->size )
   {
      int newsize;

      newsize = SCIPsetCalcMemGrowSize(set, num);
      ALLOC_OKAY( reallocBlockMemoryArray(memhdr, &row->cols, row->size, newsize) );
      ALLOC_OKAY( reallocBlockMemoryArray(memhdr, &row->vals, row->size, newsize) );
      ALLOC_OKAY( reallocBlockMemoryArray(memhdr, &row->linkpos, row->size, newsize) );
      row->size = newsize;
   }
   assert(num <= row->size);

   return SCIP_OKAY;
}


/*
 * compare methods for sorting
 */

static
DECL_SORTPTRCOMP(cmpRow)
{
   return ((ROW*)elem1)->index - ((ROW*)elem2)->index;
}

static
DECL_SORTPTRCOMP(cmpCol)
{
   return ((COL*)elem1)->index - ((COL*)elem2)->index;
}



#if 0
static
void checkLinks(
   LP*              lp                  /**< actual LP data */
   )
{
   COL* col;
   ROW* row;
   int i;
   int j;

   assert(lp != NULL);

   for( i = 0; i < lp->ncols; ++i )
   {
      col = lp->cols[i];
      assert(col != NULL);

      for( j = 0; j < col->len; ++j )
      {
         row = col->rows[j];
         assert(row != NULL);
         assert(col->linkpos[j] == -1 || row->cols[col->linkpos[j]] == col);
      }
   }

   for( i = 0; i < lp->nrows; ++i )
   {
      row = lp->rows[i];
      assert(row != NULL);

      for( j = 0; j < row->len; ++j )
      {
         col = row->cols[j];
         assert(col != NULL);
         assert(row->linkpos[j] == -1 || col->rows[row->linkpos[j]] == row);
      }
   }
}
#else
#define checkLinks(lp) /**/
#endif


/*
 * Changing announcements
 */

/** announces, that the given coefficient in the constraint matrix changed */
static
void coefChanged(
   ROW*             row,                /**< LP row */
   COL*             col,                /**< LP col */
   LP*              lp                  /**< actual LP data */
   )
{
   assert(row != NULL);
   assert(col != NULL);
   assert(lp != NULL);

   if( row->lpipos >= 0 && col->lpipos >= 0 )
   {
      assert(row->lpipos < lp->nlpirows);
      assert(col->lpipos < lp->nlpicols);

      /* we have to remember the change only in the row or in the column, 
       * because the readdition of one vector would change the other automatically.
       */
      if( row->lpipos >= lp->lpifirstchgrow )
         row->coefchanged = TRUE;
      else if( col->lpipos >= lp->lpifirstchgcol )
         col->coefchanged = TRUE;
      else if( lp->lpifirstchgrow - row->lpipos <= lp->lpifirstchgcol - col->lpipos )
      {
         row->coefchanged = TRUE;
         lp->lpifirstchgrow = row->lpipos;
      }
      else
      {
         col->coefchanged = TRUE;
         lp->lpifirstchgcol = col->lpipos;
      }
      lp->flushed = FALSE;
      lp->solved = FALSE;
      lp->dualfeasible = FALSE;
      lp->primalfeasible = FALSE;
      lp->objval = SCIP_INVALID;
      lp->lpsolstat = SCIP_LPSOLSTAT_NOTSOLVED;
   }
}

   


/*
 * local column changing methods
 */

/** searches coefficient in column, returns position in col vector or -1 */
static
int colSearchCoeff(
   COL*             col,                /**< column to be searched in */
   const ROW*       row                 /**< coefficient to be searched for */
   )
{
   int actpos;
   int minpos;
   int maxpos;
   int actidx;
   int searchidx;

   assert(col != NULL);
   assert(row != NULL);

   /* row has to be sorted, such that binary search works */
   if( !col->sorted )
      SCIPcolSort(col);
   assert(col->sorted);

   /* binary search */
   searchidx = row->index;
   minpos = 0;
   maxpos = col->len-1;
   while(minpos <= maxpos)
   {
      actpos = (minpos + maxpos)/2;
      assert(0 <= actpos && actpos < col->len);
      actidx = col->rows[actpos]->index;
      if( searchidx == actidx )
         return actpos;
      else if( searchidx < actidx )
         maxpos = actpos-1;
      else
         minpos = actpos+1;
   }

   return -1;
}

/** adds a previously non existing coefficient to an LP column */
static
RETCODE colAddCoeff(
   COL*             col,                /**< LP column */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   ROW*             row,                /**< LP row */
   Real             val,                /**< value of coefficient */
   int              linkpos,            /**< position of column in the row's col array, or -1 */
   int*             rowpos              /**< pointer to store the position of the row in the column's row array, or NULL */
   )
{
   assert(memhdr != NULL);
   assert(col != NULL);
   assert(col->var != NULL);
   assert(row != NULL);
   assert(!SCIPsetIsZero(set, val));
   assert(colSearchCoeff(col, row) == -1);

   checkLinks(lp);

   debugMessage("adding coefficient %g * <%s> at position %d to column <%s>\n", val, row->name, col->len, col->var->name);

   if( col->len > 0 )
      col->sorted &= (col->rows[col->len-1]->index < row->index);

   CHECK_OKAY( ensureColSize(memhdr, set, col, col->len+1) );
   assert(col->rows != NULL);
   assert(col->vals != NULL);
   assert(col->linkpos != NULL);

   if( rowpos != NULL )
      *rowpos = col->len;
   col->rows[col->len] = row;
   col->vals[col->len] = val;
   col->linkpos[col->len] = linkpos;
   if( linkpos == -1 )
      col->nunlinked++;
   col->len++;

   coefChanged(row, col, lp);
      
   return SCIP_OKAY;
}

/** deletes coefficient at given position from column */
static
RETCODE colDelCoeffPos(
   COL*             col,                /**< column to be changed */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   int              pos                 /**< position in column vector to delete */
   )
{
   ROW* row;
   Real val;

   assert(col != NULL);
   assert(col->var != NULL);
   assert(set != NULL);
   assert(0 <= pos && pos < col->len);
   assert(col->rows[pos] != NULL);
   assert(col->linkpos[pos] == -1 || col->rows[pos]->cols[col->linkpos[pos]] == col);

   row = col->rows[pos];
   val = col->vals[pos];

   debugMessage("deleting coefficient %g * <%s> at position %d from column <%s>\n", val, row->name, pos, col->var->name);

   if( col->linkpos[pos] == -1 )
      col->nunlinked--;

   if( pos < col->len-1 )
   {
      /* move last coefficient to position of deleted coefficient */
      col->rows[pos] = col->rows[col->len-1];
      col->vals[pos] = col->vals[col->len-1];
      col->linkpos[pos] = col->linkpos[col->len-1];

      /* if the moved coefficient is linked, update the link */
      if( col->linkpos[pos] != -1 )
         col->rows[pos]->linkpos[col->linkpos[pos]] = pos;

      col->sorted = FALSE;
   }
   col->len--;

   coefChanged(row, col, lp);

   return SCIP_OKAY;
}

/** changes a coefficient at given position of an LP column */
static
RETCODE colChgCoeffPos(
   COL*             col,                /**< LP column */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   int              pos,                /**< position in column vector to change */
   Real             val                 /**< value of coefficient */
   )
{
   assert(memhdr != NULL);
   assert(col != NULL);
   assert(col->var != NULL);
   assert(0 <= pos && pos < col->len);
   assert(col->rows[pos] != NULL);
   assert(col->linkpos[pos] == -1 || col->rows[pos]->cols[col->linkpos[pos]] == col);

   debugMessage("changing coefficient %g * <%s> at position %d of column <%s> to %g\n", 
      col->vals[pos], col->rows[pos]->name, pos, col->var->name, val);

   if( SCIPsetIsZero(set, val) )
   {
      /* delete existing coefficient */
      CHECK_OKAY( colDelCoeffPos(col, set, lp, pos) );
   }
   else if( !SCIPsetIsEQ(set, col->vals[pos], val) )
   {
      /* change existing coefficient */
      col->vals[pos] = val;
      coefChanged(col->rows[pos], col, lp);
   }

   return SCIP_OKAY;
}



/*
 * local row changing methods
 */

/** searches coefficient in row, returns position in row vector or -1 */
static
int rowSearchCoeff(
   ROW*             row,                /**< row to be searched in */
   const COL*       col                 /**< coefficient to be searched for */
   )
{
   int actpos;
   int minpos;
   int maxpos;
   int actidx;
   int searchidx;

   assert(row != NULL);
   assert(col != NULL);

   /* row has to be sorted, such that binary search works */
   if( !row->sorted )
      SCIProwSort(row);
   assert(row->sorted);

   /* binary search */
   searchidx = col->index;
   minpos = 0;
   maxpos = row->len-1;
   while(minpos <= maxpos)
   {
      actpos = (minpos + maxpos)/2;
      assert(0 <= actpos && actpos < row->len);
      actidx = row->cols[actpos]->index;
      if( searchidx == actidx )
         return actpos;
      else if( searchidx < actidx )
         maxpos = actpos-1;
      else
         minpos = actpos+1;
   }

   return -1;
}

/** update row norms after addition of new coefficient */
static
void rowAddNorms(
   ROW*             row,                /**< LP row */
   const SET*       set,                /**< global SCIP settings */
   int              colidx,             /**< column index of new coefficient, or -1 */
   Real             val                 /**< value of new coefficient */
   )
{
   Real absval;

   assert(row != NULL);
   assert(row->nummaxval >= 0);
   assert(set != NULL);

   absval = ABS(val);
   assert(!SCIPsetIsZero(set, absval));

   /* update min/maxidx */
   if( colidx != -1 )
   {
      row->minidx = MIN(row->minidx, colidx);
      row->maxidx = MAX(row->maxidx, colidx);
   }

   /* update squared euclidean norm */
   row->sqrnorm += SQR(absval);

   /* update maximum norm */
   if( row->nummaxval > 0 )
   {
      if( SCIPsetIsGT(set, absval, row->maxval) )
      {
         row->maxval = absval;
         row->nummaxval = 1;
      }
      else if( SCIPsetIsGE(set, absval, row->maxval) )
      {
         assert(row->nummaxval >= 1);
         row->nummaxval++;
      }
   }
}

/** update row norms after deletion of coefficient */
static
void rowDelNorms(
   ROW*             row,                /**< LP row */
   const SET*       set,                /**< global SCIP settings */
   int              colidx,             /**< column index of deleted coefficient, or -1 */
   Real             val                 /**< value of deleted coefficient */
   )
{
   Real absval;

   assert(row != NULL);
   assert(row->nummaxval >= 0);
   assert(set != NULL);

   absval = ABS(val);
   assert(!SCIPsetIsZero(set, absval));
   assert(SCIPsetIsGE(set, row->maxval, absval));

   /* update min/maxidx validity */
   if( colidx != -1 )
   {
      if( colidx == row->minidx || colidx == row->maxidx )
         row->validminmaxidx = FALSE;
   }

   /* update squared euclidean norm */
   row->sqrnorm -= SQR(absval);
   row->sqrnorm = MAX(row->sqrnorm, 0.0);

   /* update maximum norm */
   if( row->nummaxval > 0 )
   {
      if( SCIPsetIsGE(set, absval, row->maxval) )
         row->nummaxval--;
   }
}

/** adds a previously non existing coefficient to an LP row */
static
RETCODE rowAddCoeff(
   ROW*             row,                /**< LP row */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   COL*             col,                /**< LP column */
   Real             val,                /**< value of coefficient */
   int              linkpos,            /**< position of row in the column's row array, or -1 */
   int*             colpos              /**< pointer to store the position of the column in the row's col array, or NULL */
   )
{
   assert(row != NULL);
   assert(memhdr != NULL);
   assert(col != NULL);
   assert(col->var != NULL);
   assert(!SCIPsetIsZero(set, val));
   assert(rowSearchCoeff(row, col) == -1);

   checkLinks(lp);

   debugMessage("adding coefficient %g * <%s> at position %d to row <%s>\n", val, col->var->name, row->len, row->name);

   if( row->nlocks > 0 )
   {
      char s[255];
      sprintf(s, "cannot add a coefficient to the locked unmodifiable row <%s>", row->name);
      errorMessage(s);
      return SCIP_INVALIDDATA;
   }

   if( row->len > 0 )
      row->sorted &= (row->cols[row->len-1]->index < col->index);

   CHECK_OKAY( ensureRowSize(memhdr, set, row, row->len+1) );
   assert(row->cols != NULL);
   assert(row->vals != NULL);

   if( colpos != NULL )
      *colpos = row->len;
   row->cols[row->len] = col;
   row->vals[row->len] = val;
   row->linkpos[row->len] = linkpos;
   if( linkpos == -1 )
      row->nunlinked++;
   row->len++;

   row->pseudoactivity = SCIP_INVALID;
   row->validpsactivity = FALSE;
   CHECK_OKAY( SCIProwInvalidActivityBounds(row) );

   rowAddNorms(row, set, col->index, val);

   coefChanged(row, col, lp);

   return SCIP_OKAY;
}

/** deletes coefficient at given position from row */
static
RETCODE rowDelCoeffPos(
   ROW*             row,                /**< row to be changed */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   int              pos                 /**< position in row vector to delete */
   )
{
   COL* col;
   Real val;

   assert(row != NULL);
   assert(set != NULL);
   assert(0 <= pos && pos < row->len);
   assert(row->cols[pos] != NULL);
   assert(row->linkpos[pos] == -1 || row->cols[pos]->rows[row->linkpos[pos]] == row);

   col = row->cols[pos];
   val = row->vals[pos];
   
   debugMessage("deleting coefficient %g * <%s> at position %d from row <%s>\n", val, col->var->name, pos, row->name);

   if( row->nlocks > 0 )
   {
      char s[255];
      sprintf(s, "cannot delete a coefficient from the locked unmodifiable row <%s>", row->name);
      errorMessage(s);
      return SCIP_INVALIDDATA;
   }

   if( row->linkpos[pos] == -1 )
      row->nunlinked--;
   
   if( pos < row->len-1 )
   {
      /* move last coefficient to position of deleted coefficient */
      row->cols[pos] = row->cols[row->len-1];
      row->vals[pos] = row->vals[row->len-1];
      row->linkpos[pos] = row->linkpos[row->len-1];

      /* if the moved coefficient is linked, update the link */
      if( row->linkpos[pos] != -1 )
         row->cols[pos]->linkpos[row->linkpos[pos]] = pos;

      row->sorted = FALSE;
   }
   row->len--;
   
   row->pseudoactivity = SCIP_INVALID;
   row->validpsactivity = FALSE;
   CHECK_OKAY( SCIProwInvalidActivityBounds(row) );

   rowDelNorms(row, set, col->index, val);

   coefChanged(row, col, lp);

   return SCIP_OKAY;
}

/** changes a coefficient at given position of an LP row */
static
RETCODE rowChgCoeffPos(
   ROW*             row,                /**< LP row */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   int              pos,                /**< position in row vector to change */
   Real             val                 /**< value of coefficient */
   )
{
   assert(memhdr != NULL);
   assert(row != NULL);
   assert(0 <= pos && pos < row->len);
   assert(row->cols[pos] != NULL);
   assert(row->linkpos[pos] == -1 || row->cols[pos]->rows[row->linkpos[pos]] == row);

   debugMessage("changing coefficient %g * <%s> at position %d of row <%s> to %g\n", 
      row->vals[pos], row->cols[pos]->var->name, pos, row->name, val);

   if( row->nlocks > 0 )
   {
      char s[255];
      sprintf(s, "cannot change a coefficient of the locked unmodifiable row <%s>", row->name);
      errorMessage(s);
      return SCIP_INVALIDDATA;
   }

   if( SCIPsetIsZero(set, val) )
   {
      /* delete existing coefficient */
      CHECK_OKAY( rowDelCoeffPos(row, set, lp, pos) );
   }
   else if( !SCIPsetIsEQ(set, row->vals[pos], val) )
   {
      /* change existing coefficient */
      rowDelNorms(row, set, -1, row->vals[pos]);
      row->vals[pos] = val;
      rowAddNorms(row, set, -1, row->vals[pos]);
      coefChanged(row, row->cols[pos], lp);

      row->pseudoactivity = SCIP_INVALID;
      row->validpsactivity = FALSE;
      CHECK_OKAY( SCIProwInvalidActivityBounds(row) );
   }

   return SCIP_OKAY;
}

/** notifies LP row, that its sides were changed */
static
RETCODE rowSideChanged(
   ROW*             row,                /**< LP row */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   SIDETYPE         sidetype            /**< type of side: left or right hand side */
   )
{
   assert(row != NULL);
   assert(lp != NULL);

   if( row->lpipos >= 0 )
   {
      /* insert row in the chgrows list (if not already there) */
      if( !row->lhschanged && !row->rhschanged )
      {
         CHECK_OKAY( ensureChgrowsSize(lp, set, lp->nchgrows+1) );
         lp->chgrows[lp->nchgrows] = row;
         lp->nchgrows++;
      }
      
      /* mark side change in the row */
      switch( sidetype )
      {
      case SCIP_SIDETYPE_LEFT:
         row->lhschanged = TRUE;
         break;
      case SCIP_SIDETYPE_RIGHT:
         row->rhschanged = TRUE;
         break;
      default:
         errorMessage("Unknown row side type");
         abort();
      }
      
      lp->flushed = FALSE;
      lp->solved = FALSE;
      lp->primalfeasible = FALSE;
      lp->objval = SCIP_INVALID;
      lp->lpsolstat = SCIP_LPSOLSTAT_NOTSOLVED;

      assert(lp->nchgrows > 0);
   }

   return SCIP_OKAY;
}

/** updates rows pseudo activity after the best bound of column changed */
static
void rowUpdatePseudoActivity(
   ROW*             row,                /**< row data */
   int              pos,                /**< position of column in row, that was changed */
   Real             oldbestbound,       /**< old best (w.r.t. objective function) bound of column */
   Real             newbestbound        /**< new best (w.r.t. objective function) bound of column */
   )
{
   assert(row != NULL);
   assert(pos >= 0 && pos < row->len);
   assert(row->cols[pos] != NULL);
   assert(row->cols[pos]->var != NULL);
   assert(row->cols[pos]->var->varstatus == SCIP_VARSTATUS_COLUMN);
   assert(row->cols[pos]->var->data.col == row->cols[pos]);

   if( row->validpsactivity )
   {
      assert(row->pseudoactivity < SCIP_INVALID);
      row->pseudoactivity += (newbestbound - oldbestbound) * row->vals[pos];
   }
}

/** updates rows pseudo activity after the constant was changed */
static
void rowUpdatePseudoActivityConstant(
   ROW*             row,                /**< row data */
   Real             oldconstant,        /**< old constant of row */
   Real             newconstant         /**< new constant of row */
   )
{
   assert(row != NULL);

   if( row->validpsactivity )
   {
      assert(row->pseudoactivity < SCIP_INVALID);
      row->pseudoactivity += newconstant - oldconstant;
   }
}

/** updates rows activity bounds after the lower bound of column changed */
static
void rowUpdateActivityBoundsLb(
   ROW*             row,                /**< row data */
   const SET*       set,                /**< global SCIP settings */
   int              pos,                /**< position of column in row, that was changed */
   Real             oldlb,              /**< old lower bound of column */
   Real             newlb               /**< new lower bound of column */
   )
{
   assert(row != NULL);
   assert(pos >= 0 && pos < row->len);
   assert(row->cols[pos] != NULL);
   assert(row->cols[pos]->var != NULL);
   assert(row->cols[pos]->var->varstatus == SCIP_VARSTATUS_COLUMN);
   assert(row->cols[pos]->var->data.col == row->cols[pos]);

   if( row->validactivitybds )
   {
      Real val;

      assert(row->minactivity < SCIP_INVALID);
      assert(row->maxactivity < SCIP_INVALID);
      assert(row->minactivityinf >= 0);
      assert(row->maxactivityinf >= 0);
      assert(!SCIPsetIsInfinity(set, oldlb));
      assert(!SCIPsetIsInfinity(set, newlb));

      val = row->vals[pos];
      if( val > 0.0 )
      {
         if( SCIPsetIsInfinity(set, -oldlb) )
         {
            assert(row->minactivityinf >= 1);
            row->minactivityinf--;
         }
         else
            row->minactivity -= val * oldlb;

         if( SCIPsetIsInfinity(set, -newlb) )
            row->minactivityinf++;
         else
            row->minactivity += val * newlb;
      }
      else
      {
         if( SCIPsetIsInfinity(set, -oldlb) )
         {
            assert(row->maxactivityinf >= 1);
            row->maxactivityinf--;
         }
         else
            row->maxactivity -= val * oldlb;

         if( SCIPsetIsInfinity(set, -newlb) )
            row->maxactivityinf++;
         else
            row->maxactivity += val * newlb;
      }
   }
}

/** updates rows activity bounds after the upper bound of column changed */
static
void rowUpdateActivityBoundsUb(
   ROW*             row,                /**< row data */
   const SET*       set,                /**< global SCIP settings */
   int              pos,                /**< position of column in row, that was changed */
   Real             oldub,              /**< old upper bound of column */
   Real             newub               /**< new upper bound of column */
   )
{
   assert(row != NULL);
   assert(pos >= 0 && pos < row->len);
   assert(row->cols[pos] != NULL);
   assert(row->cols[pos]->var != NULL);
   assert(row->cols[pos]->var->varstatus == SCIP_VARSTATUS_COLUMN);
   assert(row->cols[pos]->var->data.col == row->cols[pos]);

   if( row->validactivitybds )
   {
      Real val;

      assert(row->minactivity < SCIP_INVALID);
      assert(row->maxactivity < SCIP_INVALID);
      assert(row->minactivityinf >= 0);
      assert(row->maxactivityinf >= 0);
      assert(!SCIPsetIsInfinity(set, -oldub));
      assert(!SCIPsetIsInfinity(set, -newub));

      val = row->vals[pos];
      if( val > 0.0 )
      {
         if( SCIPsetIsInfinity(set, oldub) )
         {
            assert(row->maxactivityinf >= 1);
            row->maxactivityinf--;
         }
         else
            row->maxactivity -= val * oldub;

         if( SCIPsetIsInfinity(set, newub) )
            row->maxactivityinf++;
         else
            row->maxactivity += val * newub;
      }
      else
      {
         if( SCIPsetIsInfinity(set, oldub) )
         {
            assert(row->minactivityinf >= 1);
            row->minactivityinf--;
         }
         else
            row->minactivity -= val * oldub;

         if( SCIPsetIsInfinity(set, newub) )
            row->minactivityinf++;
         else
            row->minactivity += val * newub;
      }
   }
}

/** updates rows activity bounds after the constant was changed */
static
void rowUpdateActivityBoundsConstant(
   ROW*             row,                /**< row data */
   Real             oldconstant,        /**< old constant of row */
   Real             newconstant         /**< new constant of row */
   )
{
   assert(row != NULL);

   if( row->validactivitybds )
   {
      assert(row->minactivity < SCIP_INVALID);
      assert(row->maxactivity < SCIP_INVALID);
      assert(row->minactivityinf >= 0);
      assert(row->maxactivityinf >= 0);
      row->minactivity += newconstant - oldconstant;
      row->maxactivity += newconstant - oldconstant;
   }
}




/*
 * double linked coefficient matrix methods 
 */

/** insert column coefficients in corresponding rows */
static
RETCODE colLink(
   COL*             col,                /**< column data */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp                  /**< actual LP data */
   )
{
   int i;

   assert(col != NULL);
   assert(col->var != NULL);
   assert(memhdr != NULL);
   assert(set != NULL);
   assert(lp != NULL);

   if( col->nunlinked > 0 )
   {
      debugMessage("linking column <%s>\n", col->var->name);
      for( i = 0; i < col->len; ++i )
      {
         assert(!SCIPsetIsZero(set, col->vals[i]));
         if( col->linkpos[i] == -1 )
         {
            CHECK_OKAY( rowAddCoeff(col->rows[i], memhdr, set, lp, col, col->vals[i], i, &col->linkpos[i]) );
            col->nunlinked--;
         }
         assert(col->rows[i]->cols[col->linkpos[i]] == col);
         assert(col->rows[i]->linkpos[col->linkpos[i]] == i);
      }
   }
   assert(col->nunlinked == 0);

   checkLinks(lp);

   return SCIP_OKAY;
}

/** removes column coefficients from corresponding rows */
static
RETCODE colUnlink(
   COL*             col,                /**< column data */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp                  /**< actual LP data */
   )
{
   int i;

   assert(col != NULL);
   assert(col->var != NULL);
   assert(memhdr != NULL);
   assert(set != NULL);
   assert(lp != NULL);

   if( col->nunlinked < col->len )
   {
      debugMessage("unlinking column <%s>\n", col->var->name);
      for( i = 0; i < col->len; ++i )
      {
         if( col->linkpos[i] != -1 )
         {
            assert(col->rows[i]->cols[col->linkpos[i]] == col);
            CHECK_OKAY( rowDelCoeffPos(col->rows[i], set, lp, col->linkpos[i]) );
            col->linkpos[i] = -1;
            col->nunlinked++;
         }
      }
   }
   assert(col->nunlinked == col->len);

   checkLinks(lp);

   return SCIP_OKAY;
}

/** insert row coefficients in corresponding columns */
static
RETCODE rowLink(
   ROW*             row,                /**< row data */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp                  /**< actual LP data */
   )
{
   int i;

   assert(row != NULL);
   assert(memhdr != NULL);
   assert(set != NULL);
   assert(lp != NULL);

   if( row->nunlinked > 0 )
   {
      debugMessage("linking row <%s>\n", row->name);
      for( i = 0; i < row->len; ++i )
      {
         assert(!SCIPsetIsZero(set, row->vals[i]));
         if( row->linkpos[i] == -1 )
         {
            CHECK_OKAY( colAddCoeff(row->cols[i], memhdr, set, lp, row, row->vals[i], i, &row->linkpos[i]) );
            row->nunlinked--;
         }
         assert(row->cols[i]->rows[row->linkpos[i]] == row);
         assert(row->cols[i]->linkpos[row->linkpos[i]] == i);
      }
   }
   assert(row->nunlinked == 0);

   checkLinks(lp);

   return SCIP_OKAY;
}

/** removes row coefficients from corresponding columns */
static
RETCODE rowUnlink(
   ROW*             row,                /**< row data */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp                  /**< actual LP data */
   )
{
   int i;

   assert(row != NULL);
   assert(memhdr != NULL);
   assert(set != NULL);
   assert(lp != NULL);

   if( row->nunlinked < row->len )
   {
      debugMessage("unlinking row <%s>\n", row->name);
      for( i = 0; i < row->len; ++i )
      {
         if( row->linkpos[i] != -1 )
         {
            assert(row->cols[i]->rows[row->linkpos[i]] == row);
            CHECK_OKAY( colDelCoeffPos(row->cols[i], set, lp, row->linkpos[i]) );
            row->nunlinked++;
         }
      }
   }
   assert(row->nunlinked == row->len);

   checkLinks(lp);

   return SCIP_OKAY;
}



/*
 * Column methods
 */

/** creates an LP column */
RETCODE SCIPcolCreate(
   COL**            col,                /**< pointer to column data */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   STAT*            stat,               /**< problem statistics */
   LP*              lp,                 /**< actual LP data */
   VAR*             var,                /**< variable, this column represents */
   int              len,                /**< number of nonzeros in the column */
   ROW**            row,                /**< array with rows of column entries */
   Real*            val                 /**< array with coefficients of column entries */
   )
{
   int i;

   assert(col != NULL);
   assert(memhdr != NULL);
   assert(set != NULL);
   assert(lp != NULL);
   assert(stat != NULL);
   assert(var != NULL);
   assert(len >= 0);
   assert(len == 0 || (row != NULL && val != NULL));

   ALLOC_OKAY( allocBlockMemory(memhdr, col) );

   if( len > 0 )
   {
      int i;

      ALLOC_OKAY( duplicateBlockMemoryArray(memhdr, &(*col)->rows, row, len) );
      ALLOC_OKAY( duplicateBlockMemoryArray(memhdr, &(*col)->vals, val, len) );
      ALLOC_OKAY( allocBlockMemoryArray(memhdr, &(*col)->linkpos, len) );
      for( i = 0; i < len; ++i )
         (*row)->linkpos[i] = -1;
   }
   else
   {
      (*col)->rows = NULL;
      (*col)->vals = NULL;
      (*col)->linkpos = NULL;
   }

   (*col)->var = var;
   (*col)->index = stat->ncolidx++;
   (*col)->size = len;
   (*col)->len = len;
   (*col)->nunlinked = len;
   (*col)->lppos = -1;
   (*col)->lpipos = -1;
   (*col)->primsol = 0.0;
   (*col)->redcost = SCIP_INVALID;
   (*col)->farkas = SCIP_INVALID;
   (*col)->strongdown = SCIP_INVALID;
   (*col)->strongup = SCIP_INVALID;
   (*col)->validredcostlp = -1;
   (*col)->validfarkaslp = -1;
   (*col)->strongitlim = -1;
   (*col)->sorted = TRUE;
   (*col)->lbchanged = FALSE;
   (*col)->ubchanged = FALSE;
   (*col)->coefchanged = FALSE;

   /* check, if column is sorted
    */
   for( i = 0; i < len; ++i )
   {
      assert(!SCIPsetIsZero(set, (*col)->vals[i]));
      (*col)->sorted &= (i == 0 || (*col)->rows[i-1]->index < (*col)->rows[i]->index);
   }

   return SCIP_OKAY;
}

/** frees an LP column */
RETCODE SCIPcolFree(
   COL**            col,                /**< pointer to LP column */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp                  /**< actual LP data */
   )
{
   assert(memhdr != NULL);
   assert(col != NULL);
   assert(*col != NULL);
   assert((*col)->var != NULL);
   assert((*col)->var->varstatus == SCIP_VARSTATUS_COLUMN);
   assert(&(*col)->var->data.col == col); /* SCIPcolFree() has to be called from SCIPvarFree() */
   assert((*col)->lppos == -1);

   /* remove column indices from corresponding rows */
   CHECK_OKAY( colUnlink(*col, memhdr, set, lp) );

   freeBlockMemoryArrayNull(memhdr, &(*col)->rows, (*col)->size);
   freeBlockMemoryArrayNull(memhdr, &(*col)->vals, (*col)->size);
   freeBlockMemoryArrayNull(memhdr, &(*col)->linkpos, (*col)->size);
   freeBlockMemory(memhdr, col);

   return SCIP_OKAY;
}

/** sorts column entries by row index */
void SCIPcolSort(
   COL*             col                 /**< column to be sorted */
   )
{
   if( !col->sorted )
   {
      int i;

      /* sort coefficients */
      SCIPbsortPtrDblInt((void**)(col->rows), col->vals, col->linkpos, col->len, &cmpRow);

      /* update links */
      for( i = 0; i < col->len; ++i )
      {
         if( col->linkpos[i] != -1 )
         {
            assert(col->rows[i]->cols[col->linkpos[i]] == col);
            assert(col->rows[i]->linkpos[col->linkpos[i]] != -1);
            col->rows[i]->linkpos[col->linkpos[i]] = i;
         }
      }

      col->sorted = TRUE;
   }
}

/** adds a previously non existing coefficient to an LP column */
RETCODE SCIPcolAddCoeff(
   COL*             col,                /**< LP column */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   ROW*             row,                /**< LP row */
   Real             val                 /**< value of coefficient */
   )
{
   CHECK_OKAY( colAddCoeff(col, memhdr, set, lp, row, val, -1, NULL) );

   checkLinks(lp);

   return SCIP_OKAY;
}

/** deletes existing coefficient from column */
RETCODE SCIPcolDelCoeff(
   COL*             col,                /**< column to be changed */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   ROW*             row                 /**< coefficient to be deleted */
   )
{
   int pos;

   assert(col != NULL);
   assert(col->var != NULL);
   assert(row != NULL);

   /* search the position of the row in the column's row vector */
   pos = colSearchCoeff(col, row);
   if( pos == -1 )
   {
      char s[255];
      sprintf(s, "coefficient for row <%s> doesn't exist in column <%s>", row->name, col->var->name);
      errorMessage(s);
      return SCIP_INVALIDDATA;
   }
   assert(0 <= pos && pos < col->len);
   assert(col->rows[pos] == row);

   checkLinks(lp);

   /* if row knows of the column, remove the column from the row's col vector */
   if( col->linkpos[pos] != -1 )
   {
      assert(row->cols[col->linkpos[pos]] == col);
      assert(SCIPsetIsEQ(set, row->vals[col->linkpos[pos]], col->vals[pos]));
      CHECK_OKAY( rowDelCoeffPos(row, set, lp, col->linkpos[pos]) );
   }

   /* delete the row from the column's row vector */
   CHECK_OKAY( colDelCoeffPos(col, set, lp, pos) );
   
   checkLinks(lp);

   return SCIP_OKAY;
}

/** changes or adds a coefficient to an LP column */
RETCODE SCIPcolChgCoeff(
   COL*             col,                /**< LP column */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   ROW*             row,                /**< LP row */
   Real             val                 /**< value of coefficient */
   )
{
   int pos;

   assert(col != NULL);
   assert(row != NULL);

   /* search the position of the row in the column's row vector */
   pos = colSearchCoeff(col, row);

   checkLinks(lp);

   /* check, if row already exists in the column's row vector */
   if( pos == -1 )
   {
      /* add previously not existing coefficient */
      CHECK_OKAY( colAddCoeff(col, memhdr, set, lp, row, val, -1, NULL) );
   }
   else
   {
      /* modifify already existing coefficient */
      assert(0 <= pos && pos < col->len);
      assert(col->rows[pos] == row);

      /* if row knows of the column, change the corresponding coefficient in the row */
      if( col->linkpos[pos] != -1 )
      {
         assert(row->cols[col->linkpos[pos]] == col);
         assert(SCIPsetIsEQ(set, row->vals[col->linkpos[pos]], col->vals[pos]));
         CHECK_OKAY( rowChgCoeffPos(row, memhdr, set, lp, col->linkpos[pos], val) );
      }

      /* change the coefficient in the column */
      CHECK_OKAY( colChgCoeffPos(col, memhdr, set, lp, pos, val) );
   }

   checkLinks(lp);

   return SCIP_OKAY;
}

/** increases value of an existing or nonexisting coefficient in an LP column */
RETCODE SCIPcolIncCoeff(
   COL*             col,                /**< LP column */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   ROW*             row,                /**< LP row */
   Real             incval              /**< value to add to the coefficient */
   )
{
   int pos;

   assert(col != NULL);
   assert(row != NULL);

   if( SCIPsetIsZero(set, incval) )
      return SCIP_OKAY;

   /* search the position of the row in the column's row vector */
   pos = colSearchCoeff(col, row);

   checkLinks(lp);

   /* check, if row already exists in the column's row vector */
   if( pos == -1 )
   {
      /* add previously not existing coefficient */
      CHECK_OKAY( colAddCoeff(col, memhdr, set, lp, row, incval, -1, NULL) );
   }
   else
   {
      /* modifify already existing coefficient */
      assert(0 <= pos && pos < col->len);
      assert(col->rows[pos] == row);

      /* if row knows of the column, change the corresponding coefficient in the row */
      if( col->linkpos[pos] != -1 )
      {
         assert(row->cols[col->linkpos[pos]] == col);
         assert(SCIPsetIsEQ(set, row->vals[col->linkpos[pos]], col->vals[pos]));
         CHECK_OKAY( rowChgCoeffPos(row, memhdr, set, lp, col->linkpos[pos], col->vals[pos] + incval) );
      }

      /* change the coefficient in the column */
      CHECK_OKAY( colChgCoeffPos(col, memhdr, set, lp, pos, col->vals[pos] + incval) );
   }

   checkLinks(lp);

   return SCIP_OKAY;
}

/** notifies LP, that the bounds of a column were changed */
RETCODE SCIPcolBoundChanged(
   COL*             col,                /**< LP column that changed */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   BOUNDTYPE        boundtype,          /**< type of bound: lower or upper bound */
   Real             oldbound,           /**< old bound value */
   Real             newbound            /**< new bound value */
   )
{
   int r;

   assert(col != NULL);
   assert(col->var != NULL);
   assert(col->var->varstatus == SCIP_VARSTATUS_COLUMN);
   assert(col->var->data.col == col);
   assert(lp != NULL);
   
   if( col->lpipos >= 0 )
   {
      /* insert column in the chgcols list (if not already there) */
      if( !col->lbchanged && !col->ubchanged )
      {
         CHECK_OKAY( ensureChgcolsSize(lp, set, lp->nchgcols+1) );
         lp->chgcols[lp->nchgcols] = col;
         lp->nchgcols++;
      }
      
      /* mark bound change in the column */
      switch( boundtype )
      {
      case SCIP_BOUNDTYPE_LOWER:
         col->lbchanged = TRUE;
         break;
      case SCIP_BOUNDTYPE_UPPER:
         col->ubchanged = TRUE;
         break;
      default:
         errorMessage("Unknown bound type");
         return SCIP_INVALIDDATA;
      }
      
      lp->flushed = FALSE;
      lp->solved = FALSE;
      lp->primalfeasible = FALSE;
      lp->objval = SCIP_INVALID;
      lp->lpsolstat = SCIP_LPSOLSTAT_NOTSOLVED;

      assert(lp->nchgcols > 0);
   }  

   /* update pseudo activity of rows */
   if( col->var->obj >= 0.0 && boundtype == SCIP_BOUNDTYPE_LOWER )
   {
      ROW** row;
      int* linkpos;

      row = col->rows;
      linkpos = col->linkpos;
      for( r = 0; r < col->len; ++r )
      {
         assert(row[r] != NULL);
         if( linkpos[r] >= 0 )
         {
            assert(row[r]->cols[linkpos[r]] == col);
            rowUpdatePseudoActivity(row[r], linkpos[r], oldbound, newbound);
         }
      }
   }
   else if( col->var->obj < 0.0 && boundtype == SCIP_BOUNDTYPE_UPPER )
   {
      ROW** row;
      int* linkpos;

      row = col->rows;
      linkpos = col->linkpos;
      for( r = 0; r < col->len; ++r )
      {
         assert(row[r] != NULL);
         if( linkpos[r] >= 0 )
         {
            assert(row[r]->cols[linkpos[r]] == col);
            rowUpdatePseudoActivity(row[r], linkpos[r], oldbound, newbound);
         }
      }
   }

   /* update activity bounds of rows */
   if( boundtype == SCIP_BOUNDTYPE_LOWER )
   {
      ROW** row;
      int* linkpos;

      row = col->rows;
      linkpos = col->linkpos;
      for( r = 0; r < col->len; ++r )
      {
         assert(row[r] != NULL);
         if( linkpos[r] >= 0 )
         {
            assert(row[r]->cols[linkpos[r]] == col);
            rowUpdateActivityBoundsLb(row[r], set, linkpos[r], oldbound, newbound);
         }
      }
   }
   else
   {
      ROW** row;
      int* linkpos;

      assert(boundtype == SCIP_BOUNDTYPE_UPPER);
      row = col->rows;
      linkpos = col->linkpos;
      for( r = 0; r < col->len; ++r )
      {
         assert(row[r] != NULL);
         if( linkpos[r] >= 0 )
         {
            assert(row[r]->cols[linkpos[r]] == col);
            rowUpdateActivityBoundsUb(row[r], set, linkpos[r], oldbound, newbound);
         }
      }
   }

   return SCIP_OKAY;
}

/** gets the primal LP solution of a column */
Real SCIPcolGetPrimsol(
   COL*             col                 /**< LP column */
   )
{
   assert(col != NULL);

   if( col->lppos >= 0 )
      return col->primsol;
   else
      return 0.0;
}

/** calculates the reduced costs of a column */
static
void colCalcRedcost(
   COL*             col                 /**< LP column */
   )
{
   ROW* row;
   int r;

   assert(col != NULL);
   assert(col->var->varstatus == SCIP_VARSTATUS_COLUMN);
   assert(col->var->data.col == col);

   col->redcost = col->var->obj;
   for( r = 0; r < col->len; ++r )
   {
      row = col->rows[r];
      assert(row->dualsol < SCIP_INVALID);
      col->redcost -= col->vals[r] * row->dualsol;
   }
}

/** gets the reduced costs of a column in last LP or after recalculation */
Real SCIPcolGetRedcost(
   COL*             col,                /**< LP column */
   STAT*            stat                /**< problem statistics */
   )
{
   assert(col != NULL);
   assert(col->validredcostlp <= stat->nlp);

   if( col->validredcostlp < stat->nlp )
      colCalcRedcost(col);
   assert(col->redcost < SCIP_INVALID);
   col->validredcostlp = stat->nlp;

   return col->redcost;
}

/** gets the feasibility of a column in last LP or after recalculation */
Real SCIPcolGetFeasibility(
   COL*             col,                /**< LP column */
   STAT*            stat                /**< problem statistics */
   )
{
   Real redcost;

   assert(col != NULL);
   assert(col->var != NULL);

   redcost = SCIPcolGetRedcost(col, stat);

   if( col->var->dom.lb < 0 )
      return -ABS(redcost);
   else
      return redcost;
}

/** calculates the farkas value of a column */
static
void colCalcFarkas(
   COL*             col                 /**< LP column */
   )
{
   ROW* row;
   int r;

   assert(col != NULL);
   assert(col->var->varstatus == SCIP_VARSTATUS_COLUMN);
   assert(col->var->data.col == col);

   col->farkas = 0.0;
   for( r = 0; r < col->len; ++r )
   {
      row = col->rows[r];
      assert(row->dualfarkas < SCIP_INVALID);
      col->farkas += col->vals[r] * row->dualfarkas;
   }
   if( col->farkas > 0.0 )
      col->farkas *= col->var->dom.ub;
   else
      col->farkas *= col->var->dom.lb;
}

/** gets the farkas value of a column in last LP (which must be infeasible) */
Real SCIPcolGetFarkas(
   COL*             col,                /**< LP column */
   STAT*            stat                /**< problem statistics */
   )
{
   assert(col != NULL);
   assert(col->validfarkaslp <= stat->nlp);

   if( col->validfarkaslp < stat->nlp )
      colCalcFarkas(col);
   assert(col->farkas < SCIP_INVALID);
   col->validfarkaslp = stat->nlp;

   return col->farkas;
}

/** gets strong branching information on a column variable */
RETCODE SCIPcolGetStrongbranch(
   COL*             col,                /**< LP column */
   STAT*            stat,               /**< problem statistics */
   LP*              lp,                 /**< actual LP data */
   Real             upperbound,         /**< actual global upper bound */
   int              itlim,              /**< iteration limit for strong branchings */
   Real*            down,               /**< stores dual bound after branching column down */
   Real*            up                  /**< stores dual bound after branching column up */
   )
{
   assert(col != NULL);
   assert(col->var != NULL);
   assert(col->var->data.col == col);
   assert(col->primsol < SCIP_INVALID);
   assert(col->lpipos >= 0);
   assert(col->lppos >= 0);
   assert(stat != NULL);
   assert(lp != NULL);
   assert(lp->solved);
   assert(col->lppos < lp->ncols);
   assert(lp->cols[col->lppos] == col);
   assert(itlim >= 1);
   assert(down != NULL);
   assert(up != NULL);

   if( itlim > col->strongitlim )
   {
      debugMessage("calling strong branching for variable <%s> with %d iterations\n", col->var->name, itlim);
      stat->nstrongbranch++;
      col->strongitlim = itlim;
      CHECK_OKAY( SCIPlpiStrongbranch(lp->lpi, &col->lpipos, 1, itlim, &col->strongdown, &col->strongup) );
      col->strongdown = MIN(col->strongdown, upperbound);
      col->strongup = MIN(col->strongup, upperbound);
   }
   assert(col->strongdown < SCIP_INVALID);
   assert(col->strongup < SCIP_INVALID);

   *down = col->strongdown;
   *up = col->strongup;

   return SCIP_OKAY;
}

/** gets variable this column represents */
VAR* SCIPcolGetVar(
   COL*             col                 /**< LP column */
   )
{
   assert(col != NULL);

   return col->var;
}

/** gets position of column in actual LP, or -1 if it is not in LP */
int SCIPcolGetLPPos(
   COL*             col                 /**< LP column */
   )
{
   assert(col != NULL);

   return col->lppos;
}

/** returns TRUE iff column is member of actual LP */
Bool SCIPcolIsInLP(
   COL*             col                 /**< LP column */
   )
{
   assert(col != NULL);

   return (col->lppos >= 0);
}

/** get number of nonzero entries in column vector */
int SCIPcolGetNNonz(
   COL*             col                 /**< LP column */
   )
{
   assert(col != NULL);

   return col->len;
}

/** gets array with rows of nonzero entries */
ROW** SCIPcolGetRows(
   COL*             col                 /**< LP column */
   )
{
   assert(col != NULL);

   return col->rows;
}

/** gets array with coefficients of nonzero entries */
Real* SCIPcolGetVals(
   COL*             col                 /**< LP column */
   )
{
   assert(col != NULL);

   return col->vals;
}

/** output column to file stream */
void SCIPcolPrint(
   COL*             col,                /**< LP column */
   const SET*       set,                /**< global SCIP settings */
   FILE*            file                /**< output file (or NULL for standard output) */
   )
{
   int r;

   assert(col != NULL);
   assert(col->var != NULL);

   if( file == NULL )
      file = stdout;

   /* print bounds */
   fprintf(file, "[%f,%f], ", col->var->dom.lb, col->var->dom.ub);

   /* print coefficients */
   if( col->len == 0 )
      fprintf(file, "<empty>");
   for( r = 0; r < col->len; ++r )
   {
      assert(col->rows[r] != NULL);
      assert(col->rows[r]->name != NULL);
      fprintf(file, "%+f%s ", col->vals[r], col->rows[r]->name);
   }
   fprintf(file, "\n");
}




/*
 * Row methods
 */

/** calculates row norms and min/maxidx from scratch, and checks for sortation */
static
void rowCalcNorms(
   ROW*             row,                /**< LP row */
   const SET*       set                 /**< global SCIP settings */
   )
{
   int i;
   int idx;

   assert(row != NULL);
   assert(set != NULL);

   row->sqrnorm = 0.0;
   row->maxval = 0.0;
   row->nummaxval = 1;
   row->minidx = INT_MAX;
   row->maxidx = INT_MIN;
   row->validminmaxidx = TRUE;
   row->sorted = TRUE;

   /* check, if row is sorted
    * calculate sqrnorm, maxval, minidx, and maxidx
    */
   for( i = 0; i < row->len; ++i )
   {
      assert(!SCIPsetIsZero(set, row->vals[i]));
      idx = row->cols[i]->index;
      rowAddNorms(row, set, idx, row->vals[i]);
      row->sorted &= (i == 0 || row->cols[i-1]->index < idx);
   }
}

/** creates and captures an LP row */
RETCODE SCIProwCreate(
   ROW**            row,                /**< pointer to LP row data */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   STAT*            stat,               /**< problem statistics */
   LP*              lp,                 /**< actual LP data */
   const char*      name,               /**< name of row */
   int              len,                /**< number of nonzeros in the row */
   COL**            col,                /**< array with columns of row entries */
   Real*            val,                /**< array with coefficients of row entries */
   Real             lhs,                /**< left hand side of row */
   Real             rhs,                /**< right hand side of row */
   Bool             local,              /**< is row only valid locally? */
   Bool             modifiable          /**< is row modifiable during node processing (subject to column generation)? */
   )
{
   assert(row != NULL);
   assert(memhdr != NULL);
   assert(stat != NULL);
   assert(len >= 0);
   assert(len == 0 || (col != NULL && val != NULL));
   assert(lhs <= rhs);

   ALLOC_OKAY( allocBlockMemory(memhdr, row) );

   if( len > 0 )
   {
      int i;

      ALLOC_OKAY( duplicateBlockMemoryArray(memhdr, &(*row)->cols, col, len) );
      ALLOC_OKAY( duplicateBlockMemoryArray(memhdr, &(*row)->vals, val, len) );
      ALLOC_OKAY( allocBlockMemoryArray(memhdr, &(*row)->linkpos, len) );
      for( i = 0; i < len; ++i )
         (*row)->linkpos[i] = -1;
   }
   else
   {
      (*row)->cols = NULL;
      (*row)->vals = NULL;
      (*row)->linkpos = NULL;
   }
   
   ALLOC_OKAY( duplicateBlockMemoryArray(memhdr, &(*row)->name, name, strlen(name)+1) );
   (*row)->constant = 0.0;
   (*row)->lhs = lhs;
   (*row)->rhs = rhs;
   (*row)->sqrnorm = 0.0;
   (*row)->maxval = 0.0;
   (*row)->dualsol = 0.0;
   (*row)->activity = SCIP_INVALID;
   (*row)->dualfarkas = 0.0;
   (*row)->pseudoactivity = SCIP_INVALID;
   (*row)->minactivity = SCIP_INVALID;
   (*row)->maxactivity = SCIP_INVALID;
   (*row)->minactivityinf = -1;
   (*row)->maxactivityinf = -1;
   (*row)->index = stat->nrowidx++;
   (*row)->size = len;
   (*row)->len = len;
   (*row)->nunlinked = len;
   (*row)->nuses = 0;
   (*row)->lppos = -1;
   (*row)->lpipos = -1;
   (*row)->minidx = INT_MAX;
   (*row)->maxidx = INT_MIN;
   (*row)->nummaxval = 0;
   (*row)->validactivitylp = -1;
   (*row)->sorted = FALSE;
   (*row)->validpsactivity = FALSE;
   (*row)->validactivitybds = FALSE;
   (*row)->validminmaxidx = FALSE;
   (*row)->lhschanged = FALSE;
   (*row)->rhschanged = FALSE;
   (*row)->coefchanged = FALSE;
   (*row)->local = local;
   (*row)->modifiable = modifiable;
   (*row)->nlocks = 0;

   /* calculate row norms and min/maxidx, and check if row is sorted */
   rowCalcNorms(*row, set);

   /* capture the row */
   SCIProwCapture(*row);

   return SCIP_OKAY;
}

/** frees an LP row */
RETCODE SCIProwFree(
   ROW**            row,                /**< pointer to LP row */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp                  /**< actual LP data */
   )
{
   assert(memhdr != NULL);
   assert(row != NULL);
   assert(*row != NULL);
   assert((*row)->nuses == 0);
   assert((*row)->lppos == -1);

   /* remove column indices from corresponding rows */
   CHECK_OKAY( rowUnlink(*row, memhdr, set, lp) );

   freeBlockMemoryArray(memhdr, &(*row)->name, strlen((*row)->name)+1);
   freeBlockMemoryArrayNull(memhdr, &(*row)->cols, (*row)->size);
   freeBlockMemoryArrayNull(memhdr, &(*row)->vals, (*row)->size);
   freeBlockMemoryArrayNull(memhdr, &(*row)->linkpos, (*row)->size);
   freeBlockMemory(memhdr, row);

   return SCIP_OKAY;
}

/** increases usage counter of LP row */
void SCIProwCapture(
   ROW*             row                 /**< LP row */
   )
{
   assert(row != NULL);
   assert(row->nuses >= 0);
   assert(row->nlocks <= row->nuses);

   debugMessage("capture row <%s> with nuses=%d and nlocks=%d\n", row->name, row->nuses, row->nlocks);
   row->nuses++;
}

/** decreases usage counter of LP row, and frees memory if necessary */
RETCODE SCIProwRelease(
   ROW**            row,                /**< pointer to LP row */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp                  /**< actual LP data */
   )
{
   assert(memhdr != NULL);
   assert(row != NULL);
   assert(*row != NULL);
   assert((*row)->nuses >= 1);
   assert((*row)->nlocks < (*row)->nuses);

   debugMessage("release row <%s> with nuses=%d and nlocks=%d\n", (*row)->name, (*row)->nuses, (*row)->nlocks);
   (*row)->nuses--;
   if( (*row)->nuses == 0 )
   {
      CHECK_OKAY( SCIProwFree(row, memhdr, set, lp) );
   }

   *row = NULL;

   return SCIP_OKAY;
}

/** locks an unmodifiable row, which forbids further changes */
RETCODE SCIProwLock(
   ROW*             row                 /**< LP row */
   )
{
   assert(row != NULL);

   debugMessage("lock row <%s> with nuses=%d and nlocks=%d\n", row->name, row->nuses, row->nlocks);

   /* check, if row is modifiable */
   if( row->modifiable )
   {
      char s[255];
      sprintf(s, "cannot lock the modifiable row <%s>", row->name);
      errorMessage(s);
      return SCIP_INVALIDDATA;
   }
   
   row->nlocks++;

   return SCIP_OKAY;
}

/** unlocks a lock of a row; a row with no sealed lock may be modified */
RETCODE SCIProwUnlock(
   ROW*             row                 /**< LP row */
   )
{
   assert(row != NULL);

   debugMessage("unlock row <%s> with nuses=%d and nlocks=%d\n", row->name, row->nuses, row->nlocks);

   /* check, if row is modifiable */
   if( row->modifiable )
   {
      char s[255];
      sprintf(s, "cannot unlock the modifiable row <%s>", row->name);
      errorMessage(s);
      return SCIP_INVALIDDATA;
   }
   
   /* check, if row is locked */
   if( row->nlocks == 0 )
   {
      char s[255];
      sprintf(s, "row <%s> has no sealed lock", row->name);
      errorMessage(s);
      return SCIP_INVALIDDATA;
   }

   row->nlocks--;

   return SCIP_OKAY;
}

/** forbids roundings of variables in row that may violate row */
void SCIProwForbidRounding(
   ROW*             row,                /**< LP row */
   const SET*       set                 /**< global SCIP settings */
   )
{
   COL** cols;
   Real* vals;
   Bool lhsexists;
   Bool rhsexists;
   int c;
   
   assert(row != NULL);
   assert(row->len == 0 || (row->cols != NULL && row->vals != NULL));
   assert(!SCIPsetIsInfinity(set, row->lhs));
   assert(!SCIPsetIsInfinity(set, -row->rhs));

   lhsexists = !SCIPsetIsInfinity(set, -row->lhs);
   rhsexists = !SCIPsetIsInfinity(set, row->rhs);
   cols = row->cols;
   vals = row->vals;

   for( c = 0; c < row->len; ++c )
   {
      assert(cols[c] != NULL);

      if( SCIPsetIsPositive(set, vals[c]) )
      {
         if( lhsexists )
            SCIPvarForbidRoundDown(cols[c]->var);
         if( rhsexists )
            SCIPvarForbidRoundUp(cols[c]->var);
      }
      else
      {
         assert(SCIPsetIsNegative(set, vals[c]));
         if( lhsexists )
            SCIPvarForbidRoundUp(cols[c]->var);
         if( rhsexists )
            SCIPvarForbidRoundDown(cols[c]->var);
      }
   }
}

/** allows roundings of variables in row that may violate row */
void SCIProwAllowRounding(
   ROW*             row,                /**< LP row */
   const SET*       set                 /**< global SCIP settings */
   )
{
   COL** cols;
   Real* vals;
   Bool lhsexists;
   Bool rhsexists;
   int c;
   
   assert(row != NULL);
   assert(row->len == 0 || (row->cols != NULL && row->vals != NULL));
   assert(!SCIPsetIsInfinity(set, row->lhs));
   assert(!SCIPsetIsInfinity(set, -row->rhs));

   lhsexists = !SCIPsetIsInfinity(set, -row->lhs);
   rhsexists = !SCIPsetIsInfinity(set, row->rhs);
   cols = row->cols;
   vals = row->vals;

   for( c = 0; c < row->len; ++c )
   {
      assert(cols[c] != NULL);

      if( SCIPsetIsPositive(set, vals[c]) )
      {
         if( lhsexists )
            SCIPvarAllowRoundDown(cols[c]->var);
         if( rhsexists )
            SCIPvarAllowRoundUp(cols[c]->var);
      }
      else
      {
         assert(SCIPsetIsNegative(set, vals[c]));
         if( lhsexists )
            SCIPvarAllowRoundUp(cols[c]->var);
         if( rhsexists )
            SCIPvarAllowRoundDown(cols[c]->var);
      }
   }
}

/** adds a previously non existing coefficient to an LP row */
RETCODE SCIProwAddCoeff(
   ROW*             row,                /**< LP row */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   COL*             col,                /**< LP column */
   Real             val                 /**< value of coefficient */
   )
{
   CHECK_OKAY( rowAddCoeff(row, memhdr, set, lp, col, val, -1, NULL) );

   checkLinks(lp);

   return SCIP_OKAY;
}

/** deletes coefficient from row */
RETCODE SCIProwDelCoeff(
   ROW*             row,                /**< row to be changed */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   COL*             col                 /**< coefficient to be deleted */
   )
{
   int pos;

   assert(row != NULL);
   assert(col != NULL);
   assert(col->var != NULL);

   /* search the position of the column in the row's col vector */
   pos = rowSearchCoeff(row, col);
   if( pos == -1 )
   {
      char s[255];
      sprintf(s, "coefficient for column <%s> doesn't exist in row <%s>", col->var->name, row->name);
      errorMessage(s);
      return SCIP_INVALIDDATA;
   }
   assert(0 <= pos && pos < row->len);
   assert(row->cols[pos] == col);

   checkLinks(lp);

   /* if column knows of the row, remove the row from the column's row vector */
   if( row->linkpos[pos] != -1 )
   {
      assert(col->rows[row->linkpos[pos]] == row);
      assert(SCIPsetIsEQ(set, col->vals[row->linkpos[pos]], row->vals[pos]));
      CHECK_OKAY( colDelCoeffPos(col, set, lp, row->linkpos[pos]) );
   }

   /* delete the column from the row's col vector */
   CHECK_OKAY( rowDelCoeffPos(row, set, lp, pos) );
   
   checkLinks(lp);

   return SCIP_OKAY;
}

/** changes or adds a coefficient to an LP row */
RETCODE SCIProwChgCoeff(
   ROW*             row,                /**< LP row */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   COL*             col,                /**< LP column */
   Real             val                 /**< value of coefficient */
   )
{
   int pos;

   assert(col != NULL);
   assert(row != NULL);

   /* search the position of the column in the row's col vector */
   pos = rowSearchCoeff(row, col);

   checkLinks(lp);

   /* check, if column already exists in the row's col vector */
   if( pos == -1 )
   {
      /* add previously not existing coefficient */
      CHECK_OKAY( rowAddCoeff(row, memhdr, set, lp, col, val, -1, NULL) );
   }
   else
   {
      /* modifify already existing coefficient */
      assert(0 <= pos && pos < row->len);
      assert(row->cols[pos] == col);

      /* if column knows of the row, change the corresponding coefficient in the column */
      if( row->linkpos[pos] != -1 )
      {
         assert(col->rows[row->linkpos[pos]] == row);
         assert(SCIPsetIsEQ(set, col->vals[row->linkpos[pos]], row->vals[pos]));
         CHECK_OKAY( colChgCoeffPos(col, memhdr, set, lp, row->linkpos[pos], val) );
      }

      /* change the coefficient in the row */
      CHECK_OKAY( rowChgCoeffPos(row, memhdr, set, lp, pos, val) );
   }

   checkLinks(lp);

   return SCIP_OKAY;
}

/** increases value of an existing or nonexisting coefficient in an LP row */
RETCODE SCIProwIncCoeff(
   ROW*             row,                /**< LP row */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   COL*             col,                /**< LP column */
   Real             incval              /**< value to add to the coefficient */
   )
{
   int pos;

   assert(col != NULL);
   assert(row != NULL);

   if( SCIPsetIsZero(set, incval) )
      return SCIP_OKAY;

   /* search the position of the column in the row's col vector */
   pos = rowSearchCoeff(row, col);

   checkLinks(lp);

   /* check, if column already exists in the row's col vector */
   if( pos == -1 )
   {
      /* add previously not existing coefficient */
      CHECK_OKAY( rowAddCoeff(row, memhdr, set, lp, col, incval, -1, NULL) );
   }
   else
   {
      /* modifify already existing coefficient */
      assert(0 <= pos && pos < row->len);
      assert(row->cols[pos] == col);

      /* if column knows of the row, change the corresponding coefficient in the column */
      if( row->linkpos[pos] != -1 )
      {
         assert(col->rows[row->linkpos[pos]] == row);
         assert(SCIPsetIsEQ(set, col->vals[row->linkpos[pos]], row->vals[pos]));
         CHECK_OKAY( colChgCoeffPos(col, memhdr, set, lp, row->linkpos[pos], row->vals[pos] + incval) );
      }

      /* change the coefficient in the row */
      CHECK_OKAY( rowChgCoeffPos(row, memhdr, set, lp, pos, row->vals[pos] + incval) );
   }

   checkLinks(lp);

   return SCIP_OKAY;
}

/** add constant value to a row, i.e. subtract value from lhs and rhs */
RETCODE SCIProwAddConst(
   ROW*             row,                /**< LP row */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   Real             constant            /**< constant value to add to the row */
   )
{
   assert(row != NULL);
   assert(row->lhs <= row->rhs);
   assert(!SCIPsetIsInfinity(set, ABS(constant)));

   if( !SCIPsetIsZero(set, constant) )
   {
      Real oldconstant;

      oldconstant = row->constant;
      row->constant += constant;

      rowUpdatePseudoActivityConstant(row, oldconstant, row->constant);
      rowUpdateActivityBoundsConstant(row, oldconstant, row->constant);

      if( !SCIPsetIsInfinity(set, -row->lhs) )
      {
         CHECK_OKAY( rowSideChanged(row, set, lp, SCIP_SIDETYPE_LEFT) );
      }
      if( !SCIPsetIsInfinity(set, row->rhs) )
      {
         CHECK_OKAY( rowSideChanged(row, set, lp, SCIP_SIDETYPE_RIGHT) );
      }
   }

   return SCIP_OKAY;
}

/** sorts row entries by column index */
void SCIProwSort(
   ROW*             row                 /**< row to be sorted */
   )
{
   if( !row->sorted )
   {
      int i;

      /* sort coefficients */
      SCIPbsortPtrDblInt((void**)(row->cols), row->vals, row->linkpos, row->len, &cmpCol);

      /* update links */
      for( i = 0; i < row->len; ++i )
      {
         if( row->linkpos[i] != -1 )
         {
            assert(row->cols[i]->rows[row->linkpos[i]] == row);
            assert(row->cols[i]->linkpos[row->linkpos[i]] != -1);
            row->cols[i]->linkpos[row->linkpos[i]] = i;
         }
      }

      row->sorted = TRUE;
   }
}

/** recalculates the actual activity of a row */
static
void rowCalcActivity(
   ROW*             row                 /**< LP row */
   )
{
   COL* col;
   int c;

   assert(row != NULL);

   row->activity = row->constant;
   for( c = 0; c < row->len; ++c )
   {
      col = row->cols[c];
      assert(col->primsol < SCIP_INVALID);
      row->activity += row->vals[c] * col->primsol;
   }
}

/** returns the activity of a row in the last LP or after recalculation */
Real SCIProwGetActivity(
   ROW*             row,                /**< LP row */
   STAT*            stat                /**< problem statistics */
   )
{
   assert(row != NULL);
   assert(row->validactivitylp <= stat->nlp);

   if( row->validactivitylp != stat->nlp )
      rowCalcActivity(row);
   assert(row->activity < SCIP_INVALID);
   row->validactivitylp = stat->nlp;

   return row->activity;
}

/** returns the feasibility of a row in the last solution or after recalc */
Real SCIProwGetFeasibility(
   ROW*             row,                /**< LP row */
   STAT*            stat                /**< problem statistics */
   )
{
   Real activity;

   assert(row != NULL);

   activity = SCIProwGetActivity(row, stat);

   return MIN(row->rhs - activity, activity - row->lhs);
}

/** calculates the actual pseudo activity of a row */
static
RETCODE rowCalcPseudoActivity(
   ROW*             row,                /**< row data */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp                  /**< actual LP data */
   )
{
   int i;

   assert(row != NULL);
   assert(!row->validpsactivity);
   assert(row->pseudoactivity >= SCIP_INVALID);

   /* link row to the columns, such that a column bound change affects the now valid pseudo activity */
   CHECK_OKAY( rowLink(row, memhdr, set, lp) );
   
   /* calculate activity bounds */
   row->validpsactivity = TRUE;
   row->pseudoactivity = row->constant;
   for( i = 0; i < row->len; ++i )
   {
      assert(row->cols[i] != NULL);
      assert(row->cols[i]->var != NULL);
      rowUpdatePseudoActivity(row, i, 0.0, SCIPvarGetPseudoSol(row->cols[i]->var));
   }

   return SCIP_OKAY;
}

/** returns the pseudo activity of a row */
RETCODE SCIProwGetPseudoActivity(
   ROW*             row,                /**< LP row */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   Real*            pseudoactivity      /**< pointer to store the pseudo activity */
   )
{
   assert(row != NULL);
   assert(pseudoactivity != NULL);

   /* check, if activity bounds has to be calculated */
   if( !row->validpsactivity )
   {
      assert(row->pseudoactivity >= SCIP_INVALID);
      CHECK_OKAY( rowCalcPseudoActivity(row, memhdr, set, lp) );
   }
#ifdef DEBUG
   else
   {
      Real oldpseudoactivity;

      /* test, if updating of pseudo activity was correct */
      oldpseudoactivity = row->pseudoactivity;
      row->validpsactivity = FALSE;
      row->pseudoactivity = SCIP_INVALID;
      CHECK_OKAY( rowCalcPseudoActivity(row, memhdr, set, lp) );
      assert(SCIPsetIsSumEQ(set, row->pseudoactivity, oldpseudoactivity));
      row->pseudoactivity = oldpseudoactivity;
   }
#endif

   assert(row->validpsactivity);
   assert(row->pseudoactivity < SCIP_INVALID);

   *pseudoactivity = row->pseudoactivity;

   return SCIP_OKAY;
}

/** returns the feasibility of a row in the actual pseudo solution */
RETCODE SCIProwGetPseudoFeasibility(
   ROW*             row,                /**< LP row */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   Real*            pseudofeasibility   /**< pointer to store the pseudo feasibility */
   )
{
   Real pseudoactivity;

   assert(row != NULL);
   assert(pseudofeasibility != NULL);

   CHECK_OKAY( SCIProwGetPseudoActivity(row, memhdr, set, lp, &pseudoactivity) );

   *pseudofeasibility = MIN(row->rhs - pseudoactivity, pseudoactivity - row->lhs);

   return SCIP_OKAY;
}

/** returns the activity of a row for a given solution */
RETCODE SCIProwGetSolActivity(
   ROW*             row,                /**< LP row */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   STAT*            stat,               /**< problem statistics data */
   SOL*             sol,                /**< primal CIP solution */
   Real*            solactivity         /**< pointer to store the row's activity for the solution */
   )
{
   Real solval;
   int i;

   assert(row != NULL);
   assert(solactivity != NULL);

   *solactivity = row->constant;
   for( i = 0; i < row->len; ++i )
   {
      assert(row->cols[i] != NULL);
      CHECK_OKAY( SCIPsolGetVal(sol, memhdr, set, stat, row->cols[i]->var, &solval) );
      *solactivity += row->vals[i] * solval;
   }

   return SCIP_OKAY;
}

/** returns the feasibility of a row for the given solution */
RETCODE SCIProwGetSolFeasibility(
   ROW*             row,                /**< LP row */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   STAT*            stat,               /**< problem statistics data */
   SOL*             sol,                /**< primal CIP solution */
   Real*            solfeasibility      /**< pointer to store the row's feasibility for the solution */
   )
{
   Real solactivity;

   assert(row != NULL);
   assert(solfeasibility != NULL);

   CHECK_OKAY( SCIProwGetSolActivity(row, memhdr, set, stat, sol, &solactivity) );

   *solfeasibility = MIN(row->rhs - solactivity, solactivity - row->lhs);

   return SCIP_OKAY;
}

/** calculates minimal and maximal activity of row w.r.t. the column's bounds */
static
RETCODE rowCalcActivityBounds(
   ROW*             row,                /**< row data */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp                  /**< actual LP data */
   )
{
   int i;
   
   assert(row != NULL);
   assert(!SCIPsetIsInfinity(set, ABS(row->constant)));
   
   /* link row to the columns, such that a column bound change affects the now valid activity bounds */
   CHECK_OKAY( rowLink(row, memhdr, set, lp) );
   
   /* calculate activity bounds */
   row->validactivitybds = TRUE;
   row->minactivity = row->constant;
   row->maxactivity = row->constant;
   row->minactivityinf = 0;
   row->maxactivityinf = 0;
   for( i = 0; i < row->len; ++i )
   {
      assert(row->cols[i] != NULL);
      assert(row->cols[i]->var != NULL);
      rowUpdateActivityBoundsLb(row, set, i, 0.0, row->cols[i]->var->dom.lb);
      rowUpdateActivityBoundsUb(row, set, i, 0.0, row->cols[i]->var->dom.ub);      
   }

   return SCIP_OKAY;
}

/** returns the minimal and maximal activity of a row w.r.t. the column's bounds */
RETCODE SCIProwGetActivityBounds(
   ROW*             row,                /**< LP row */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   Real*            minactivity,        /**< pointer to store the minimal activity, or NULL */
   Real*            maxactivity         /**< pointer to store the maximal activity, or NULL */
   )
{
   assert(row != NULL);

   /* check, if activity bounds has to be calculated */
   if( !row->validactivitybds )
   {
      assert(row->minactivity >= SCIP_INVALID);
      assert(row->maxactivity >= SCIP_INVALID);
      assert(row->minactivityinf == -1);
      assert(row->maxactivityinf == -1);      
      CHECK_OKAY( rowCalcActivityBounds(row, memhdr, set, lp) );
   }
#ifdef DEBUG
   else
   {
      Real oldminactivity;
      Real oldmaxactivity;
      int oldminactivityinf;
      int oldmaxactivityinf;

      /* test, if updating of activity bounds was correct */
      oldminactivity = row->minactivity;
      oldmaxactivity = row->maxactivity;
      oldminactivityinf = row->minactivityinf;
      oldmaxactivityinf = row->maxactivityinf;
      CHECK_OKAY( rowCalcActivityBounds(row, memhdr, set, lp) );
      assert(SCIPsetIsSumEQ(set, row->minactivity, oldminactivity));
      assert(SCIPsetIsSumEQ(set, row->maxactivity, oldmaxactivity));
      assert(row->minactivityinf == oldminactivityinf);
      assert(row->maxactivityinf == oldmaxactivityinf);
      row->minactivity = oldminactivity;
      row->maxactivity = oldmaxactivity;
   }
#endif

   assert(row->validactivitybds);
   assert(row->minactivity < SCIP_INVALID);
   assert(row->maxactivity < SCIP_INVALID);

   if( minactivity != NULL )
   {
      if( row->minactivityinf > 0 )
         *minactivity = -set->infinity;
      else
         *minactivity = row->minactivity;
   }
   if( maxactivity != NULL )
   {
      if( row->maxactivityinf > 0 )
         *maxactivity = set->infinity;
      else
         *maxactivity = row->maxactivity;
   }

   return SCIP_OKAY;
}
   
/** gets activity bounds for row after setting variable to zero */
RETCODE SCIProwGetActivityResiduals(
   ROW*             row,                /**< LP row */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   VAR*             var,                /**< variable to calculate activity residual for */
   Real             val,                /**< coefficient value of variable in linear constraint */
   Real*            minresactivity,     /**< pointer to store the minimal residual activity */
   Real*            maxresactivity      /**< pointer to store the maximal residual activity */
   )
{
   Real lb;
   Real ub;

   assert(row != NULL);
   assert(var != NULL);
   assert(minresactivity != NULL);
   assert(maxresactivity != NULL);

   if( !row->validactivitybds )
      rowCalcActivityBounds(row, memhdr, set, lp);
   assert(row->minactivity < SCIP_INVALID);
   assert(row->maxactivity < SCIP_INVALID);
   
   lb = SCIPvarGetLb(var);
   ub = SCIPvarGetUb(var);
   assert(!SCIPsetIsInfinity(set, lb));
   assert(!SCIPsetIsInfinity(set, -ub));
   
   if( val > 0.0 )
   {
      if( SCIPsetIsInfinity(set, -lb) )
      {
         assert(row->minactivityinf >= 1);
         if( row->minactivityinf >= 2 )
            *minresactivity = -set->infinity;
         else
            *minresactivity = row->minactivity;
      }
      else
      {
         if( row->minactivityinf >= 1 )
            *minresactivity = -set->infinity;
         else
            *minresactivity = row->minactivity - val * lb;
      }
      if( SCIPsetIsInfinity(set, ub) )
      {
         assert(row->maxactivityinf >= 1);
         if( row->maxactivityinf >= 2 )
            *maxresactivity = +set->infinity;
         else
            *maxresactivity = row->maxactivity;
      }
      else
      {
         if( row->maxactivityinf >= 1 )
            *maxresactivity = +set->infinity;
         else
            *maxresactivity = row->maxactivity - val * ub;
      }
   }
   else
   {
      if( SCIPsetIsInfinity(set, ub) )
      {
         assert(row->minactivityinf >= 1);
         if( row->minactivityinf >= 2 )
            *minresactivity = -set->infinity;
         else
            *minresactivity = row->minactivity;
      }
      else
      {
         if( row->minactivityinf >= 1 )
            *minresactivity = -set->infinity;
         else
            *minresactivity = row->minactivity - val * ub;
      }
      if( SCIPsetIsInfinity(set, -lb) )
      {
         assert(row->maxactivityinf >= 1);
         if( row->maxactivityinf >= 2 )
            *maxresactivity = +set->infinity;
         else
            *maxresactivity = row->maxactivity;
      }
      else
      {
         if( row->maxactivityinf >= 1 )
            *maxresactivity = +set->infinity;
         else
            *maxresactivity = row->maxactivity - val * lb;
      }
   }

   return SCIP_OKAY;
}

/** invalidates activity bounds, such that they are recalculated in next get */
RETCODE SCIProwInvalidActivityBounds(
   ROW*             row                 /**< LP row */
   )
{
   assert(row != NULL);

   row->validactivitybds = FALSE;
   row->minactivity = SCIP_INVALID;
   row->maxactivity = SCIP_INVALID;
   row->minactivityinf = -1;
   row->maxactivityinf = -1;

   return SCIP_OKAY;
}

/** get number of nonzero entries in row vector */
int SCIProwGetNNonz(
   ROW*             row                 /**< LP row */
   )
{
   assert(row != NULL);

   return row->len;
}

/** gets array with columns of nonzero entries */
COL** SCIProwGetCols(
   ROW*             row                 /**< LP row */
   )
{
   assert(row != NULL);

   return row->cols;
}

/** gets array with coefficients of nonzero entries */
Real* SCIProwGetVals(
   ROW*             row                 /**< LP row */
   )
{
   assert(row != NULL);

   return row->vals;
}

/** gets constant shift of row */
Real SCIProwGetConstant(
   ROW*             row                 /**< LP row */
   )
{
   assert(row != NULL);

   return row->constant;
}

/** get euclidean norm of row vector */
Real SCIProwGetNorm(
   ROW*             row                 /**< LP row */
   )
{
   assert(row != NULL);

   return sqrt(row->sqrnorm);
}

/** gets maximal absolute value of row vector coefficients */
Real SCIProwGetMaxval(
   ROW*             row,                /**< LP row */
   const SET*       set                 /**< global SCIP settings */
   )
{
   assert(row != NULL);
   
   if( row->nummaxval == 0 )
      rowCalcNorms(row, set);
   assert(row->nummaxval > 0);
   assert(row->maxval >= 0.0);

   return row->maxval;
}

/** changes left hand side of LP row */
RETCODE SCIProwChgLhs(
   ROW*             row,                /**< LP row */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   Real             lhs                 /**< new left hand side */
   )
{
   assert(row != NULL);

   if( !SCIPsetIsEQ(set, row->lhs, lhs) )
   {
      row->lhs = lhs;
      CHECK_OKAY( rowSideChanged(row, set, lp, SCIP_SIDETYPE_LEFT) );
   }

   return SCIP_OKAY;
}

/** changes right hand side of LP row */
RETCODE SCIProwChgRhs(
   ROW*             row,                /**< LP row */
   const SET*       set,                /**< global SCIP settings */
   LP*              lp,                 /**< actual LP data */
   Real             rhs                 /**< new right hand side */
   )
{
   assert(row != NULL);

   if( !SCIPsetIsEQ(set, row->rhs, rhs) )
   {
      row->rhs = rhs;
      CHECK_OKAY( rowSideChanged(row, set, lp, SCIP_SIDETYPE_RIGHT) );
   }

   return SCIP_OKAY;
}

/** returns the left hand side of the row */
Real SCIProwGetLhs(
   ROW*             row                 /**< LP row */
   )
{
   assert(row != NULL);

   return row->lhs;
}

/** returns the right hand side of the row */
Real SCIProwGetRhs(
   ROW*             row                 /**< LP row */
   )
{
   assert(row != NULL);

   return row->rhs;
}

/** returns the name of the row */
const char* SCIProwGetName(
   ROW*             row                 /**< LP row */
   )
{
   assert(row != NULL);

   return row->name;
}

/** gets unique index of row */
int SCIProwGetIndex(
   ROW*             row                 /**< LP row */
   )
{
   assert(row != NULL);

   return row->index;
}

/** returns TRUE iff row is only valid locally */
Bool SCIProwIsLocal(
   ROW*             row                 /**< LP row */
   )
{
   assert(row != NULL);

   return row->local;
}

/** gets position of row in actual LP, or -1 if it is not in LP */
int SCIProwGetLPPos(
   ROW*             row                 /**< LP row */
   )
{
   assert(row != NULL);

   return row->lppos;
}

/** returns TRUE iff row is member of actual LP */
Bool SCIProwIsInLP(
   ROW*             row                 /**< LP row */
   )
{
   assert(row != NULL);

   return (row->lppos >= 0);
}

/** output row to file stream */
void SCIProwPrint(
   ROW*             row,                /**< LP row */
   FILE*            file                /**< output file (or NULL for standard output) */
   )
{
   int c;

   assert(row != NULL);

   if( file == NULL )
      file = stdout;

   /* print left hand side */
   fprintf(file, "%+f <= ", row->lhs);

   /* print coefficients */
   if( row->len == 0 )
      fprintf(file, "0 ");
   for( c = 0; c < row->len; ++c )
   {
      assert(row->cols[c] != NULL);
      assert(row->cols[c]->var != NULL);
      assert(row->cols[c]->var->name != NULL);
      assert(row->cols[c]->var->varstatus == SCIP_VARSTATUS_COLUMN);
      fprintf(file, "%+f%s ", row->vals[c], row->cols[c]->var->name);
   }

   /* print right hand side */
   fprintf(file, "<= %+f\n", row->rhs);
}



/*
 * LP solver data update
 */

/** applies all cached column removals to the LP solver */
static
RETCODE lpFlushDelCols(
   LP*              lp                  /**< actual LP data */
   )
{
   assert(lp != NULL);
   assert(lp->lpifirstchgcol <= lp->nlpicols);
   assert(lp->lpifirstchgcol <= lp->ncols);

   /* find the first column to change */
   while( lp->lpifirstchgcol < lp->nlpicols
      && lp->lpifirstchgcol < lp->ncols
      && lp->cols[lp->lpifirstchgcol]->lpipos == lp->lpifirstchgcol
      && !lp->cols[lp->lpifirstchgcol]->coefchanged )
   {
      assert(lp->cols[lp->lpifirstchgcol] == lp->lpicols[lp->lpifirstchgcol]);
      lp->lpifirstchgcol++;
   }
   
   /* shrink LP to the part which didn't change */
   if( lp->lpifirstchgcol < lp->nlpicols )
   {
      int i;

      debugMessage("flushing col deletions: shrink LP from %d to %d colums\n", lp->nlpicols, lp->lpifirstchgcol);
      CHECK_OKAY( SCIPlpiDelCols(lp->lpi, lp->lpifirstchgcol, lp->nlpicols-1) );
      for( i = lp->lpifirstchgcol; i < lp->nlpicols; ++i )
      {
         lp->lpicols[i]->lpipos = -1;
         lp->lpicols[i]->primsol = 0.0;
         lp->lpicols[i]->redcost = SCIP_INVALID;
         lp->lpicols[i]->farkas = SCIP_INVALID;
         lp->lpicols[i]->strongdown = SCIP_INVALID;
         lp->lpicols[i]->strongup = SCIP_INVALID;
         lp->lpicols[i]->validredcostlp = -1;
         lp->lpicols[i]->validfarkaslp = -1;
         lp->lpicols[i]->strongitlim = -1;
      }
      lp->nlpicols = lp->lpifirstchgcol;
   }
   assert(lp->nlpicols == lp->lpifirstchgcol);

   return SCIP_OKAY;
}

/** applies all cached column additions to the LP solver */
static
RETCODE lpFlushAddCols(
   LP*              lp,                 /**< actual LP data */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set                 /**< global SCIP settings */
   )
{
   Real* obj;
   Real* lb;
   Real* ub;
   int* beg;
   int* ind;
   Real* val;
   char** name;
   COL* col;
   const VAR* var;
   Real infinity;
   int c;
   int pos;
   int nnonz;
   int naddcols;
   int naddcoefs;
   int i;
   int lpipos;

   assert(lp != NULL);
   assert(lp->lpifirstchgcol == lp->nlpicols);
   assert(memhdr != NULL);
   assert(set != NULL);

   /* if there are no columns to add, we are ready */
   if( lp->ncols == lp->nlpicols )
      return SCIP_OKAY;

   /* add the additional columns */
   assert(lp->ncols > lp->nlpicols);
   CHECK_OKAY( ensureLpicolsSize(lp, set, lp->ncols) );

   /* get the solver's infinity value */
   infinity = SCIPlpiInfinity(lp->lpi);

   /* count the (maximal) number of added coefficients, calculate the number of added columns */
   naddcols = lp->ncols - lp->nlpicols;
   naddcoefs = 0;
   for( c = lp->nlpicols; c < lp->ncols; ++c )
      naddcoefs += lp->cols[c]->len;
   assert(naddcols > 0);

   /* get temporary memory for changes */
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &obj, naddcols) );
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &lb, naddcols) );
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &ub, naddcols) );
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &beg, naddcols) );
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &ind, naddcoefs) );
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &val, naddcoefs) );
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &name, naddcols) );
   
   /* fill temporary memory with column data */
   nnonz = 0;
   for( pos = 0, c = lp->nlpicols; c < lp->ncols; ++pos, ++c )
   {
      col = lp->cols[c];
      assert(col != NULL);
      assert(col->var != NULL);
      assert(col->var->varstatus == SCIP_VARSTATUS_COLUMN);
      assert(col->var->data.col == col);
      assert(col->lppos == c);
      assert(nnonz + col->len <= naddcoefs);

      debugMessage("flushing added column <%s>:", col->var->name);
      debug( SCIPcolPrint(col, set, NULL) );

      /* Because the column becomes a member of the LP solver, it now can take values
       * different from zero. That means, we have to include the column in the corresponding
       * row vectors.
       */
      CHECK_OKAY( colLink(col, memhdr, set, lp) );

      var = col->var;
      assert(var != NULL);
      assert(var->varstatus == SCIP_VARSTATUS_COLUMN);
      assert(var->data.col == col);

      lp->lpicols[c] = col;
      col->lpipos = c;
      col->primsol = SCIP_INVALID;
      col->redcost = SCIP_INVALID;
      col->farkas = SCIP_INVALID;
      col->strongdown = SCIP_INVALID;
      col->strongup = SCIP_INVALID;
      col->validredcostlp = -1;
      col->validfarkaslp = -1;
      col->strongitlim = -1;
      col->lbchanged = FALSE;
      col->ubchanged = FALSE;
      col->coefchanged = FALSE;
      obj[pos] = var->obj;
      if( SCIPsetIsInfinity(set, -var->dom.lb) )
         lb[pos] = -infinity;
      else
         lb[pos] = var->dom.lb;
      if( SCIPsetIsInfinity(set, var->dom.ub) )
         ub[pos] = infinity;
      else
         ub[pos] = var->dom.ub;
      beg[pos] = nnonz;
      name[pos] = var->name;

      for( i = 0; i < col->len; ++i )
      {
         lpipos = col->rows[i]->lpipos;
         if( lpipos >= 0 )
         {
            assert(lpipos < lp->nrows);
            ind[nnonz] = lpipos;
            val[nnonz] = col->vals[i];
            nnonz++;
         }
      }
   }

   /* call LP interface */
   debugMessage("flushing col additions: enlarge LP from %d to %d colums\n", lp->nlpicols, lp->ncols);
   CHECK_OKAY( SCIPlpiAddCols(lp->lpi, naddcols, obj, lb, ub, nnonz, beg, ind, val, name) );
   lp->nlpicols = lp->ncols;
   lp->lpifirstchgcol = lp->nlpicols;

   /* free temporary memory */
   SCIPsetReleaseBufferArray(set, &name);
   SCIPsetReleaseBufferArray(set, &val);
   SCIPsetReleaseBufferArray(set, &ind);
   SCIPsetReleaseBufferArray(set, &beg);
   SCIPsetReleaseBufferArray(set, &ub);
   SCIPsetReleaseBufferArray(set, &lb);
   SCIPsetReleaseBufferArray(set, &obj);

   return SCIP_OKAY;
}

/** applies all cached row removals to the LP solver */
static
RETCODE lpFlushDelRows(
   LP*              lp                  /**< actual LP data */
   )
{
   assert(lp != NULL);
   assert(lp->lpifirstchgrow <= lp->nlpirows);
   assert(lp->lpifirstchgrow <= lp->nrows);
      
   /* find the first row to change */
   while( lp->lpifirstchgrow < lp->nlpirows
      && lp->lpifirstchgrow < lp->nrows
      && lp->rows[lp->lpifirstchgrow]->lpipos == lp->lpifirstchgrow
      && !lp->rows[lp->lpifirstchgrow]->coefchanged )
   {
      assert(lp->rows[lp->lpifirstchgrow] == lp->lpirows[lp->lpifirstchgrow]);
      lp->lpifirstchgrow++;
   }
   
   /* shrink LP to the part which didn't change */
   if( lp->lpifirstchgrow < lp->nlpirows )
   {
      int i;

      debugMessage("flushing row deletions: shrink LP from %d to %d rows\n", lp->nlpirows, lp->lpifirstchgrow);
      CHECK_OKAY( SCIPlpiDelRows(lp->lpi, lp->lpifirstchgrow, lp->nlpirows-1) );
      for( i = lp->lpifirstchgrow; i < lp->nlpirows; ++i )
      {
         lp->lpirows[i]->lpipos = -1;
         lp->lpirows[i]->dualsol = 0.0;
         lp->lpirows[i]->activity = SCIP_INVALID;
         lp->lpirows[i]->dualfarkas = 0.0;
         lp->lpirows[i]->validactivitylp = -1;
      }
      lp->nlpirows = lp->lpifirstchgrow;
   }
   assert(lp->nlpirows == lp->lpifirstchgrow);

   return SCIP_OKAY;
}

/** applies all cached row additions and removals to the LP solver */
static
RETCODE lpFlushAddRows(
   LP*              lp,                 /**< actual LP data */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set                 /**< global SCIP settings */
   )
{
   Real* lhs;
   Real* rhs;
   int* beg;
   int* ind;
   Real* val;
   char** name;
   ROW* row;
   Real infinity;
   int r;
   int pos;
   int nnonz;
   int naddrows;
   int naddcoefs;
   int i;
   int lpipos;

   assert(lp != NULL);
   assert(lp->lpifirstchgrow == lp->nlpirows);
   assert(memhdr != NULL);
      
   /* if there are no rows to add, we are ready */
   if( lp->nrows == lp->nlpirows )
      return SCIP_OKAY;

   /* add the additional rows */
   assert(lp->nrows > lp->nlpirows);
   CHECK_OKAY( ensureLpirowsSize(lp, set, lp->nrows) );

   /* get the solver's infinity value */
   infinity = SCIPlpiInfinity(lp->lpi);

   /* count the (maximal) number of added coefficients, calculate the number of added rows */
   naddrows = lp->nrows - lp->nlpirows;
   naddcoefs = 0;
   for( r = lp->nlpirows; r < lp->nrows; ++r )
      naddcoefs += lp->rows[r]->len;
   assert(naddrows > 0);

   /* get temporary memory for changes */
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &lhs, naddrows) );
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &rhs, naddrows) );
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &beg, naddrows) );
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &ind, naddcoefs) );
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &val, naddcoefs) );
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &name, naddrows) );
   
   /* fill temporary memory with row data */
   nnonz = 0;
   for( pos = 0, r = lp->nlpirows; r < lp->nrows; ++pos, ++r )
   {
      row = lp->rows[r];
      assert(row != NULL);
      assert(row->lppos == r);
      assert(nnonz + row->len <= naddcoefs);

      debugMessage("flushing added row:");
      debug( SCIProwPrint(row, NULL) );

      /* Because the row becomes a member of the LP solver, its dual variable now can take values
       * different from zero. That means, we have to include the row in the corresponding
       * column vectors.
       */
      CHECK_OKAY( rowLink(row, memhdr, set, lp) );

      lp->lpirows[r] = row;
      row->lpipos = r;
      row->dualsol = SCIP_INVALID;
      row->activity = SCIP_INVALID;
      row->dualfarkas = SCIP_INVALID;
      row->validactivitylp = -1;
      row->lhschanged = FALSE;
      row->rhschanged = FALSE;
      row->coefchanged = FALSE;
      if( SCIPsetIsInfinity(set, -row->lhs) )
         lhs[pos] = -infinity;
      else
         lhs[pos] = row->lhs + row->constant;
      if( SCIPsetIsInfinity(set, row->rhs) )
         rhs[pos] = infinity;
      else
         rhs[pos] = row->rhs + row->constant;
      beg[pos] = nnonz;
      name[pos] = row->name;

      debugMessage("flushing added row (LPI): %+g <=", lhs[pos]);
      for( i = 0; i < row->len; ++i )
      {
         lpipos = row->cols[i]->lpipos;
         debug( printf(" %+gx%d(<%s>)", row->vals[i], lpipos+1, row->cols[i]->var->name) );
         if( lpipos >= 0 )
         {
            assert(lpipos < lp->ncols);
            ind[nnonz] = lpipos;
            val[nnonz] = row->vals[i];
            nnonz++;
         }
      }
      debug( printf(" <= %+g\n", rhs[pos]) );
   }

   /* call LP interface */
   debugMessage("flushing row additions: enlarge LP from %d to %d rows\n", lp->nlpirows, lp->nrows);
   CHECK_OKAY( SCIPlpiAddRows(lp->lpi, naddrows, lhs, rhs, nnonz, beg, ind, val, name) );
   lp->nlpirows = lp->nrows;
   lp->lpifirstchgrow = lp->nlpirows;

   /* free temporary memory */
   SCIPsetReleaseBufferArray(set, &name);
   SCIPsetReleaseBufferArray(set, &val);
   SCIPsetReleaseBufferArray(set, &ind);
   SCIPsetReleaseBufferArray(set, &beg);
   SCIPsetReleaseBufferArray(set, &rhs);
   SCIPsetReleaseBufferArray(set, &lhs);
   
   return SCIP_OKAY;
}

/** applies all cached column changes to the LP */
static
RETCODE lpFlushChgCols(
   LP*              lp,                 /**< actual LP data */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set                 /**< global SCIP settings */
   )
{
   COL* col;
   const VAR* var;
   int* ind;
   Real* lb;
   Real* ub;
   Real infinity;
   int i;
   int nchg;

   assert(lp != NULL);
   assert(memhdr != NULL);

   if( lp->nchgcols == 0 )
      return SCIP_OKAY;

   /* get the solver's infinity value */
   infinity = SCIPlpiInfinity(lp->lpi);

   /* get temporary memory for changes */
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &ind, lp->ncols) );
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &lb, lp->ncols) );
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &ub, lp->ncols) );

   /* collect all cached bound changes */
   nchg = 0;
   for( i = 0; i < lp->nchgcols; ++i )
   {
      col = lp->chgcols[i];
      assert(col != NULL);

      var = col->var;
      assert(var != NULL);
      assert(var->varstatus == SCIP_VARSTATUS_COLUMN);
      assert(var->data.col == col);

      if( col->lpipos >= 0 )
      {
         if( col->lbchanged || col->ubchanged )
         {
            assert(nchg < lp->ncols);
            ind[nchg] = col->lpipos;
            if( SCIPsetIsInfinity(set, -var->dom.lb) )
               lb[nchg] = -infinity;
            else
               lb[nchg] = var->dom.lb;
            if( SCIPsetIsInfinity(set, var->dom.ub) )
               ub[nchg] = infinity;
            else
               ub[nchg] = var->dom.ub;
            nchg++;
            col->lbchanged = FALSE;
            col->ubchanged = FALSE;
         }
      }
   }

   /* change sides in LP */
   if( nchg > 0 )
   {
      debugMessage("flushing bound changes: change %d bounds of %d columns\n", nchg, lp->nchgcols);
      CHECK_OKAY( SCIPlpiChgBounds(lp->lpi, nchg, ind, lb, ub) );
   }

   lp->nchgcols = 0;

   /* free temporary memory */
   SCIPsetReleaseBufferArray(set, &ub);
   SCIPsetReleaseBufferArray(set, &lb);
   SCIPsetReleaseBufferArray(set, &ind);

   return SCIP_OKAY;
}

/** applies all cached row changes to the LP */
static
RETCODE lpFlushChgRows(
   LP*              lp,                 /**< actual LP data */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set                 /**< global SCIP settings */
   )
{
   ROW* row;
   int* ind;
   Real* lhs;
   Real* rhs;
   Real infinity;
   int i;
   int nchg;

   assert(lp != NULL);
   assert(memhdr != NULL);

   if( lp->nchgrows == 0 )
      return SCIP_OKAY;

   /* get the solver's infinity value */
   infinity = SCIPlpiInfinity(lp->lpi);

   /* get temporary memory for changes */
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &ind, lp->nrows) );
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &lhs, lp->nrows) );
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &rhs, lp->nrows) );

   /* collect all cached left and right hand side changes */
   nchg = 0;
   for( i = 0; i < lp->nchgrows; ++i )
   {
      row = lp->chgrows[i];
      assert(row != NULL);

      if( row->lpipos >= 0 )
      {
         if( row->lhschanged || row->rhschanged )
         {
            assert(nchg < lp->nrows);
            ind[nchg] = row->lpipos;
            if( SCIPsetIsInfinity(set, -row->lhs) )
               lhs[nchg] = -infinity;
            else
               lhs[nchg] = row->lhs + row->constant;
            if( SCIPsetIsInfinity(set, row->rhs) )
               rhs[nchg] = infinity;
            else
               rhs[nchg] = row->rhs + row->constant;
            nchg++;
            row->lhschanged = FALSE;
            row->rhschanged = FALSE;
         }
      }
   }

   /* change left and right hand sides in LP */
   if( nchg > 0 )
   {
      debugMessage("flushing side changes: change %d sides of %d rows\n", nchg, lp->nchgrows);
      CHECK_OKAY( SCIPlpiChgSides(lp->lpi, nchg, ind, lhs, rhs) );
   }

   lp->nchgrows = 0;

   /* free temporary memory */
   SCIPsetReleaseBufferArray(set, &rhs);
   SCIPsetReleaseBufferArray(set, &lhs);
   SCIPsetReleaseBufferArray(set, &ind);

   return SCIP_OKAY;
}

/** applies all cached changes to the LP solver */
static
RETCODE lpFlush(
   LP*              lp,                 /**< actual LP data */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set                 /**< global SCIP settings */
   )
{
   assert(lp != NULL);
   assert(memhdr != NULL);
   
   debugMessage("flushing LP changes: old (%d cols, %d rows), chgcol=%d, chgrow=%d, new (%d cols, %d rows)\n",
      lp->nlpicols, lp->nlpirows, lp->lpifirstchgcol, lp->lpifirstchgrow, lp->ncols, lp->nrows);
   if( lp->flushed )
   {
      assert(lp->nlpicols == lp->ncols);
      assert(lp->lpifirstchgcol == lp->nlpicols);
      assert(lp->nlpirows == lp->nrows);
      assert(lp->lpifirstchgrow == lp->nlpirows);
      assert(lp->nchgcols == 0);

      return SCIP_OKAY;
   }
   
   assert(!lp->solved);

   CHECK_OKAY( lpFlushDelCols(lp) );
   CHECK_OKAY( lpFlushDelRows(lp) );
   CHECK_OKAY( lpFlushChgCols(lp, memhdr, set) );
   CHECK_OKAY( lpFlushChgRows(lp, memhdr, set) );
   CHECK_OKAY( lpFlushAddCols(lp, memhdr, set) );
   CHECK_OKAY( lpFlushAddRows(lp, memhdr, set) );

   lp->flushed = TRUE;

   return SCIP_OKAY;
}



/*
 * LP methods
 */

/** creates empty LP data object */
RETCODE SCIPlpCreate(
   LP**             lp,                 /**< pointer to LP data object */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   const char*      name                /**< problem name */
   )
{
   assert(lp != NULL);
   assert(set != NULL);
   assert(name != NULL);

   ALLOC_OKAY( allocMemory(lp) );

   /* open LP Solver interface */
   CHECK_OKAY( SCIPlpiCreate(&((*lp)->lpi), name) );

   (*lp)->lpicols = NULL;
   (*lp)->lpirows = NULL;
   (*lp)->chgcols = NULL;
   (*lp)->chgrows = NULL;
   (*lp)->cols = NULL;
   (*lp)->rows = NULL;
   (*lp)->lpsolstat = SCIP_LPSOLSTAT_OPTIMAL;
   (*lp)->objval = 0.0;
   (*lp)->lpicolssize = 0;
   (*lp)->nlpicols = 0;
   (*lp)->lpirowssize = 0;
   (*lp)->nlpirows = 0;
   (*lp)->lpifirstchgcol = 0;
   (*lp)->lpifirstchgrow = 0;
   (*lp)->colssize = 0;
   (*lp)->ncols = 0;
   (*lp)->rowssize = 0;
   (*lp)->nrows = 0;
   (*lp)->chgcolssize = 0;
   (*lp)->nchgcols = 0;
   (*lp)->chgrowssize = 0;
   (*lp)->nchgrows = 0;
   (*lp)->firstnewcol = 0;
   (*lp)->firstnewrow = 0;
   (*lp)->flushed = TRUE;
   (*lp)->solved = TRUE;
   (*lp)->primalfeasible = TRUE;
   (*lp)->dualfeasible = TRUE;

   /* set default parameters in LP solver */
   CHECK_OKAY( SCIPlpSetFeastol(*lp, set->feastol) );

   return SCIP_OKAY;
}

/** frees LP data object */
RETCODE SCIPlpFree(
   LP**             lp,                 /**< pointer to LP data object */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set                 /**< global SCIP settings */
   )
{
   assert(lp != NULL);
   assert(*lp != NULL);
   
   CHECK_OKAY( SCIPlpClear(*lp, memhdr, set) );

   if( (*lp)->lpi != NULL )
   {
      CHECK_OKAY( SCIPlpiFree(&((*lp)->lpi)) );
   }

   freeMemoryArrayNull(&(*lp)->lpicols);
   freeMemoryArrayNull(&(*lp)->lpirows);
   freeMemoryArrayNull(&(*lp)->chgcols);
   freeMemoryArrayNull(&(*lp)->cols);
   freeMemoryArrayNull(&(*lp)->rows);
   freeMemory(lp);

   return SCIP_OKAY;
}

/** adds a column to the LP */
RETCODE SCIPlpAddCol(
   LP*              lp,                 /**< LP data */
   const SET*       set,                /**< global SCIP settings */
   COL*             col                 /**< LP column */
   )
{
   assert(lp != NULL);
   assert(col != NULL);
   assert(col->lppos == -1);
   assert(col->var != NULL);
   assert(col->var->varstatus == SCIP_VARSTATUS_COLUMN);
   assert(col->var->data.col == col);
   
   debugMessage("adding column <%s> to LP\n", col->var->name);
   CHECK_OKAY( ensureColsSize(lp, set, lp->ncols+1) );
   lp->cols[lp->ncols] = col;
   col->lppos = lp->ncols;
   lp->ncols++;
   lp->flushed = FALSE;
   lp->solved = FALSE;
   lp->dualfeasible = FALSE;
   lp->objval = SCIP_INVALID;
   lp->lpsolstat = SCIP_LPSOLSTAT_NOTSOLVED;

   return SCIP_OKAY;
}

/** adds a row to the LP and captures it */
RETCODE SCIPlpAddRow(
   LP*              lp,                 /**< LP data */
   const SET*       set,                /**< global SCIP settings */
   ROW*             row                 /**< LP row */
   )
{
   assert(lp != NULL);
   assert(row != NULL);
   assert(row->lppos == -1);

   SCIProwCapture(row);

   debugMessage("adding row <%s> to LP\n", row->name);
   CHECK_OKAY( ensureRowsSize(lp, set, lp->nrows+1) );
   lp->rows[lp->nrows] = row;
   row->lppos = lp->nrows;
   lp->nrows++;
   lp->flushed = FALSE;
   lp->solved = FALSE;
   lp->primalfeasible = FALSE;
   lp->objval = SCIP_INVALID;
   lp->lpsolstat = SCIP_LPSOLSTAT_NOTSOLVED;

   return SCIP_OKAY;
}

/** removes all columns after the given number of cols from the LP */
RETCODE SCIPlpShrinkCols(
   LP*              lp,                 /**< LP data */
   int              newncols            /**< new number of columns in the LP */
   )
{
   int c;

   assert(lp != NULL);
   debugMessage("shrinking LP from %d to %d columns\n", lp->ncols, newncols);
   assert(0 <= newncols);
   assert(newncols <= lp->ncols);

   if( newncols < lp->ncols )
   {
      for( c = newncols; c < lp->ncols; ++c )
      {
         assert(lp->cols[c]->var != NULL);
         assert(lp->cols[c]->var->varstatus == SCIP_VARSTATUS_COLUMN);
         assert(lp->cols[c]->var->data.col == lp->cols[c]);
         assert(lp->cols[c]->lppos == c);
         
         lp->cols[c]->lppos = -1;
      }
      lp->ncols = newncols;
      lp->lpifirstchgcol = MIN(lp->lpifirstchgcol, newncols);
      lp->flushed = FALSE;
      lp->solved = FALSE;
      lp->primalfeasible = FALSE;
      lp->objval = SCIP_INVALID;
      lp->lpsolstat = SCIP_LPSOLSTAT_NOTSOLVED;
   }

   return SCIP_OKAY;
}

/** removes and releases all rows after the given number of rows from the LP */
RETCODE SCIPlpShrinkRows(
   LP*              lp,                 /**< LP data */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   int              newnrows            /**< new number of rows in the LP */
   )
{
   int r;

   assert(lp != NULL);
   assert(0 <= newnrows && newnrows <= lp->nrows);

   debugMessage("shrinking LP from %d to %d rows\n", lp->nrows, newnrows);
   if( newnrows < lp->nrows )
   {
      for( r = newnrows; r < lp->nrows; ++r )
      {
         assert(lp->rows[r]->lppos == r);
         lp->rows[r]->lppos = -1;
         CHECK_OKAY( SCIProwRelease(&lp->rows[r], memhdr, set, lp) );
      }
      lp->nrows = newnrows;
      lp->lpifirstchgrow = MIN(lp->lpifirstchgrow, newnrows);
      lp->flushed = FALSE;
      lp->solved = FALSE;
      lp->dualfeasible = FALSE;
      lp->objval = SCIP_INVALID;
      lp->lpsolstat = SCIP_LPSOLSTAT_NOTSOLVED;
   }

   return SCIP_OKAY;
}

/** removes all columns and rows from LP, releases all rows */
RETCODE SCIPlpClear(
   LP*              lp,                 /**< LP data */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set                 /**< global SCIP settings */
   )
{
   assert(lp != NULL);

   debugMessage("clearing LP\n");
   CHECK_OKAY( SCIPlpShrinkCols(lp, 0) );
   CHECK_OKAY( SCIPlpShrinkRows(lp, memhdr, set, 0) );

   return SCIP_OKAY;
}

/** remembers number of columns and rows to track the newly added ones */
void SCIPlpMarkSize(
   LP*              lp                  /**< actual LP data */
   )
{
   assert(lp != NULL);
   lp->firstnewcol = lp->ncols;
   lp->firstnewrow = lp->nrows;
}

/** get array with newly added columns after the last mark */
COL** SCIPlpGetNewcols(
   const LP*        lp                  /**< actual LP data */
   )
{
   assert(lp != NULL);
   assert(0 <= lp->firstnewcol && lp->firstnewcol <= lp->ncols);

   return &(lp->cols[lp->firstnewcol]);
}

/** get number of newly added columns after the last mark */
int SCIPlpGetNumNewcols(
   const LP*        lp                  /**< actual LP data */
   )
{
   assert(lp != NULL);
   assert(0 <= lp->firstnewcol && lp->firstnewcol <= lp->ncols);

   return lp->ncols - lp->firstnewcol;
}

/** get array with newly added rows after the last mark */
ROW** SCIPlpGetNewrows(
   const LP*        lp                  /**< actual LP data */
   )
{
   assert(lp != NULL);
   assert(0 <= lp->firstnewrow && lp->firstnewrow <= lp->nrows);

   return &(lp->rows[lp->firstnewrow]);
}

/** get number of newly added rows after the last mark */
int SCIPlpGetNumNewrows(
   const LP*        lp                  /**< actual LP data */
   )
{
   assert(lp != NULL);
   assert(0 <= lp->firstnewrow && lp->firstnewrow <= lp->nrows);

   return lp->nrows - lp->firstnewrow;
}

/** stores LP state (like basis information) into LP state object */
RETCODE SCIPlpGetState(
   LP*              lp,                 /**< LP data */
   MEMHDR*          memhdr,             /**< block memory */
   LPISTATE**       lpistate            /**< pointer to LP state information (like basis information) */
   )
{
   assert(lp != NULL);
   assert(lp->flushed);
   assert(lp->solved);
   assert(memhdr != NULL);
   assert(lpistate != NULL);

   CHECK_OKAY( SCIPlpiGetState(lp->lpi, memhdr, lpistate) );

   return SCIP_OKAY;
}

/** loads LP state (like basis information) into solver */
RETCODE SCIPlpSetState(
   LP*              lp,                 /**< LP data */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   LPISTATE*        lpistate            /**< LP state information (like basis information) */
   )
{
   assert(lp != NULL);
   assert(memhdr != NULL);
   assert(lpistate != NULL);

   lpFlush(lp, memhdr, set);

   CHECK_OKAY( SCIPlpiSetState(lp->lpi, memhdr, lpistate) );
   lp->primalfeasible = TRUE;
   lp->dualfeasible = TRUE;

   return SCIP_OKAY;
}

/** sets the feasibility tolerance of the LP solver */
RETCODE SCIPlpSetFeastol(
   LP*              lp,                 /**< actual LP data */
   Real             feastol             /**< new feasibility tolerance */
   )
{
   assert(lp != NULL);
   assert(feastol >= 0.0);

   CHECK_OKAY( SCIPlpiSetRealpar(lp->lpi, SCIP_LPPAR_FEASTOL, feastol) );
   if( lp->nrows > 0 )
   {
      lp->solved = FALSE;
      lp->lpsolstat = SCIP_LPSOLSTAT_NOTSOLVED;
      lp->primalfeasible = FALSE;
   }

   return SCIP_OKAY;
}

/** sets the upper objective limit of the LP solver */
RETCODE SCIPlpSetUpperbound(
   LP*              lp,                 /**< actual LP data */
   Real             upperbound          /**< new upper objective limit */
   )
{
   assert(lp != NULL);

   debugMessage("setting LP upper objective limit to %g\n", upperbound);
   CHECK_OKAY( SCIPlpiSetRealpar(lp->lpi, SCIP_LPPAR_UOBJLIM, upperbound) );

   return SCIP_OKAY;
}

/** solves the LP with the primal simplex algorithm */
RETCODE SCIPlpSolvePrimal(
   LP*              lp,                 /**< actual LP data */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   STAT*            stat                /**< problem statistics */
   )
{
   int iterations;
   Bool primalfeasible;
   Bool dualfeasible;

   assert(lp != NULL);
   assert(memhdr != NULL);
   assert(set != NULL);
   assert(stat != NULL);

   debugMessage("solving primal LP %d (LP %d, %d cols, %d rows)\n", stat->nprimallp+1, stat->nlp+1, lp->ncols, lp->nrows);

   /* flush changes to the LP solver */
   CHECK_OKAY( lpFlush(lp, memhdr, set) );

   /* call primal simplex */
   CHECK_OKAY( SCIPlpiSolvePrimal(lp->lpi) );

   /* check for primal and dual feasibility */
   CHECK_OKAY( SCIPlpiGetBasisFeasibility(lp->lpi, &primalfeasible, &dualfeasible) );
   lp->primalfeasible = primalfeasible;
   lp->dualfeasible = dualfeasible;

   /* evaluate solution status */
   if( SCIPlpiIsOptimal(lp->lpi) )
   {
      assert(lp->primalfeasible);
      assert(lp->dualfeasible);
      lp->lpsolstat = SCIP_LPSOLSTAT_OPTIMAL;
      CHECK_OKAY( SCIPlpiGetObjval(lp->lpi, &lp->objval) );
   }
   else if( SCIPlpiIsPrimalInfeasible(lp->lpi) )
   {
      lp->lpsolstat = SCIP_LPSOLSTAT_INFEASIBLE;
      lp->objval = set->infinity;
   }
   else if( SCIPlpiIsPrimalUnbounded(lp->lpi) )
   {
      lp->lpsolstat = SCIP_LPSOLSTAT_UNBOUNDED;
      lp->objval = -set->infinity;
   }
   else if( SCIPlpiIsIterlimExc(lp->lpi) )
   {
      lp->lpsolstat = SCIP_LPSOLSTAT_ITERLIMIT;
      lp->objval = -set->infinity;
   }
   else if( SCIPlpiIsTimelimExc(lp->lpi) )
   {
      lp->lpsolstat = SCIP_LPSOLSTAT_TIMELIMIT;
      lp->objval = -set->infinity;
   }
   else if( SCIPlpiIsObjlimExc(lp->lpi) )
   {
      errorMessage("Objective limit exceeded in primal simplex - this should not happen");
      lp->lpsolstat = SCIP_LPSOLSTAT_ERROR;
      lp->objval = -set->infinity;
      return SCIP_LPERROR;
   }
   else
   {
      errorMessage("Unknown return status of primal simplex");
      lp->lpsolstat = SCIP_LPSOLSTAT_ERROR;
      return SCIP_LPERROR;
   }

   lp->solved = TRUE;

   stat->nlp++;
   stat->nprimallp++;
   CHECK_OKAY( SCIPlpGetIterations(lp, &iterations) );
   stat->nlpiterations += iterations;
   stat->nprimallpiterations += iterations;

   debugMessage("solving primal LP returned solstat=%d\n", lp->lpsolstat);

   return SCIP_OKAY;
}

/** solves the LP with the dual simplex algorithm */
RETCODE SCIPlpSolveDual(
   LP*              lp,                 /**< actual LP data */
   MEMHDR*          memhdr,             /**< block memory */
   const SET*       set,                /**< global SCIP settings */
   STAT*            stat                /**< problem statistics */
   )
{
   int iterations;
   Bool primalfeasible;
   Bool dualfeasible;

   assert(lp != NULL);
   assert(memhdr != NULL);
   assert(set != NULL);
   assert(stat != NULL);

   debugMessage("solving dual LP %d (LP %d, %d cols, %d rows)\n", stat->nduallp+1, stat->nlp+1, lp->ncols, lp->nrows);

   /* flush changes to the LP solver */
   CHECK_OKAY( lpFlush(lp, memhdr, set) );

   /* call primal simplex */
   CHECK_OKAY( SCIPlpiSolveDual(lp->lpi) );

   /* check for primal and dual feasibility */
   CHECK_OKAY( SCIPlpiGetBasisFeasibility(lp->lpi, &primalfeasible, &dualfeasible) );
   lp->primalfeasible = primalfeasible;
   lp->dualfeasible = dualfeasible;

   /* evaluate solution status */
   if( SCIPlpiIsOptimal(lp->lpi) )
   {
      assert(lp->primalfeasible);
      assert(lp->dualfeasible);
      lp->lpsolstat = SCIP_LPSOLSTAT_OPTIMAL;
      CHECK_OKAY( SCIPlpiGetObjval(lp->lpi, &lp->objval) );
   }
   else if( SCIPlpiIsObjlimExc(lp->lpi) )
   {
      lp->lpsolstat = SCIP_LPSOLSTAT_OBJLIMIT;
      lp->objval = set->infinity;
   }
   else if( SCIPlpiIsPrimalInfeasible(lp->lpi) )
   {
      lp->lpsolstat = SCIP_LPSOLSTAT_INFEASIBLE;
      lp->objval = set->infinity;
   }
   else if( SCIPlpiIsPrimalUnbounded(lp->lpi) )
   {
      lp->lpsolstat = SCIP_LPSOLSTAT_UNBOUNDED;
      lp->objval = -set->infinity;
   }
   else if( SCIPlpiIsIterlimExc(lp->lpi) )
   {
      lp->lpsolstat = SCIP_LPSOLSTAT_ITERLIMIT;
      CHECK_OKAY( SCIPlpiGetObjval(lp->lpi, &lp->objval) );
   }
   else if( SCIPlpiIsTimelimExc(lp->lpi) )
   {
      lp->lpsolstat = SCIP_LPSOLSTAT_TIMELIMIT;
      CHECK_OKAY( SCIPlpiGetObjval(lp->lpi, &lp->objval) );
   }
   else
   {
      errorMessage("Unknown return status of dual simplex");
      lp->lpsolstat = SCIP_LPSOLSTAT_ERROR;
      lp->objval = -set->infinity;
      return SCIP_LPERROR;
   }

   lp->solved = TRUE;

   stat->nlp++;
   stat->nduallp++;
   CHECK_OKAY( SCIPlpGetIterations(lp, &iterations) );
   stat->nlpiterations += iterations;
   stat->nduallpiterations += iterations;

   debugMessage("solving dual LP returned solstat=%d\n", lp->lpsolstat);

   return SCIP_OKAY;
}

/** gets solution status of last solve call */
LPSOLSTAT SCIPlpGetSolstat(
   LP*              lp                  /**< actual LP data */
   )
{
   assert(lp != NULL);
   assert(lp->solved || lp->lpsolstat == SCIP_LPSOLSTAT_NOTSOLVED);

   return lp->lpsolstat;
}

/** gets objective value of last solution */
Real SCIPlpGetObjval(
   LP*              lp                  /**< actual LP data */
   )
{
   assert(lp != NULL);
   assert(lp->solved);

   return lp->objval;
}


/** stores the LP solution in the columns and rows */
RETCODE SCIPlpGetSol(
   LP*              lp,                 /**< actual LP data */
   MEMHDR*          memhdr,             /**< block memory buffers */
   const SET*       set,                /**< global SCIP settings */
   STAT*            stat                /**< problem statistics */
   )
{
   Real* primsol;
   Real* dualsol;
   Real* activity;
   Real* redcost;
   int c;
   int r;

   assert(lp != NULL);
   assert(lp->flushed);
   assert(lp->solved);
   assert(set != NULL);
   assert(memhdr != NULL);

   /* get temporary memory */
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &primsol, lp->nlpicols) );
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &dualsol, lp->nlpirows) );
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &activity, lp->nlpirows) );
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &redcost, lp->nlpicols) );
   
   CHECK_OKAY( SCIPlpiGetSol(lp->lpi, &lp->objval, primsol, dualsol, activity, redcost) );

   debugMessage("LP solution: obj=%f\n", lp->objval);

   for( c = 0; c < lp->nlpicols; ++c )
   {
      lp->lpicols[c]->primsol = primsol[c];
      lp->lpicols[c]->redcost = redcost[c];
      lp->lpicols[c]->validredcostlp = stat->nlp;
      debugMessage(" col <%s>: primsol=%f, redcost=%f\n",
         lp->lpicols[c]->var->name, lp->lpicols[c]->primsol, lp->lpicols[c]->redcost);
   }

   for( r = 0; r < lp->nlpirows; ++r )
   {
      lp->lpirows[r]->dualsol = dualsol[r];
      lp->lpirows[r]->activity = activity[r] + lp->lpirows[r]->constant;
      lp->lpirows[r]->validactivitylp = stat->nlp;
      debugMessage(" row <%s>: dualsol=%f, activity=%f\n", 
         lp->lpirows[r]->name, lp->lpirows[r]->dualsol, lp->lpirows[r]->activity);
   }

   /* free temporary memory */
   SCIPsetReleaseBufferArray(set, &redcost);
   SCIPsetReleaseBufferArray(set, &activity);
   SCIPsetReleaseBufferArray(set, &dualsol);
   SCIPsetReleaseBufferArray(set, &primsol);

   return SCIP_OKAY;
}

/** stores LP solution with infinite objective value in the columns and rows */
RETCODE SCIPlpGetUnboundedSol(
   LP*              lp,                 /**< actual LP data */
   MEMHDR*          memhdr,             /**< block memory buffers */
   const SET*       set,                /**< global SCIP settings */
   STAT*            stat                /**< problem statistics */
   )
{
   Real* primsol;
   Real* activity;
   Real* ray;
   Real rayobjval;
   Real rayscale;
   int c;
   int r;

   assert(lp != NULL);
   assert(lp->flushed);
   assert(lp->solved);
   assert(lp->lpsolstat == SCIP_LPSOLSTAT_UNBOUNDED);
   assert(set != NULL);
   assert(memhdr != NULL);

   /* get temporary memory */
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &primsol, lp->nlpicols) );
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &activity, lp->nlpirows) );
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &ray, lp->nlpicols) );

   /* get primal feasible point */
   CHECK_OKAY( SCIPlpiGetSol(lp->lpi, &lp->objval, primsol, NULL, activity, NULL) );

   /* get primal unbounded ray */
   CHECK_OKAY( SCIPlpiGetPrimalRay(lp->lpi, ray) );
   
   /* calculate the objective value decrease of the ray */
   rayobjval = 0.0;
   for( c = 0; c < lp->nlpicols; ++c )
   {
      assert(lp->lpicols[c] != NULL);
      assert(lp->lpicols[c]->var != NULL);
      rayobjval += ray[c] * lp->lpicols[c]->var->obj;
   }
   assert(SCIPsetIsNegative(set, rayobjval));

   /* scale the ray, such that the resulting point has infinite objective value */
   rayscale = -2*set->infinity/rayobjval;

   /* calculate the unbounded point: x' = x + rayscale * ray */
   debugMessage("unbounded LP solution: baseobjval=%f, rayobjval=%f, rayscale=%f\n", lp->objval, rayobjval, rayscale);
   lp->objval = -set->infinity;

   for( c = 0; c < lp->nlpicols; ++c )
   {
      lp->lpicols[c]->primsol = primsol[c] + rayscale * ray[c];
      lp->lpicols[c]->redcost = SCIP_INVALID;
      lp->lpicols[c]->validredcostlp = -1;
      debugMessage(" col <%s>: basesol=%f, ray=%f, unbdsol=%f\n", 
         lp->lpicols[c]->var->name, primsol[c], ray[c], lp->lpicols[c]->primsol);
   }

   for( r = 0; r < lp->nlpirows; ++r )
   {
      lp->lpirows[r]->dualsol = SCIP_INVALID;
      lp->lpirows[r]->activity = activity[r] + lp->lpirows[r]->constant;
      lp->lpirows[r]->validactivitylp = stat->nlp;
      debugMessage(" row <%s>: activity=%f\n", lp->lpirows[r]->name, lp->lpirows[r]->activity);
   }


   /* free temporary memory */
   SCIPsetReleaseBufferArray(set, &ray);
   SCIPsetReleaseBufferArray(set, &activity);
   SCIPsetReleaseBufferArray(set, &primsol);

   return SCIP_OKAY;
}

/** stores the dual farkas multipliers for infeasibility proof in rows */
RETCODE SCIPlpGetDualfarkas(
   LP*              lp,                 /**< actual LP data */
   MEMHDR*          memhdr,             /**< block memory buffers */
   const SET*       set                 /**< global SCIP settings */
   )
{
   Real* dualfarkas;
   int c;
   int r;

   assert(lp != NULL);
   assert(lp->flushed);
   assert(lp->solved);
   assert(lp->lpsolstat == SCIP_LPSOLSTAT_INFEASIBLE);
   assert(set != NULL);
   assert(memhdr != NULL);

   /* get temporary memory */
   CHECK_OKAY( SCIPsetCaptureBufferArray(set, &dualfarkas, lp->nlpirows) );

   /* get dual farkas infeasibility proof */
   CHECK_OKAY( SCIPlpiGetDualfarkas(lp->lpi, dualfarkas) );

   /* store infeasibility proof in rows */
   debugMessage("LP is infeasible:\n");
   for( r = 0; r < lp->nlpirows; ++r )
   {
      debugMessage(" row <%s>: dualfarkas=%f\n", lp->lpirows[r]->name, dualfarkas[r]);
      lp->lpirows[r]->dualfarkas = dualfarkas[r];
   }

   /* free temporary memory */
   SCIPsetReleaseBufferArray(set, &dualfarkas);
   
   return SCIP_OKAY;
}

/** get number of iterations used in last LP solve */
RETCODE SCIPlpGetIterations(
   LP*              lp,                 /**< actual LP data */
   int*             iterations          /**< pointer to store the iteration count */
   )
{
   int iter1;
   int iter2;

   assert(lp != NULL);
   assert(iterations != NULL);

   CHECK_OKAY( SCIPlpiGetIntpar(lp->lpi, SCIP_LPPAR_LPIT1, &iter1) );
   CHECK_OKAY( SCIPlpiGetIntpar(lp->lpi, SCIP_LPPAR_LPIT2, &iter2) );
   
   *iterations = iter1 + iter2;

   return SCIP_OKAY;
}
