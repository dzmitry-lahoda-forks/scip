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

/**@file   cons_expr_product.c
 * @brief  product expression handler
 * @author Stefan Vigerske
 * @author Benjamin Mueller
 * @author Felipe Serrano
 *
 * Implementation of the product expression, representing a product of expressions
 * and a constant, i.e., coef * prod_i x_i.
 *
 * @todo initsepaProduct
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include <string.h>

#include "scip/cons_expr_product.h"
#include "scip/cons_expr_sum.h"
#include "scip/cons_expr_value.h"
#include "scip/cons_expr_pow.h"
#include "scip/cons_expr_entropy.h"

#include "scip/pub_misc.h"

#define EXPRHDLR_NAME         "prod"
#define EXPRHDLR_DESC         "product of children"
#define EXPRHDLR_PRECEDENCE  50000
#define EXPRHDLR_HASHKEY     SCIPcalcFibHash(54949.0)

#define DEFAULT_RANDSEED           101  /**< initial random seed */

/** macro to activate/deactivate debugging information of simplify method */
#ifdef SIMPLIFY_DEBUG
#define debugSimplify                   printf
#else
#define debugSimplify                   while( FALSE ) printf
#endif

/*
 * Data structures
 */

struct SCIP_ConsExpr_ExprData
{
   SCIP_Real             coefficient;        /**< coefficient */

   SCIP_ROW*             row;                /**< row created during initLP() */
};

struct SCIP_ConsExpr_ExprHdlrData
{
   SCIP_LPI*             multilinearseparationlp; /**< lp to separate product expressions */
   int                   lpsize;             /**< number of rows - 1 of multilinearseparationlp */

   SCIP_RANDNUMGEN*      randnumgen;         /**< random number generator */
};

/** node for linked list of expressions */
struct exprnode
{
   SCIP_CONSEXPR_EXPR*   expr;               /**< expression in node */
   struct exprnode*      next;               /**< next node */
};

typedef struct exprnode EXPRNODE;


/*
 * Local methods
 */

/** builds a simplified product from simplifiedfactors
 * Note: this function also releases simplifiedfactors
 */
static
SCIP_RETCODE buildSimplifiedProduct(
   SCIP*                    scip,            /**< SCIP data structure */
   SCIP_Real                simplifiedcoef,  /**< simplified product should be simplifiedcoef * PI simplifiedfactors */
   EXPRNODE**               simplifiedfactors, /**< factors of simplified product */
   SCIP_Bool                changed,         /**< indicates whether some of the simplified factors was changed */
   SCIP_CONSEXPR_EXPR**     simplifiedexpr   /**< buffer to store the simplified expression */
   );

/*  methods for handling linked list of expressions */
/** inserts newnode at beginning of list */
static
void insertFirstList(
   EXPRNODE*             newnode,            /**< node to insert */
   EXPRNODE**            list                /**< list */
   )
{
   assert(list != NULL);
   assert(newnode != NULL);

   newnode->next = *list;
   *list = newnode;
}

/** removes first element of list and returns it */
static
EXPRNODE* listPopFirst(
   EXPRNODE**            list                /**< list */
   )
{
   EXPRNODE* first;

   assert(list != NULL);

   if( *list == NULL )
      return NULL;

   first = *list;
   *list = (*list)->next;
   first->next = NULL;

   return first;
}

/** returns length of list */
static
int listLength(
   EXPRNODE*             list                /**< list */
   )
{
   int length;

   if( list == NULL )
      return 0;

   length = 1;
   while( (list=list->next) != NULL )
      ++length;

   return length;
}

/** creates expression node and capture expression */
static
SCIP_RETCODE createExprNode(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSEXPR_EXPR*   expr,               /**< expression stored at node */
   EXPRNODE**            newnode             /**< pointer to store node */
   )
{
   SCIP_CALL( SCIPallocBuffer(scip, newnode) );

   (*newnode)->expr = expr;
   (*newnode)->next = NULL;
   SCIPcaptureConsExprExpr(expr);

   return SCIP_OKAY;
}

/** creates expression list from expressions */
static
SCIP_RETCODE createExprlistFromExprs(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSEXPR_EXPR**  exprs,              /**< expressions stored in list */
   int                   nexprs,             /**< number of expressions */
   EXPRNODE**            list                /**< pointer to store list */
   )
{
   int i;

   assert(*list == NULL);
   assert(nexprs > 0);

   debugSimplify("building expr list from %d expressions\n", nexprs); /*lint !e506 !e681*/
   for( i = nexprs - 1; i >= 0; --i )
   {
      EXPRNODE* newnode;

      SCIP_CALL( createExprNode(scip, exprs[i], &newnode) );
      insertFirstList(newnode, list);
   }
   assert(nexprs > 1 || (*list)->next == NULL);

   return SCIP_OKAY;
}

/** frees expression node and release expressions */
static
SCIP_RETCODE freeExprNode(
   SCIP*                 scip,               /**< SCIP data structure */
   EXPRNODE**            node                /**< node to be freed */
   )
{
   assert(node != NULL && *node != NULL);

   SCIP_CALL( SCIPreleaseConsExprExpr(scip, &(*node)->expr) );
   SCIPfreeBuffer(scip, node);

   return SCIP_OKAY;
}

/** frees an expression list */
static
SCIP_RETCODE freeExprlist(
   SCIP*                 scip,               /**< SCIP data structure */
   EXPRNODE**            exprlist            /**< list */
   )
{
   EXPRNODE* current;

   if( *exprlist == NULL )
      return SCIP_OKAY;

   current = *exprlist;
   while( current != NULL )
   {
      EXPRNODE* tofree;

      tofree = current;
      current = current->next;
      SCIP_CALL( freeExprNode(scip, &tofree) );
   }
   assert(current == NULL);
   *exprlist = NULL;

   return SCIP_OKAY;
}

static
SCIP_DECL_VERTEXPOLYFUN(prodfunction)
{
   /* funcdata is a pointer to the double holding the coefficient */
   SCIP_Real ret = *(SCIP_Real*)funcdata;
   int i;

   for( i = 0; i < nargs; ++i )
      ret *= args[i];

   return ret;
}

/* helper functions for simplifying expressions */

/** creates a product expression with the elements of exprlist as its children */
static
SCIP_RETCODE createExprProductFromExprlist(
   SCIP*                 scip,               /**< SCIP data structure */
   EXPRNODE*             exprlist,           /**< list containing the children of expr */
   SCIP_Real             coef,               /**< coef of expr */
   SCIP_CONSEXPR_EXPR**  expr                /**< pointer to store the product expression */
   )
{
   int i;
   int nchildren;
   SCIP_CONSEXPR_EXPR** children;

   /* asserts SP8 */
   assert(coef == 1.0);
   nchildren = listLength(exprlist);

   SCIP_CALL( SCIPallocBufferArray(scip, &children, nchildren) );

   for( i = 0; i < nchildren; ++i )
   {
      children[i] = exprlist->expr;
      exprlist = exprlist->next;
   }

   assert(exprlist == NULL);

   SCIP_CALL( SCIPcreateConsExprExprProduct(scip, SCIPfindConshdlr(scip, "expr"), expr, nchildren, children, coef) );

   SCIPfreeBufferArray(scip, &children);

   return SCIP_OKAY;
}

/** simplifies a factor of a product expression: base, so that it is a valid children of a simplified product expr
 * @note: in contrast to other simplify methods, this does *not* return a simplified expression.
 * Instead, the method is intended to be called only when simplifying a product expression,
 * Since in general, base is not a simplified child of a product expression, this method returns
 * a list of expressions L, such that (prod L) = baset *and* each expression in L
 * is a valid child of a simplified product expression.
 */
static
SCIP_RETCODE simplifyFactor(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSEXPR_EXPR*   factor,             /**< expression to be simplified */
   SCIP_Real*            simplifiedcoef,     /**< coefficient of parent product expression */
   EXPRNODE**            simplifiedfactor,   /**< pointer to store the resulting expression node/list of nodes */
   SCIP_Bool*            changed             /**< pointer to store if some term actually got simplified */
   )
{
   const char* factortype;

   assert(simplifiedfactor != NULL);
   assert(*simplifiedfactor == NULL);
   assert(factor != NULL);

   factortype = SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(factor));

   /* enforces SP7 */
   if( strcmp(factortype, "val") == 0 )
   {
      *changed = TRUE;
      *simplifiedcoef *= SCIPgetConsExprExprValueValue(factor);
      return SCIP_OKAY;
   }

   /* enforces SP2 */
   if( strcmp(factortype, EXPRHDLR_NAME) == 0 )
   {
      *changed = TRUE;

      /* assert SP8 */
      assert(SCIPgetConsExprExprProductCoef(factor) == 1.0);
      debugSimplify("[simplifyFactor] seeing a product: include its children\n"); /*lint !e506 !e681*/

      SCIP_CALL( createExprlistFromExprs(scip, SCIPgetConsExprExprChildren(factor),
               SCIPgetConsExprExprNChildren(factor), simplifiedfactor) );

      return SCIP_OKAY;
   }

   /* enforces SP13: a sum with a unique child and no constant -> take the coefficient and use its child as factor */
   if( strcmp(factortype, "sum") == 0 && SCIPgetConsExprExprNChildren(factor) == 1 &&
         SCIPgetConsExprExprSumConstant(factor) == 0.0 )
   {
      *changed = TRUE;

      /* assert SS8 and SS7 */
      assert(SCIPgetConsExprExprSumCoefs(factor)[0] != 0.0 && SCIPgetConsExprExprSumCoefs(factor)[0] != 1.0);
      debugSimplify("[simplifyFactor] seeing a sum of the form coef * child : take coef and child apart\n"); /*lint !e506 !e681*/

      SCIP_CALL( createExprlistFromExprs(scip, SCIPgetConsExprExprChildren(factor), 1, simplifiedfactor) );
      *simplifiedcoef *= SCIPgetConsExprExprSumCoefs(factor)[0];

      return SCIP_OKAY;
   }

   /* the given (simplified) expression `factor`, can be a child of a simplified product */
   assert(strcmp(factortype, EXPRHDLR_NAME) != 0);
   assert(strcmp(factortype, "val") != 0);
   SCIP_CALL( createExprNode(scip, factor, simplifiedfactor) );

   return SCIP_OKAY;
}

/** merges tomerge into finalchildren
 * Both, tomerge and finalchildren contain expressions that could be the children of a simplified product
 * (except for SP8 and SP10 which are enforced later).
 * However, the concatenation of both lists will not in general yield a simplified product expression,
 * because both SP4 and SP5 could be violated.  So the purpose of this method is to enforce SP4 and SP5.
 * In the process of enforcing SP4, it could happen that SP2 is violated. Since enforcing SP2
 * could generate further violations, we remove the affected children from finalchildren
 * and include them in unsimplifiedchildren for further processing.
 * @note: if tomerge has more than one element, then they are the children of a simplified product expression
 */
static
SCIP_RETCODE mergeProductExprlist(
   SCIP*                 scip,               /**< SCIP data structure */
   EXPRNODE*             tomerge,            /**< list to merge */
   EXPRNODE**            finalchildren,      /**< pointer to store the result of merge between tomerge and *finalchildren */
   EXPRNODE**            unsimplifiedchildren,/**< the list of children that should go to the product expression; they are
                                                  unsimplified when seen as children of a simplified product */
   SCIP_Bool*            changed             /**< pointer to store if some term actually got simplified */
   )
{
   EXPRNODE* tomergenode;
   EXPRNODE* current;
   EXPRNODE* previous;

   if( tomerge == NULL )
      return SCIP_OKAY;

   if( *finalchildren == NULL )
   {
      *finalchildren = tomerge;
      return SCIP_OKAY;
   }

   tomergenode = tomerge;
   current = *finalchildren;
   previous = NULL;

   while( tomergenode != NULL && current != NULL )
   {
      int compareres;
      EXPRNODE* aux;
      SCIP_CONSEXPR_EXPR* base1;
      SCIP_CONSEXPR_EXPR* base2;
      SCIP_Real expo1;
      SCIP_Real expo2;

      /* assert invariants */
      assert(strcmp(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(tomergenode->expr)), "val") != 0);
      assert(strcmp(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(current->expr)), "val") != 0);
      assert(previous == NULL || previous->next == current);

      /* in general the base of an expression is itself if type(expr) != pow, otherwise it is child of pow */
      /* TODO: better documentation
       *       clean code */
      if( strcmp(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(current->expr)), "pow") == 0 )
      {
         base1 = SCIPgetConsExprExprChildren(current->expr)[0];
         expo1 = SCIPgetConsExprExprPowExponent(current->expr);
      }
      else
      {
         base1 = current->expr;
         expo1 = 1.0;
      }
      if( strcmp(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(tomergenode->expr)), "pow") == 0 )
      {
         base2 = SCIPgetConsExprExprChildren(tomergenode->expr)[0];
         expo2 = SCIPgetConsExprExprPowExponent(tomergenode->expr);
      }
      else
      {
         base2 = tomergenode->expr;
         expo2 = 1.0;
      }

      /* if both bases are the same: have to build simplifiy(base^(expo1 + expo2)) */
      if( SCIPcompareConsExprExprs(base1, base2) == 0 )
      {
         SCIP_CONSEXPR_EXPR* power;
         SCIP_CONSEXPR_EXPR* simplifiedpower;

         *changed = TRUE;

         SCIP_CALL( SCIPcreateConsExprExprPow(scip, SCIPfindConshdlr(scip, "expr"), &power, base1, expo1 + expo2) );
         SCIP_CALL( SCIPsimplifyConsExprExprHdlr(scip, power, &simplifiedpower) ); /* calls simplifyPow */
         SCIP_CALL( SCIPreleaseConsExprExpr(scip, &power) );

         /* replace tomergenode's expression with simplifiedpower */
         SCIP_CALL( SCIPreleaseConsExprExpr(scip, &tomergenode->expr) );
         tomergenode->expr = simplifiedpower;
         /* move tomergenode to unsimplifiedchildren */
         aux = tomergenode;
         tomergenode = tomergenode->next;
         insertFirstList(aux, unsimplifiedchildren);

         /* destroy current */
         if( current == *finalchildren )
         {
            assert(previous == NULL);
            aux = listPopFirst(finalchildren);
            assert(aux == current);
            current = *finalchildren;
         }
         else
         {
            assert(previous != NULL);
            aux = current;
            current = current->next;
            previous->next = current;
         }
         SCIP_CALL( freeExprNode(scip, &aux) );

         continue;
      }

      /* bases are not the same, then expressions cannot be the same */
      compareres = SCIPcompareConsExprExprs(current->expr, tomergenode->expr);
      if( compareres == -1 )
      {
         /* current < tomergenode => move current */
         previous = current;
         current = current->next;
      }
      else
      {
         *changed = TRUE;
         assert(compareres == 1);

         /* insert: if current is the first node, then insert at beginning; otherwise, insert between previous and current */
         if( current == *finalchildren )
         {
            assert(previous == NULL);
            aux = tomergenode;
            tomergenode = tomergenode->next;
            insertFirstList(aux, finalchildren);
            previous = *finalchildren;
         }
         else
         {
            assert(previous != NULL);
            /* extract */
            aux = tomergenode;
            tomergenode = tomergenode->next;
            /* insert */
            previous->next = aux;
            aux->next = current;
            previous = aux;
         }
      }
   }

   /* if all nodes of tomerge were merged, we are done */
   if( tomergenode == NULL )
      return SCIP_OKAY;

   assert(current == NULL);

   /* if all nodes of finalchildren were cancelled by nodes of tomerge (ie, transfered to unsimplifiedchildren),
    * then the rest of tomerge is finalchildren */
   if( *finalchildren == NULL )
   {
      assert(previous == NULL);
      *finalchildren = tomergenode;
      return SCIP_OKAY;
   }

   /* there are still nodes of tomerge unmerged; these nodes are larger than finalchildren, so append at end */
   assert(previous != NULL && previous->next == NULL);
   previous->next = tomergenode;

   return SCIP_OKAY;
}

static
SCIP_RETCODE createData(
   SCIP*                    scip,            /**< SCIP data structure */
   SCIP_CONSEXPR_EXPRDATA** exprdata,        /**< pointer where to store expression data */
   SCIP_Real                coefficient      /**< coefficient of product */
   )
{
   assert(exprdata != NULL);

   SCIP_CALL( SCIPallocBlockMemory(scip, exprdata) );

   (*exprdata)->coefficient  = coefficient;
   (*exprdata)->row          = NULL;

   return SCIP_OKAY;
}


/** simplifies the given (simplified) exprs so that they can be factors of a simplified product;
 * in particular, it will sort and multiply factors whose product leads to new expressions
 */
static
SCIP_RETCODE simplifyMultiplyChildren(
   SCIP*                    scip,            /**< SCIP data structure */
   SCIP_CONSEXPR_EXPR**     exprs,           /**< factors to be simplified */
   int                      nexprs,          /**< number of factors */
   SCIP_Real*               simplifiedcoef,  /**< buffer to store coefficient of PI exprs; do not initialize */
   EXPRNODE**               finalchildren,   /**< expr node list to store the simplified factors */
   SCIP_Bool*               changed          /**< buffer to store whether some factor changed */
   )
{
   EXPRNODE* unsimplifiedchildren;

   /* set up list of current children (when looking at each of them individually, they are simplified, but as
    * children of a product expression they might be unsimplified)
    */
   unsimplifiedchildren = NULL;
   SCIP_CALL( createExprlistFromExprs(scip, exprs, nexprs, &unsimplifiedchildren) );

   *changed = FALSE;

   /* while there are still children to process */
   *finalchildren  = NULL;
   while( unsimplifiedchildren != NULL )
   {
      EXPRNODE* tomerge;
      EXPRNODE* first;

      first = listPopFirst(&unsimplifiedchildren);
      assert(first != NULL);

#ifdef SIMPLIFY_DEBUG
      debugSimplify("simplifying factor:\n");
      SCIP_CALL( SCIPprintConsExprExpr(scip, SCIPfindConshdlr(scip, "expr"), first->expr, NULL) );
      SCIPinfoMessage(scip, NULL, "\n");
#endif

      /* enforces SP2, SP7 and SP13 */
      tomerge = NULL;
      SCIP_CALL( simplifyFactor(scip, first->expr, simplifiedcoef, &tomerge, changed) );

      /* enforces SP4 and SP5 note: merge frees (or uses) the nodes of the tomerge list */
      SCIP_CALL( mergeProductExprlist(scip, tomerge, finalchildren, &unsimplifiedchildren, changed) );

      /* free first */
      SCIP_CALL( freeExprlist(scip, &first) );

      /* if the simplified coefficient is 0, we can return value 0 */
      if( *simplifiedcoef == 0.0 )
      {
         *changed = TRUE;
         SCIP_CALL( freeExprlist(scip, finalchildren) );
         SCIP_CALL( freeExprlist(scip, &unsimplifiedchildren) );
         assert(*finalchildren == NULL);
         break;
      }
   }
   return SCIP_OKAY;
}

/* make sure product has at least two children
 * - if it is empty; return value
 * - if it has one child and coef = 1; return child
 * - if it has one child and coef != 1; return (sum 0 coef expr)
 */
static
SCIP_RETCODE enforceSP10(
   SCIP*                    scip,            /**< SCIP data structure */
   SCIP_Real                simplifiedcoef,  /**< simplified product should be simplifiedcoef * PI simplifiedfactors */
   EXPRNODE*                finalchildren,   /**< factors of simplified product */
   SCIP_CONSEXPR_EXPR**     simplifiedexpr   /**< buffer to store the simplified expression */
   )
{
   /* empty list --> return value */
   if( finalchildren == NULL )
   {
      SCIP_CALL( SCIPcreateConsExprExprValue(scip, SCIPfindConshdlr(scip, "expr"), simplifiedexpr, simplifiedcoef) );
      return SCIP_OKAY;
   }

   /* one child and coef equal to 1 --> return child */
   if( finalchildren->next == NULL && simplifiedcoef == 1.0 )
   {
      *simplifiedexpr = finalchildren->expr;
      SCIPcaptureConsExprExpr(*simplifiedexpr);
      return SCIP_OKAY;
   }

   /* one child and coef different from 1 --> return (sum 0 coef child) */
   if( finalchildren->next == NULL )
   {
      SCIP_CONSEXPR_EXPR* sum;

      SCIP_CALL( SCIPcreateConsExprExprSum(scip, SCIPfindConshdlr(scip, "expr"), &sum,
               1, &(finalchildren->expr), &simplifiedcoef, 0.0) );

      /* simplifying here is necessary, the product could have sums as children e.g., (prod 2 (sum 1 <x>))
       * -> (sum 0 2 (sum 1 <x>)) and that needs to be simplified to (sum 0 2 <x>)
       */
      SCIP_CALL( SCIPsimplifyConsExprExprHdlr(scip, sum, simplifiedexpr) );
      SCIP_CALL( SCIPreleaseConsExprExpr(scip, &sum) );
      return SCIP_OKAY;
   }

   return SCIP_OKAY;
}

/** check if it is entropy expression */
static
SCIP_RETCODE enforceSP11(
   SCIP*                    scip,            /**< SCIP data structure */
   SCIP_Real                simplifiedcoef,  /**< simplified product should be simplifiedcoef * PI simplifiedfactors */
   EXPRNODE*                finalchildren,   /**< factors of simplified product */
   SCIP_CONSEXPR_EXPR**     simplifiedexpr   /**< buffer to store the simplified expression */
   )
{
   SCIP_CONSEXPR_EXPR* entropicchild = NULL;

   if( ! (finalchildren != NULL && finalchildren->next != NULL && finalchildren->next->next == NULL) )
      return SCIP_OKAY;

   /* could be log(expr) * expr, e.g., log(sin(x)) * sin(x) (OR11) */
   if( strcmp(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(finalchildren->expr)), "log") == 0 )
   {
      assert(SCIPgetConsExprExprNChildren(finalchildren->expr) == 1);
      if( 0 == SCIPcompareConsExprExprs(SCIPgetConsExprExprChildren(finalchildren->expr)[0], finalchildren->next->expr) )
         entropicchild = finalchildren->next->expr;
   }
   /* could be expr * log(expr), e.g., (1 + abs(x)) log(1 + abs(x)) (OR11) */
   else if( strcmp(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(finalchildren->next->expr)), "log") == 0 )
   {
      assert(SCIPgetConsExprExprNChildren(finalchildren->next->expr) == 1);
      if( 0 == SCIPcompareConsExprExprs(SCIPgetConsExprExprChildren(finalchildren->next->expr)[0], finalchildren->expr) )
         entropicchild = finalchildren->expr;
   }

   /* success --> replace finalchildren by entropy expression */
   if( entropicchild != NULL )
   {
      SCIP_CONSEXPR_EXPR* entropy;

      simplifiedcoef *= -1.0;

      SCIP_CALL( SCIPcreateConsExprExprEntropy(scip, SCIPfindConshdlr(scip, "expr"), &entropy, entropicchild) );

      /* enforces SP8: if simplifiedcoef != 1.0, transform it into a sum with the (simplified) entropy as child */
      if( simplifiedcoef != 1.0 )
      {
         SCIP_CALL( SCIPcreateConsExprExprSum(scip, SCIPfindConshdlr(scip, "expr"), simplifiedexpr,
                  1, &entropy, &simplifiedcoef, 0.0) );
         SCIP_CALL( SCIPreleaseConsExprExpr(scip, &entropy) );
      }
      else
         *simplifiedexpr = entropy;
   }

   return SCIP_OKAY;
}

/* expands product of two sums or one sum and another expression
 * -) two sums: (prod (sum c1 s1 ... sn) (sum c2 t1 ... tm)
 *    Builds a sum representing the expansion, where all of its children are simplified, and then simplify the sum
 *    - constant != 0 --> c1 ti or c2 * sj is simplified (ti, sj are not sums, because they are children of a simplified sum)
 *    - sj * ti may be not be simplified, so put them in a product list and simplify them from there
 * -) one sum: (prod factor (sum c s1 ... sn))
 *    - c != 0 --> c * factor is simplified (i.e. factor is not sum!)
 *    - factor * si may be not be simplified, so put them in a product list and simplify them from there
 */
static
SCIP_RETCODE enforceSP12(
   SCIP*                    scip,            /**< SCIP data structure */
   SCIP_Real                simplifiedcoef,  /**< simplified product should be simplifiedcoef * PI simplifiedfactors */
   EXPRNODE*                finalchildren,   /**< factors of simplified product */
   SCIP_CONSEXPR_EXPR**     simplifiedexpr   /**< buffer to store the simplified expression */
   )
{
   /* we need only two children */
   if( ! (finalchildren != NULL && finalchildren->next != NULL && finalchildren->next->next == NULL) )
      return SCIP_OKAY;

   /* handle both sums case */
   if( strcmp(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(finalchildren->expr)), "sum") == 0 &&
         strcmp(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(finalchildren->next->expr)), "sum") == 0 )
   {
      SCIP_CONSEXPR_EXPR* expanded = NULL;
      SCIP_Real c1 = SCIPgetConsExprExprSumConstant(finalchildren->expr);
      SCIP_Real c2 = SCIPgetConsExprExprSumConstant(finalchildren->next->expr);
      int nchildren1 = SCIPgetConsExprExprNChildren(finalchildren->expr);
      int nchildren2 = SCIPgetConsExprExprNChildren(finalchildren->next->expr);
      int j;
      int k;

#ifdef SIMPLIFY_DEBUG
      debugSimplify("Multiplying sum1 * sum2\n");
      debugSimplify("sum1: \n");
      SCIP_CALL( SCIPprintConsExprExpr(scip, SCIPfindConshdlr(scip, "expr"), finalchildren->expr, NULL) );
      SCIPinfoMessage(scip, NULL, "\n");
      debugSimplify("sum2: \n");
      SCIP_CALL( SCIPprintConsExprExpr(scip, SCIPfindConshdlr(scip, "expr"), finalchildren->next->expr, NULL) );
      SCIPinfoMessage(scip, NULL, "\n");
#endif
      SCIP_CALL( SCIPcreateConsExprExprSum(scip, SCIPfindConshdlr(scip, "expr"), &expanded,
               0, NULL, NULL, c1 * c2 * simplifiedcoef) );
      /* multiply c1 * sum2 */
      if( c1 != 0.0 )
      {
         int i;

         for( i = 0; i < nchildren2; ++i )
         {
            SCIP_CONSEXPR_EXPR* term;

            term = SCIPgetConsExprExprChildren(finalchildren->next->expr)[i];
            SCIP_CALL( SCIPappendConsExprExprSumExpr(scip, expanded, term,
                     SCIPgetConsExprExprSumCoefs(finalchildren->next->expr)[i] * c1 * simplifiedcoef) );
            /* we are just re-using a child here, so do not release term! */
#ifdef SIMPLIFY_DEBUG
            debugSimplify("Multiplying %f * summand2_i\n", c1);
            debugSimplify("summand2_i: \n");
            SCIP_CALL( SCIPprintConsExprExpr(scip, SCIPfindConshdlr(scip, "expr"), term, NULL) );
            SCIPinfoMessage(scip, NULL, "\n");
#endif
         }
      }
      /* multiply c2 * sum1 */
      if( c2 != 0.0 )
      {
         int i;

         for( i = 0; i < nchildren1; ++i )
         {
            SCIP_CONSEXPR_EXPR* term;

            term = SCIPgetConsExprExprChildren(finalchildren->expr)[i];
            SCIP_CALL( SCIPappendConsExprExprSumExpr(scip, expanded, term,
                     SCIPgetConsExprExprSumCoefs(finalchildren->expr)[i] * c2 * simplifiedcoef) );
            /* we are just re-using a child here, so do not release term! */
#ifdef SIMPLIFY_DEBUG
            debugSimplify("Multiplying summand1_i * %f\n", c2);
            debugSimplify("summand1_i: \n");
            SCIP_CALL( SCIPprintConsExprExpr(scip, SCIPfindConshdlr(scip, "expr"), term, NULL) );
            SCIPinfoMessage(scip, NULL, "\n");
#endif
         }
      }
      /* multiply sum1 * sum2 without constants */
      for( j = 0; j < nchildren1; ++j )
      {
         SCIP_CONSEXPR_EXPR* factors[2];
         SCIP_Real coef1;

         coef1 = SCIPgetConsExprExprSumCoefs(finalchildren->expr)[j];
         factors[0] = SCIPgetConsExprExprChildren(finalchildren->expr)[j];
         for( k = 0; k < nchildren2; ++k )
         {
            EXPRNODE* finalfactors;
            SCIP_Real factorscoef;
            SCIP_Real coef2;
            SCIP_CONSEXPR_EXPR* term = NULL;
            SCIP_Bool dummy;

            coef2 = SCIPgetConsExprExprSumCoefs(finalchildren->next->expr)[k];
            factors[1] = SCIPgetConsExprExprChildren(finalchildren->next->expr)[k];

#ifdef SIMPLIFY_DEBUG
            debugSimplify("multiplying %g expr1 * %g expr2\n", coef1, coef2);
            debugSimplify("expr1:\n");
            SCIP_CALL( SCIPprintConsExprExpr(scip, SCIPfindConshdlr(scip, "expr"), factors[0], NULL) );
            SCIPinfoMessage(scip, NULL, "\n");
            debugSimplify("expr2\n");
            SCIP_CALL( SCIPprintConsExprExpr(scip, SCIPfindConshdlr(scip, "expr"), factors[1], NULL) );
            SCIPinfoMessage(scip, NULL, "\n");
#endif

            factorscoef = coef1 * coef2;
            SCIP_CALL( simplifyMultiplyChildren(scip, factors, 2, &factorscoef, &finalfactors, &dummy) );
            assert(factorscoef != 0.0);

#ifdef SIMPLIFY_DEBUG
            {
               EXPRNODE* node;
               int i;

               debugSimplify("Building product from simplified factors\n");
               node = finalfactors;
               i = 0;
               while( node != NULL )
               {
                  debugSimplify("factor %d (nuses %d):\n", i, SCIPgetConsExprExprNUses(node->expr));
                  SCIP_CALL( SCIPprintConsExprExpr(scip, SCIPfindConshdlr(scip, "expr"), node->expr, NULL) );
                  SCIPinfoMessage(scip, NULL, "\n");
                  node = node->next;
                  i++;
               }
            }
#endif

            SCIP_CALL( buildSimplifiedProduct(scip, 1.0, &finalfactors, TRUE, &term) );
            assert(finalfactors == NULL);
            assert(term != NULL);

#ifdef SIMPLIFY_DEBUG
            debugSimplify("%g expr1 * %g expr2 = %g * product\n", coef1, coef2, coef1 * coef2);
            debugSimplify("product: (nused %d)\n", SCIPgetConsExprExprNUses(term));
            SCIP_CALL( SCIPprintConsExprExpr(scip, SCIPfindConshdlr(scip, "expr"), term, NULL) );
            SCIPinfoMessage(scip, NULL, "\n");
#endif

            SCIP_CALL( SCIPappendConsExprExprSumExpr(scip, expanded, term, factorscoef * simplifiedcoef) );

            SCIP_CALL( SCIPreleaseConsExprExpr(scip, &term) );
         }
      }

      /* simplify the sum */
      SCIP_CALL( SCIPsimplifyConsExprExprHdlr(scip, expanded, simplifiedexpr) );
      SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expanded) );
 
      return SCIP_OKAY;
   }

   /* handle one sum case */
   if( strcmp(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(finalchildren->expr)), "sum") == 0 ||
         strcmp(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(finalchildren->next->expr)), "sum") == 0 )
   {
      SCIP_CONSEXPR_EXPR* expanded = NULL;
      SCIP_CONSEXPR_EXPR* factors[2];
      SCIP_CONSEXPR_EXPR* sum = NULL;
      SCIP_Real constant;
      int nchildren;
      int j;

      if( strcmp(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(finalchildren->expr)), "sum") == 0 )
      {
         assert(strcmp(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(finalchildren->next->expr)), "sum") != 0);
         sum = finalchildren->expr;
         factors[0] = finalchildren->next->expr;
      }
      else
      {
         assert(strcmp(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(finalchildren->expr)), "sum") != 0);
         sum = finalchildren->next->expr;
         factors[0] = finalchildren->expr;
      }
      constant = simplifiedcoef * SCIPgetConsExprExprSumConstant(sum);
      nchildren = SCIPgetConsExprExprNChildren(sum);

      SCIP_CALL( SCIPcreateConsExprExprSum(scip, SCIPfindConshdlr(scip, "expr"), &expanded,
               1, &factors[0], &constant,  0.0) );
      /* we are just re-using a child here, so do not release factor! */

      for( j = 0; j < nchildren; ++j )
      {
         SCIP_Real coef;
         SCIP_Real termcoef;
         SCIP_Bool dummy;
         EXPRNODE* finalfactors;
         SCIP_CONSEXPR_EXPR* term = NULL;

         coef = SCIPgetConsExprExprSumCoefs(sum)[j];
         factors[1] = SCIPgetConsExprExprChildren(sum)[j];

         termcoef = coef;
         SCIP_CALL( simplifyMultiplyChildren(scip, factors, 2, &termcoef, &finalfactors, &dummy) );
         assert(termcoef != 0.0);

         SCIP_CALL( buildSimplifiedProduct(scip, 1.0, &finalfactors, TRUE, &term) );
         assert(finalfactors == NULL);
         assert(term != NULL);

         SCIP_CALL( SCIPappendConsExprExprSumExpr(scip, expanded, term, termcoef * simplifiedcoef) );
         SCIP_CALL( SCIPreleaseConsExprExpr(scip, &term) );
      }

      /* simplify the sum */
      SCIP_CALL( SCIPsimplifyConsExprExprHdlr(scip, expanded, simplifiedexpr) );
      SCIP_CALL( SCIPreleaseConsExprExpr(scip, &expanded) );
   }

   return SCIP_OKAY;
}

/** builds a simplified product from simplifiedfactors
 * Note: this function also releases simplifiedfactors
 */
static
SCIP_RETCODE buildSimplifiedProduct(
   SCIP*                    scip,            /**< SCIP data structure */
   SCIP_Real                simplifiedcoef,  /**< simplified product should be simplifiedcoef * PI simplifiedfactors */
   EXPRNODE**               simplifiedfactors, /**< factors of simplified product */
   SCIP_Bool                changed,         /**< indicates whether some of the simplified factors was changed */
   SCIP_CONSEXPR_EXPR**     simplifiedexpr   /**< buffer to store the simplified expression */
   )
{
   EXPRNODE* finalchildren = *simplifiedfactors;

   /* build product expression from finalchildren and post-simplify */
   debugSimplify("[simplifyProduct] finalchildren has length %d\n", listLength(finalchildren)); /*lint !e506 !e681*/

   *simplifiedexpr = NULL;

   SCIP_CALL( enforceSP11(scip, simplifiedcoef, *simplifiedfactors, simplifiedexpr) );
   if( *simplifiedexpr != NULL ) goto CLEANUP;

   SCIP_CALL( enforceSP12(scip, simplifiedcoef, *simplifiedfactors, simplifiedexpr) );
   if( *simplifiedexpr != NULL ) goto CLEANUP;

   SCIP_CALL( enforceSP10(scip, simplifiedcoef, *simplifiedfactors, simplifiedexpr) );
   if( *simplifiedexpr != NULL ) goto CLEANUP;

   /* enforces SP8: if simplifiedcoef != 1.0, transform it into a sum with the (simplified) product as child */
   if( simplifiedcoef != 1.0 )
   {
      SCIP_CONSEXPR_EXPR* aux;

      SCIP_CALL( createExprProductFromExprlist(scip, finalchildren, 1.0, &aux) );
      SCIP_CALL( SCIPcreateConsExprExprSum(scip, SCIPfindConshdlr(scip, "expr"), simplifiedexpr,
               1, &aux, &simplifiedcoef, 0.0) );
      SCIP_CALL( SCIPreleaseConsExprExpr(scip, &aux) );
      goto CLEANUP;
   }

   /* build product expression from list */
   if( changed )
   {
      SCIP_CALL( createExprProductFromExprlist(scip, finalchildren, simplifiedcoef, simplifiedexpr) );
      goto CLEANUP;
   }

CLEANUP:

   SCIP_CALL( freeExprlist(scip, simplifiedfactors) );
   return SCIP_OKAY;
}

/*
 * Callback methods of expression handler
 */

/** simplifies a product expression
 *
 * Summary: we first build a list of expressions (called finalchildren) which will be the children of the simplified product
 * and then we process this list in order to enforce SP8 and SP10
 * Description: In order to build finalchildren, we first build list of unsimplified children (called unsimplifiedchildren)
 * with the children of the product. Each node of the list is manipulated (see simplifyFactor) in order to satisfy
 * SP2 and SP7 as follows
 * SP7: if the node's expression is a value, multiply the value to the products's coef
 * SP2: if the node's expression is a product, then build a list with the child's children
 * Then, we merge the built list (or the simplified node) into finalchildren. While merging, nodes from finalchildren
 * can go back to unsimplifiedchildren for further processing (see mergeProductExprlist for more details)
 * After building finalchildren, we create the simplified product out of it, taking care that SP8 and SP10 are satisfied
 */
static
SCIP_DECL_CONSEXPR_EXPRSIMPLIFY(simplifyProduct)
{  /*lint --e{715}*/
   EXPRNODE* finalchildren;
   SCIP_Real simplifiedcoef;
   SCIP_Bool changed;

   assert(expr != NULL);
   assert(simplifiedexpr != NULL);
   assert(strcmp(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(expr)), EXPRHDLR_NAME) == 0);

   simplifiedcoef = SCIPgetConsExprExprProductCoef(expr);

#ifdef SIMPLIFY_DEBUG
   debugSimplify("Simplifying expr:\n");
   SCIP_CALL( SCIPprintConsExprExpr(scip, SCIPfindConshdlr(scip, "expr"), expr, NULL) );
   SCIPinfoMessage(scip, NULL, "\n");
   debugSimplify("First multiplying children\n");
#endif

   /* simplify and multiply factors */
   SCIP_CALL( simplifyMultiplyChildren(scip, SCIPgetConsExprExprChildren(expr), SCIPgetConsExprExprNChildren(expr),
         &simplifiedcoef, &finalchildren, &changed) );

#ifdef SIMPLIFY_DEBUG
   {
      EXPRNODE* node;
      int i;

      debugSimplify("Building product from simplified factors\n");
      node = finalchildren;
      i = 0;
      while( node != NULL )
      {
         debugSimplify("factor %d:\n", i);
         SCIP_CALL( SCIPprintConsExprExpr(scip, SCIPfindConshdlr(scip, "expr"), node->expr, NULL) );
         SCIPinfoMessage(scip, NULL, "\n");
         node = node->next;
         i++;
      }
   }
#endif

   /* get simplified product from simplified factors in finalchildren */
   SCIP_CALL( buildSimplifiedProduct(scip, simplifiedcoef, &finalchildren, changed, simplifiedexpr) );
   assert(finalchildren == NULL);

   if( *simplifiedexpr == NULL )
   {
      *simplifiedexpr = expr;

      /* we have to capture it, since it must simulate a "normal" simplified call in which a new expression is created */
      SCIPcaptureConsExprExpr(*simplifiedexpr);
   }
   assert(*simplifiedexpr != NULL);

   return SCIP_OKAY;
}

/** the order of two product expressions, u and v, is a lexicographical order on the factors.
 *  Starting from the *last*, we find the first child where they differ, say, the i-th.
 *  Then u < v <=> u_i < v_i.
 *  If there is no such children and they have different number of children, then u < v <=> nchildren(u) < nchildren(v)
 *  If all children are the same and they have the same number of childre, u < v <=> coeff(u) < coeff(v)
 *  Otherwise, they are the same.
 *  Note: we are assuming expression are simplified, so within u, we have u_1 < u_2, etc
 *  Example: y * z < x * y * z
 */
static
SCIP_DECL_CONSEXPR_EXPRCOMPARE(compareProduct)
{  /*lint --e{715}*/
   int compareresult;
   int i;
   int j;
   int nchildren1;
   int nchildren2;
   SCIP_CONSEXPR_EXPR** children1;
   SCIP_CONSEXPR_EXPR** children2;

   nchildren1 = SCIPgetConsExprExprNChildren(expr1);
   nchildren2 = SCIPgetConsExprExprNChildren(expr2);
   children1 = SCIPgetConsExprExprChildren(expr1);
   children2 = SCIPgetConsExprExprChildren(expr2);

   for( i = nchildren1 - 1, j = nchildren2 - 1; i >= 0 && j >= 0; --i, --j )
   {
      compareresult = SCIPcompareConsExprExprs(children1[i], children2[j]);
      if( compareresult != 0 )
         return compareresult;
      /* expressions are equal, continue */
   }

   /* all children of one expression are children of the other expression, use number of children as a tie-breaker */
   if( i < j )
   {
      assert(i == -1);
      /* expr1 has less elements, hence expr1 < expr2 */
      return -1;
   }
   if( i > j )
   {
      assert(j == -1);
      /* expr1 has more elements, hence expr1 > expr2 */
      return 1;
   }

   /* everything is equal, use coefficient as tie-breaker */
   assert(i == -1 && j == -1);
   if( SCIPgetConsExprExprProductCoef(expr1) < SCIPgetConsExprExprProductCoef(expr2) )
      return -1;
   if( SCIPgetConsExprExprProductCoef(expr1) > SCIPgetConsExprExprProductCoef(expr2) )
      return 1;

   /* they are equal */
   return 0;
}

static
SCIP_DECL_CONSEXPR_EXPRCOPYHDLR(copyhdlrProduct)
{  /*lint --e{715}*/
   SCIP_CALL( SCIPincludeConsExprExprHdlrProduct(scip, consexprhdlr) );
   *valid = TRUE;

   return SCIP_OKAY;
}

/** expression handler free callback */
static
SCIP_DECL_CONSEXPR_EXPRFREEHDLR(freehdlrProduct)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(consexprhdlr != NULL);
   assert(exprhdlr != NULL);
   assert(exprhdlrdata != NULL);
   assert(*exprhdlrdata != NULL);

   /* free lp to separate product expressions */
   if( (*exprhdlrdata)->lpsize > 0 )
   {
      SCIP_CALL( SCIPlpiFree(&((*exprhdlrdata)->multilinearseparationlp)) );
      (*exprhdlrdata)->lpsize = 0;
   }
   assert((*exprhdlrdata)->lpsize == 0);
   assert((*exprhdlrdata)->multilinearseparationlp == NULL);

   /* free random number generator */
   SCIPfreeRandom(scip, &(*exprhdlrdata)->randnumgen);

   SCIPfreeBlockMemory(scip, exprhdlrdata);
   assert(*exprhdlrdata == NULL);

   return SCIP_OKAY;
}

static
SCIP_DECL_CONSEXPR_EXPRCOPYDATA(copydataProduct)
{  /*lint --e{715}*/
   SCIP_CONSEXPR_EXPRDATA* sourceexprdata;

   assert(targetexprdata != NULL);
   assert(sourceexpr != NULL);

   sourceexprdata = SCIPgetConsExprExprData(sourceexpr);
   assert(sourceexprdata != NULL);

   SCIP_CALL( createData(targetscip, targetexprdata, sourceexprdata->coefficient) );

   return SCIP_OKAY;
}

static
SCIP_DECL_CONSEXPR_EXPRFREEDATA(freedataProduct)
{  /*lint --e{715}*/
   SCIP_CONSEXPR_EXPRDATA* exprdata;

   assert(expr != NULL);

   exprdata = SCIPgetConsExprExprData(expr);
   assert(exprdata != NULL);

   SCIPfreeBlockMemory(scip, &exprdata);

   SCIPsetConsExprExprData(expr, NULL);

   return SCIP_OKAY;
}

static
SCIP_DECL_CONSEXPR_EXPRPRINT(printProduct)
{  /*lint --e{715}*/
   SCIP_CONSEXPR_EXPRDATA* exprdata;

   assert(expr != NULL);

   exprdata = SCIPgetConsExprExprData(expr);
   assert(exprdata != NULL);

   switch( stage )
   {
      case SCIP_CONSEXPRITERATOR_ENTEREXPR :
      {
         /* print opening parenthesis, if necessary */
         if( EXPRHDLR_PRECEDENCE <= parentprecedence )
         {
            SCIPinfoMessage(scip, file, "(");
         }

         /* print coefficient, if not one */
         if( exprdata->coefficient != 1.0 )
         {
            if( exprdata->coefficient < 0.0 && EXPRHDLR_PRECEDENCE > parentprecedence )
            {
               SCIPinfoMessage(scip, file, "(%g)", exprdata->coefficient);
            }
            else
            {
               SCIPinfoMessage(scip, file, "%g", exprdata->coefficient);
            }
         }
         break;
      }

      case SCIP_CONSEXPRITERATOR_VISITINGCHILD :
      {
         /* print multiplication sign, if not first factor */
         if( exprdata->coefficient != 1.0 || currentchild > 0 )
         {
            SCIPinfoMessage(scip, file, "*");
         }
         break;
      }

      case SCIP_CONSEXPRITERATOR_VISITEDCHILD :
      {
         break;
      }

      case SCIP_CONSEXPRITERATOR_LEAVEEXPR :
      {
         /* print closing parenthesis, if necessary */
         if( EXPRHDLR_PRECEDENCE <= parentprecedence )
         {
            SCIPinfoMessage(scip, file, ")");
         }
         break;
      }

      default:
         /* all stages should have been covered above */
         SCIPABORT();
   }

   return SCIP_OKAY;
}

/** product hash callback */
static
SCIP_DECL_CONSEXPR_EXPRHASH(hashProduct)
{  /*lint --e{715}*/
   SCIP_CONSEXPR_EXPRDATA* exprdata;
   int c;

   assert(scip != NULL);
   assert(expr != NULL);
   assert(hashkey != NULL);
   assert(childrenhashes != NULL);

   exprdata = SCIPgetConsExprExprData(expr);
   assert(exprdata != NULL);

   *hashkey = EXPRHDLR_HASHKEY;
   *hashkey ^= SCIPcalcFibHash(exprdata->coefficient);

   for( c = 0; c < SCIPgetConsExprExprNChildren(expr); ++c )
      *hashkey ^= childrenhashes[c];

   return SCIP_OKAY;
}

/** expression point evaluation callback */
static
SCIP_DECL_CONSEXPR_EXPREVAL(evalProduct)
{  /*lint --e{715}*/
   SCIP_CONSEXPR_EXPRDATA* exprdata;
   SCIP_Real childval;
   int c;

   assert(expr != NULL);

   exprdata = SCIPgetConsExprExprData(expr);
   assert(exprdata != NULL);

   *val = exprdata->coefficient;
   for( c = 0; c < SCIPgetConsExprExprNChildren(expr) && (*val != 0.0); ++c )
   {
      childval = SCIPgetConsExprExprValue(SCIPgetConsExprExprChildren(expr)[c]);
      assert(childval != SCIP_INVALID); /*lint !e777*/

      *val *= childval;
   }

   return SCIP_OKAY;
}

/** expression derivative evaluation callback */
static
SCIP_DECL_CONSEXPR_EXPRBWDIFF(bwdiffProduct)
{  /*lint --e{715}*/
   SCIP_CONSEXPR_EXPR* child;

   assert(expr != NULL);
   assert(SCIPgetConsExprExprData(expr) != NULL);
   assert(childidx >= 0 && childidx < SCIPgetConsExprExprNChildren(expr));

   child = SCIPgetConsExprExprChildren(expr)[childidx];
   assert(child != NULL);
   assert(strcmp(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(child)), "val") != 0);
   assert(SCIPgetConsExprExprValue(child) != SCIP_INVALID); /*lint !e777*/

   if( !SCIPisZero(scip, SCIPgetConsExprExprValue(child)) )
      *val = SCIPgetConsExprExprValue(expr) / SCIPgetConsExprExprValue(child);
   else
   {
      int i;

      *val = SCIPgetConsExprExprData(expr)->coefficient;
      for( i = 0; i < SCIPgetConsExprExprNChildren(expr) && (*val != 0.0); ++i )
      {
         if( i == childidx )
            continue;

         *val *= SCIPgetConsExprExprValue(SCIPgetConsExprExprChildren(expr)[i]);
      }
   }

   return SCIP_OKAY;
}

/** expression interval evaluation callback */
static
SCIP_DECL_CONSEXPR_EXPRINTEVAL(intevalProduct)
{  /*lint --e{715}*/
   SCIP_CONSEXPR_EXPRDATA* exprdata;
   int c;

   assert(expr != NULL);

   exprdata = SCIPgetConsExprExprData(expr);
   assert(exprdata != NULL);

   SCIPintervalSet(interval, exprdata->coefficient);

   for( c = 0; c < SCIPgetConsExprExprNChildren(expr); ++c )
   {
      SCIP_INTERVAL childinterval;

      childinterval = SCIPgetConsExprExprInterval(SCIPgetConsExprExprChildren(expr)[c]);
      assert(!SCIPintervalIsEmpty(SCIP_INTERVAL_INFINITY, childinterval));

      /* multiply childinterval with the so far computed interval */
      SCIPintervalMul(SCIP_INTERVAL_INFINITY, interval, *interval, childinterval);
   }

   return SCIP_OKAY;
}

/** separates a multilinear constraint of the form \f$ f(x) := a \Pi_{i = 1}^n x_i = w \f$ where \f$ x_i \f$ are the
 * auxiliary variables of the children and \f$ w \f$ is the auxiliary variable of expr. If \f$ f(x^*) > w^* \f$, then we
 * look for an affine underestimator of \f$ f(x) \f$ which separates \f$ (x^*, w^*) \f$ from the feasible region, i.e.
 * \f$ g(x) := \alpha^T x + \beta \le f(x) = w \f$ for all \f$ x \f$ in the domain, such that \f$ \alpha x^* > w^* \f$.
 *
 * Since \f$ f(x) \f$ is componentwise linear, its convex envelope is piecewise linear and its value can be computed by
 * finding the largest affine underestimator. This is done either explicitly (if n=2) or by solving an LP,
 * see SCIPcomputeFacetVertexPolyhedral().
 */
static
SCIP_DECL_CONSEXPR_EXPRESTIMATE(estimateProduct)
{
   SCIP_CONSEXPR_EXPRDATA* exprdata;
   SCIP_CONSEXPR_EXPR* child;
   SCIP_VAR* var;
   int nchildren;

   assert(scip != NULL);
   assert(conshdlr != NULL);
   assert(strcmp(SCIPconshdlrGetName(conshdlr), "expr") == 0);
   assert(expr != NULL);
   assert(strcmp(SCIPgetConsExprExprHdlrName(SCIPgetConsExprExprHdlr(expr)), EXPRHDLR_NAME) == 0);
   assert(coefs != NULL);
   assert(constant != NULL);
   assert(islocal != NULL);
   assert(success != NULL);

   exprdata = SCIPgetConsExprExprData(expr);
   assert(exprdata != NULL);

   *success = FALSE;

   nchildren = SCIPgetConsExprExprNChildren(expr);

   /* debug output: prints expression we are trying to estimate, bounds of variables and point */
#ifdef SCIP_DEBUG
   {
      int c;

      SCIPdebugMsg(scip, "%sestimating product with %d variables\n", overestimate ? "over": "under", SCIPgetConsExprExprNChildren(expr));
      for( c = 0; c < SCIPgetConsExprExprNChildren(expr); ++c )
      {
         child = SCIPgetConsExprExprChildren(expr)[c];
         var = SCIPgetConsExprExprAuxVar(child);
         assert(var != NULL);
         SCIPdebugMsg(scip, "var: %s = %g in [%g, %g]\n", SCIPvarGetName(var), SCIPgetSolVal(scip, sol, var),
               SCIPvarGetLbLocal(var), SCIPvarGetUbLocal(var));

         if( SCIPisInfinity(scip, SCIPvarGetUbLocal(var)) || SCIPisInfinity(scip, -SCIPvarGetLbLocal(var)) )
         {
            SCIPdebugMsg(scip, "unbounded factor related to\n");
            SCIP_CALL( SCIPdismantleConsExprExpr(scip, child) );
         }
      }
   }
#endif

   /* bilinear term */
   if( nchildren == 2 )
   {
      SCIP_VAR* x;
      SCIP_VAR* y;
      SCIP_Real refpointx;
      SCIP_Real refpointy;

      /* collect first variable */
      child = SCIPgetConsExprExprChildren(expr)[0];
      x = SCIPgetConsExprExprAuxVar(child);
      assert(x != NULL);

      /* collect second variable */
      child = SCIPgetConsExprExprChildren(expr)[1];
      y = SCIPgetConsExprExprAuxVar(child);
      assert(y != NULL);

      coefs[0] = 0.0;
      coefs[1] = 0.0;
      *constant = 0.0;
      *success = TRUE;
      *islocal = TRUE;

      refpointx = SCIPgetSolVal(scip, sol, x);
      refpointy = SCIPgetSolVal(scip, sol, y);

      /* adjust the reference points */
      refpointx = MIN(MAX(refpointx, SCIPvarGetLbLocal(x)),SCIPvarGetUbLocal(x)); /*lint !e666*/
      refpointy = MIN(MAX(refpointy, SCIPvarGetLbLocal(y)),SCIPvarGetUbLocal(y)); /*lint !e666*/
      assert(SCIPisLE(scip, refpointx, SCIPvarGetUbLocal(x)) && SCIPisGE(scip, refpointx, SCIPvarGetLbLocal(x)));
      assert(SCIPisLE(scip, refpointy, SCIPvarGetUbLocal(y)) && SCIPisGE(scip, refpointy, SCIPvarGetLbLocal(y)));

      SCIPaddBilinMcCormick(scip, exprdata->coefficient, SCIPvarGetLbLocal(x), SCIPvarGetUbLocal(x), refpointx,
            SCIPvarGetLbLocal(y), SCIPvarGetUbLocal(y), refpointy, overestimate, &coefs[0], &coefs[1], constant,
            success);

      return SCIP_OKAY;
   }
   else
   {
      SCIP_Real* box;
      SCIP_Real* xstar;
      int i;

      /* Since the product is componentwise linear, its convex and concave envelopes are piecewise linear.*/

      /* assemble box, check for unbounded variables, assemble xstar */
      SCIP_CALL( SCIPallocBufferArray(scip, &box, 2*nchildren) );
      SCIP_CALL( SCIPallocBufferArray(scip, &xstar, nchildren) );
      for( i = 0; i < nchildren; ++i )
      {
         child = SCIPgetConsExprExprChildren(expr)[i];
         var = SCIPgetConsExprExprAuxVar(child);

         if( SCIPisInfinity(scip, SCIPvarGetUbLocal(var)) || SCIPisInfinity(scip, -SCIPvarGetLbLocal(var)) )
         {
            SCIPdebugMsg(scip, "a factor is unbounded, no cut is possible\n");
            goto CLEANUP;
         }

         box[2*i] = SCIPvarGetLbLocal(var);
         box[2*i+1] = SCIPvarGetUbLocal(var);

         xstar[i] = SCIPgetSolVal(scip, sol, var);
      }

      SCIP_CALL( SCIPcomputeFacetVertexPolyhedral(scip, conshdlr, overestimate, prodfunction, &exprdata->coefficient, xstar, box, nchildren, targetvalue, success, coefs, constant) );

CLEANUP:
      SCIPfreeBufferArray(scip, &xstar);
      SCIPfreeBufferArray(scip, &box);
   }

   return SCIP_OKAY;
}

/** expression reverse propagation callback */
static
SCIP_DECL_CONSEXPR_EXPRREVERSEPROP(reversepropProduct)
{  /*lint --e{715}*/
   SCIP_CONSEXPR_EXPRDATA* exprdata;
   SCIP_INTERVAL childbounds;
   SCIP_INTERVAL otherfactor;
   SCIP_INTERVAL zero;
   int i;
   int j;

   assert(scip != NULL);
   assert(expr != NULL);
   assert(SCIPgetConsExprExprNChildren(expr) > 0);
   assert(infeasible != NULL);
   assert(nreductions != NULL);

   *nreductions = 0;
   *infeasible = FALSE;

   /* too expensive (runtime here is quadratic in number of children)
    * TODO implement something faster for larger numbers of factors, e.g., split product into smaller products
    */
   if( SCIPgetConsExprExprNChildren(expr) > 10 )
      return SCIP_OKAY;

   /* not possible to learn bounds if expression interval is unbounded in both directions */
   if( SCIPintervalIsEntire(SCIP_INTERVAL_INFINITY, SCIPgetConsExprExprInterval(expr)) )
      return SCIP_OKAY;

   exprdata = SCIPgetConsExprExprData(expr);
   assert(exprdata != NULL);

   SCIPintervalSet(&zero, 0.0);

   /* f = const * prod_k c_k => c_i solves c_i * (const * prod_{j:j!=i} c_j) = f */
   for( i = 0; i < SCIPgetConsExprExprNChildren(expr) && !(*infeasible); ++i )
   {
      SCIPintervalSet(&otherfactor, exprdata->coefficient);

      /* compute prod_{j:j!=i} c_j */
      for( j = 0; j < SCIPgetConsExprExprNChildren(expr); ++j )
      {
         if( i == j )
            continue;

         SCIPintervalMul(SCIP_INTERVAL_INFINITY, &otherfactor, otherfactor,
               SCIPgetConsExprExprInterval(SCIPgetConsExprExprChildren(expr)[j]));
      }

      /* solve x*otherfactor = f for x in c_i */
      SCIPintervalSolveUnivariateQuadExpression(SCIP_INTERVAL_INFINITY, &childbounds, zero, otherfactor,
         SCIPgetConsExprExprInterval(expr), SCIPgetConsExprExprInterval(SCIPgetConsExprExprChildren(expr)[i]));

      /* try to tighten the bounds of the expression */
      SCIP_CALL( SCIPtightenConsExprExprInterval(scip, SCIPgetConsExprExprChildren(expr)[i], childbounds, force, reversepropqueue,
         infeasible, nreductions) );
   }

   return SCIP_OKAY;
}

/** expression curvature detection callback */
static
SCIP_DECL_CONSEXPR_EXPRCURVATURE(curvatureProduct)
{  /*lint --e{715}*/
   assert(scip != NULL);
   assert(expr != NULL);
   assert(curvature != NULL);
   assert(SCIPgetConsExprExprNChildren(expr) > 1);
   assert(SCIPgetConsExprExprProductCoef(expr) == 1.0);

   *curvature = SCIP_EXPRCURV_UNKNOWN;

   return SCIP_OKAY;
}

/** expression monotonicity detection callback */
static
SCIP_DECL_CONSEXPR_EXPRMONOTONICITY(monotonicityProduct)
{  /*lint --e{715}*/
   SCIP_Real coef;
   int i;
   int nneg;

   assert(scip != NULL);
   assert(expr != NULL);
   assert(result != NULL);
   assert(SCIPgetConsExprExprNChildren(expr) >= 1);
   assert(childidx >= 0 && childidx < SCIPgetConsExprExprNChildren(expr));

   coef = SCIPgetConsExprExprProductCoef(expr);

   /* count the number of negative children (except for childidx); if some children changes sign -> monotonicity unknown */
   nneg = 0;
   for( i = 0; i < SCIPgetConsExprExprNChildren(expr); ++i )
   {
      SCIP_INTERVAL interval;

      if( i == childidx )
         continue;

      assert(SCIPgetConsExprExprChildren(expr)[i] != NULL);
      interval = SCIPgetConsExprExprInterval(SCIPgetConsExprExprChildren(expr)[i]);

      if( SCIPintervalGetSup(interval) <= 0.0 )
         nneg++;
      else if( SCIPintervalGetInf(interval) < 0.0 )
      {
         *result = SCIP_MONOTONE_UNKNOWN;
         return SCIP_OKAY;
      }
   }

   /* note that the monotonicity depends on the sign of the coefficient */
   if( nneg % 2 == 0 )
      *result = (coef >= 0.0) ? SCIP_MONOTONE_INC : SCIP_MONOTONE_DEC;
   else
      *result = (coef >= 0.0) ? SCIP_MONOTONE_DEC : SCIP_MONOTONE_INC;

   return SCIP_OKAY;
}

/** expression integrality detection callback */
static
SCIP_DECL_CONSEXPR_EXPRINTEGRALITY(integralityProduct)
{  /*lint --e{715}*/
   SCIP_CONSEXPR_EXPRDATA* exprdata;
   int i;

   assert(scip != NULL);
   assert(expr != NULL);
   assert(isintegral != NULL);

   exprdata = SCIPgetConsExprExprData(expr);
   assert(exprdata != NULL);

   *isintegral = EPSISINT(exprdata->coefficient, 0.0); /*lint !e835*/

   for( i = 0; i < SCIPgetConsExprExprNChildren(expr) && *isintegral; ++i )
   {
      SCIP_CONSEXPR_EXPR* child = SCIPgetConsExprExprChildren(expr)[i];
      assert(child != NULL);

      *isintegral = SCIPisConsExprExprIntegral(child);
   }

   return SCIP_OKAY;
}

/** creates the handler for product expressions and includes it into the expression constraint handler */
SCIP_RETCODE SCIPincludeConsExprExprHdlrProduct(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        consexprhdlr        /**< expression constraint handler */
   )
{
   SCIP_CONSEXPR_EXPRHDLRDATA* exprhdlrdata;
   SCIP_CONSEXPR_EXPRHDLR* exprhdlr;

   /* allocate expression handler data */
   SCIP_CALL( SCIPallocBlockMemory(scip, &exprhdlrdata) );

   /* initialize all data in exprhdlrdata to 0/NULL */
   BMSclearMemory(exprhdlrdata);

   /* create/initialize random number generator */
   /* TODO FIXME: we need an INITSOL callback so that calls like SCIPsolve() SCIPfreeTransform() and then SCIPsolve()
    * again behave the same; right now, the initial seed is set when SCIP include the plugin, but this happens only once
    */
   SCIP_CALL( SCIPcreateRandom(scip, &exprhdlrdata->randnumgen, DEFAULT_RANDSEED, TRUE) );

   SCIP_CALL( SCIPincludeConsExprExprHdlrBasic(scip, consexprhdlr, &exprhdlr, EXPRHDLR_NAME, EXPRHDLR_DESC,
            EXPRHDLR_PRECEDENCE, evalProduct, exprhdlrdata) );
   assert(exprhdlr != NULL);

   SCIP_CALL( SCIPsetConsExprExprHdlrCopyFreeHdlr(scip, consexprhdlr, exprhdlr, copyhdlrProduct, freehdlrProduct) );
   SCIP_CALL( SCIPsetConsExprExprHdlrCopyFreeData(scip, consexprhdlr, exprhdlr, copydataProduct, freedataProduct) );
   SCIP_CALL( SCIPsetConsExprExprHdlrSimplify(scip, consexprhdlr, exprhdlr, simplifyProduct) );
   SCIP_CALL( SCIPsetConsExprExprHdlrCompare(scip, consexprhdlr, exprhdlr, compareProduct) );
   SCIP_CALL( SCIPsetConsExprExprHdlrPrint(scip, consexprhdlr, exprhdlr, printProduct) );
   SCIP_CALL( SCIPsetConsExprExprHdlrIntEval(scip, consexprhdlr, exprhdlr, intevalProduct) );
   SCIP_CALL( SCIPsetConsExprExprHdlrSepa(scip, consexprhdlr, exprhdlr, NULL, NULL, NULL, estimateProduct) );
   SCIP_CALL( SCIPsetConsExprExprHdlrReverseProp(scip, consexprhdlr, exprhdlr, reversepropProduct) );
   SCIP_CALL( SCIPsetConsExprExprHdlrHash(scip, consexprhdlr, exprhdlr, hashProduct) );
   SCIP_CALL( SCIPsetConsExprExprHdlrBwdiff(scip, consexprhdlr, exprhdlr, bwdiffProduct) );
   SCIP_CALL( SCIPsetConsExprExprHdlrCurvature(scip, consexprhdlr, exprhdlr, curvatureProduct) );
   SCIP_CALL( SCIPsetConsExprExprHdlrMonotonicity(scip, consexprhdlr, exprhdlr, monotonicityProduct) );
   SCIP_CALL( SCIPsetConsExprExprHdlrIntegrality(scip, consexprhdlr, exprhdlr, integralityProduct) );

   return SCIP_OKAY;
}

/** creates a product expression */
SCIP_RETCODE SCIPcreateConsExprExprProduct(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSHDLR*        consexprhdlr,       /**< expression constraint handler */
   SCIP_CONSEXPR_EXPR**  expr,               /**< pointer where to store expression */
   int                   nchildren,          /**< number of children */
   SCIP_CONSEXPR_EXPR**  children,           /**< children */
   SCIP_Real             coefficient         /**< constant coefficient of product */
   )
{
   SCIP_CONSEXPR_EXPRDATA* exprdata;

   SCIP_CALL( createData(scip, &exprdata, coefficient) );

   SCIP_CALL( SCIPcreateConsExprExpr(scip, expr, SCIPgetConsExprExprHdlrProduct(consexprhdlr), exprdata, nchildren, children) );

   return SCIP_OKAY;
}

/** gets the constant coefficient of a product expression */
SCIP_Real SCIPgetConsExprExprProductCoef(
   SCIP_CONSEXPR_EXPR*   expr                /**< product expression */
   )
{
   SCIP_CONSEXPR_EXPRDATA* exprdata;

   assert(expr != NULL);

   exprdata = SCIPgetConsExprExprData(expr);
   assert(exprdata != NULL);

   return exprdata->coefficient;
}

/** appends an expression to a product expression */
SCIP_RETCODE SCIPappendConsExprExprProductExpr(
   SCIP*                 scip,               /**< SCIP data structure */
   SCIP_CONSEXPR_EXPR*   expr,               /**< product expression */
   SCIP_CONSEXPR_EXPR*   child               /**< expression to be appended */
   )
{
   assert(expr != NULL);

   SCIP_CALL( SCIPappendConsExprExpr(scip, expr, child) );

   return SCIP_OKAY;
}
