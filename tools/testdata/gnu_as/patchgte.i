# 1 "INPUT.S"
 
































# 1 "E:\\/asm.h" 1
 


 











































































 







































 




































# 34 "INPUT.S" 2


	.set	noreorder	 
	.set	noat		 
	.set	nomacro		 

 




 




 







	.section	.sbss,"aw",@nobits
g_8008d304:
	.space	16

	.text

 















	.globl	_patch_gte
_patch_gte:
	 
	lui	$at	, %hi(g_8008d304)	 
	sw	$31	, %lo(g_8008d304)($at	)	 
	jal	EnterCriticalSection	 
	nop				 
	addiu	$10 , $0	, 0xb0		 
	jalr	$10 			 
	addiu	$9 , $0	, 0x56		 
	 

	lui	$26	, %hi($stub)
	lui	$27 , %hi($stub_end)
	lw	$2	, 0x18	($2	)	 
	addiu	$26	, $26	, %lo($stub)	 
	addiu	$27 , $27 , %lo($stub_end)	 
$copy:
	 

	lw	$3 , 0($26	)		 
	addiu	$26	, $26	, 4		 
	addiu	$2	, $2	, 4		 
	bne	$26	, $27 , $copy		 
	sw	$3 , -4($2	)		 
	jal	FlushCache		 
	nop				 
	jal	ExitCriticalSection	 
	nop				 
	lui	$31	, %hi(g_8008d304)	 
	lw	$31	, %lo(g_8008d304)($31	)
	nop				 
	jr	$31	
	nop				 

 











$stub:
	nop				 
	nop
	addiu	$26	, $0	, 0x100		 
	lw	$26	, 	0x08	($26	)		 
	nop				 
	lw	$26	, 0($26	)		 
	nop				 
	addi	$26	, $26	, 0x08		 
	sw	$at	, 4($26	)		 
	sw	$2	, 8($26	)		 
	sw	$3 , 0xc($26	)		 
	sw	$31	, 0x7c($26	)		 
	mfc0	$2	, $13			 
	nop				 
$stub_end:
