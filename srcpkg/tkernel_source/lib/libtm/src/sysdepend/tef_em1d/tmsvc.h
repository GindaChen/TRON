/*
 *----------------------------------------------------------------------
 *    T-Kernel 2.0 Software Package
 *
 *    Copyright 2011 by Ken Sakamura.
 *    This software is distributed under the T-License 2.0.
 *----------------------------------------------------------------------
 *
 *    Released by T-Engine Forum(http://www.t-engine.org/) at 2011/05/17.
 *
 *----------------------------------------------------------------------
 */

/*
 *	@(#)tmsvc.h (libtm/EM1-D512)
 *
 *	T-Engine Reference Board definitions
 *
 *	* Used by assembler
 */

#include <tk/sysdef.h>

/*
 * T-Monitor service call
 */
.macro _TMCALL func, fno
	.text
	.balign	4
	.globl	Csym(\func)
	.type	Csym(\func), %function
Csym(\func):
	stmfd	sp!, {lr}
	ldr	ip, =\fno
	swi	SWI_MONITOR
	ldmfd	sp!, {lr}
	bx	lr
.endm

#define TMCALL(func, fno)	_TMCALL func, fno
