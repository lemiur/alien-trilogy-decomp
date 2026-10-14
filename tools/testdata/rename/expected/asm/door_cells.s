/*
 * door_cells.s - reads g_doorRecords for Door_Build's caller.
 */
#include "gte.h"

	.text
	.globl	door_cells
door_cells:
	lw	t0, g_doorRecords	/* t0 = first record (macro: lui + lw) */
	jal	Door_Build
	nop
