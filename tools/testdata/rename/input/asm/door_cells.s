/*
 * door_cells.s - reads g_door_records for doors_build's caller.
 */
#include "gte.h"

	.text
	.globl	door_cells
door_cells:
	lw	t0, g_door_records	/* t0 = first record (macro: lui + lw) */
	jal	doors_build
	nop
