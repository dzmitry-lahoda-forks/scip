/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*                                                                           */
/*                  This file is part of the program and library             */
/*         SCIP --- Solving Constraint Integer Programs                      */
/*                                                                           */
/*    Copyright (C) 2002-2009 Konrad-Zuse-Zentrum                            */
/*                            fuer Informationstechnik Berlin                */
/*                                                                           */
/*  SCIP is distributed under the terms of the ZIB Academic License.         */
/*                                                                           */
/*  You should have received a copy of the ZIB Academic License              */
/*  along with SCIP; see the file COPYING. If not email to scip@zib.de.      */
/*                                                                           */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
#pragma ident "@(#) $Id: branch_allfullstrong.h,v 1.10.2.1 2009/06/19 07:53:38 bzfwolte Exp $"

/**@file   branch_allfullstrong.h
 * @brief  all variables full strong LP branching rule
 * @author Tobias Achterberg
 */

/*---+----1----+----2----+----3----+----4----+----5----+----6----+----7----+----8----+----9----+----0----+----1----+----2*/

#ifndef __SCIP_BRANCH_ALLFULLSTRONG_H__
#define __SCIP_BRANCH_ALLFULLSTRONG_H__


#include "scip/scip.h"


/** creates the all variables full strong LP braching rule and includes it in SCIP */
extern
SCIP_RETCODE SCIPincludeBranchruleAllfullstrong(
   SCIP*                 scip                /**< SCIP data structure */
   );

#endif
