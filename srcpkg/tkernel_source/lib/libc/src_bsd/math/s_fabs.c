/* @(#)s_fabs.c 5.1 93/09/24 */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice 
 * is preserved.
 * ====================================================
 */

/*
 * fabs(x) returns the absolute value of x.
 */

/* LINTLIBRARY */

#include <sys/cdefs.h>
#include <float.h>
#include <math.h>

#include "math_private.h"

double
fabs(double x)
{
	u_int32_t high;
	GET_HIGH_WORD(high,x);
	SET_HIGH_WORD(x,high&0x7fffffff);
        return x;
}

#if	LDBL_MANT_DIG == 53
#ifdef	lint
/* PROTOLIB1 */
long double fabsl(long double);
#else	/* lint */
__weak_alias(fabsl, fabs);
#endif	/* lint */
#endif	/* LDBL_MANT_DIG == 53 */
