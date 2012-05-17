; Inputs: eax = size of allocated array
;         edx = fill value of array
;
; Output: array of size eax filled with ebx
;
; Follows the L1 calling conventions

allocate:
	; Allocate scratch space
	subl	$8, %esp

	; Save the values of esi and edi to the stack
	movl	%esi, -4(%ebp)
	movl	%edi, -8(%ebp)

	; Push the arguments of allocate_gc
	pushl	%ebp	; Address of array of esi and edi values
	pushl	%ebp	; Top of L1 stack
	pushl	%edx	; Fill value
	pushl	%eax	; Size of array

	; Call allocate_gc and fix stack pointer
	call 	allocate_gc
	addl	$8, %esp

	; Write back update values to esi and edi
	movl	-4(%ebp), %esi
	movl	-8(%ebp), %edi

	; Free scratch space
	addl	$8, %esp

	; Return
	movl 	%ebp, %esp
	popl 	%ebp
	ret
