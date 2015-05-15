/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2015 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/**@file    nlpi_ipopt_dummy.c
 * @brief   dummy Ipopt NLP interface for the case that Ipopt is not available
 * @author  Stefan Vigerske
 * @author  Benjamin Müller
 *
 * This code has been separate from nlpi_ipopt.cpp, so the SCIP build system recognizes it as pure C code,
 * thus the linker does not need to be changed to C++.
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#include "scip/pub_message.h"
#include "nlpi/nlpi_ipopt.h"

/** create solver interface for Ipopt solver */
SCIP_RETCODE SCIPcreateNlpSolverIpopt(
   BMS_BLKMEM*           blkmem,             /**< block memory data structure */
   SCIP_NLPI**           nlpi                /**< pointer to buffer for nlpi address */
   )
{
   assert(nlpi != NULL);

   *nlpi = NULL;

   return SCIP_OKAY;
}  /*lint !e715*/

/** gets string that identifies Ipopt (version number) */
const char* SCIPgetSolverNameIpopt(void)
{
   return "";
}

/** gets string that describes Ipopt (version number) */
const char* SCIPgetSolverDescIpopt(void)
{
   return "";
}

/** returns whether Ipopt is available, i.e., whether it has been linked in */
SCIP_Bool SCIPisIpoptAvailableIpopt(void)
{
   return FALSE;
}

/** gives a pointer to the IpoptApplication object stored in Ipopt-NLPI's NLPI problem data structure */
void* SCIPgetIpoptApplicationPointerIpopt(
   SCIP_NLPIPROBLEM*     nlpiproblem         /**< NLP problem of Ipopt-NLPI */
   )
{
   SCIPerrorMessage("Ipopt not available!\n");
   SCIPABORT();
   return NULL;  /*lint !e527*/
}  /*lint !e715*/

/** gives a pointer to the NLPIORACLE object stored in Ipopt-NLPI's NLPI problem data structure */
void* SCIPgetNlpiOracleIpopt(
   SCIP_NLPIPROBLEM*     nlpiproblem         /**< NLP problem of Ipopt-NLPI */
   )
{
   SCIPerrorMessage("Ipopt not available!\n");
   SCIPABORT();
   return NULL;  /*lint !e527*/
}  /*lint !e715*/

/** sets modified default settings that are used when setting up an Ipopt problem
 *
 * Do not forget to add a newline after the last option in optionsstring.
 */
void SCIPsetModifiedDefaultSettingsIpopt(
   SCIP_NLPI*            nlpi,               /**< Ipopt NLP interface */
   const char*           optionsstring       /**< string with options as in Ipopt options file */
   )
{
   SCIPerrorMessage("Ipopt not available!\n");
   SCIPABORT();
}  /*lint !e715*/

/** Calls Lapacks Dsyev routine to compute eigenvalues and eigenvectors of a dense matrix. 
 * It's here, because Ipopt is linked against Lapack.
 */
SCIP_RETCODE LapackDsyev(
   SCIP_Bool             computeeigenvectors,/**< should also eigenvectors should be computed ? */
   int                   N,                  /**< dimension */
   SCIP_Real*            a,                  /**< matrix data on input (size N*N); eigenvectors on output if computeeigenvectors == TRUE */
   SCIP_Real*            w                   /**< buffer to store eigenvalues (size N) */
   )
{
   SCIPerrorMessage("Ipopt not available, cannot use it's Lapack link!\n");
   return SCIP_ERROR;
}  /*lint !e715*/

/* solves a linear problem of the form Ax = b */
SCIP_RETCODE SCIPsolveLinearProb(
   int                   N,                  /**< dimension */
   SCIP_Real**           a,                  /**< matrix data on input (size N*N) */
   SCIP_Real*            b,                  /**< right hand side vector (size N) */
   SCIP_Real*            x,                  /**< buffer to store solution (size N) */
   SCIP_Bool*            success             /**< pointer to store if the solving routine was successful */
   )
{
   SCIP_Real** LU;
   SCIP_Real* y;
   int* pivot;
   int k;

   assert(N > 0);
   assert(a != NULL);
   assert(b != NULL);
   assert(x != NULL);
   assert(success != NULL);

   *success = TRUE;

   /* copy arrays */
   SCIP_ALLOC( BMSallocMemoryArray(&LU, N) );
   SCIP_ALLOC( BMSallocMemoryArray(&y, N) );
   SCIP_ALLOC( BMSallocMemoryArray(&pivot, N) );

   /* initialize values */
   for( k = 0; k < N; ++k )
   {
      int j;
      SCIP_ALLOC( BMSallocMemoryArray(&LU[k], N) );
      for( j = 0; j < N; ++j )
         LU[k][j] = a[k][j];

      y[k] = b[k];
      pivot[k] = k;
   }

   /* first step: compute LU factorization */
   for( k = 0; k < N; ++k )
   {
      int p;
      int i;

      p = k;
      for( i = k+1; i < N; ++i )
      {
         if( ABS( LU[pivot[i]][k] ) > ABS( LU[pivot[p]][k] ) )
            p = i;
      }

      if( ABS(LU[pivot[p]][k]) < 1e-08 )
      {
         SCIPerrorMessage("Error in nlpi_ipopt_dummy - matrix is singular!\n");
         *success = FALSE;
         goto TERMINATE;
      }

      if( p != k )
      {
         int tmp;

         tmp = pivot[k];
         pivot[k] = pivot[p];
         pivot[p] = tmp;
      }

      for( i = k+1; i < N; ++i )
      {
         SCIP_Real m;
         int j;

         m = LU[pivot[i]][k] / LU[pivot[k]][k];

         for( j = k+1; j < N; ++j )
            LU[pivot[i]][j] -= m * LU[pivot[k]][j];

         LU[pivot[i]][k] = m;
      }
   }

   /* second step: forward substitution */
   y[0] = b[pivot[0]];

   for( k = 1; k < N; ++k )
   {
      SCIP_Real s;
      int j;

      s = b[pivot[k]];
      for( j = 0; j < k; ++j )
      {
         s -= LU[pivot[k]][j] * y[j];
      }
      y[k] = s;
   }

   /* third step: backward substitution */
   x[N-1] = y[N-1] / LU[pivot[N-1]][N-1];
   for( k = N-2; k >= 0; --k )
   {
      SCIP_Real s;
      int j;

      s = y[k];
      for( j = k+1; j < N; ++j )
      {
         s -= LU[pivot[k]][j] * x[j];
      }
      x[k] = s / LU[pivot[k]][k];
   }

   TERMINATE:
   /* free arrays */
   for( k = 0; k < N; ++k )
      BMSfreeMemoryArray(&LU[k]);

   BMSfreeMemoryArray(&pivot);
   BMSfreeMemoryArray(&LU);
   BMSfreeMemoryArray(&y);

   return SCIP_OKAY;
}
