/*#############################################################################
 * Copyright (c) 2010 Linaro Limited
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors:
 *     Peter Maydell (Linaro) - initial implementation
 *###########################################################################*/

/* A trivial test image for x86 */

/* Initialise the registers to avoid spurious mismatches */
	xor	%eax, %eax
	sahf				/* init eflags */

	mov	$0x12345678, %eax
	mov	$0x9abcdef0, %ebx
	mov	$0x97361234, %ecx
	mov	$0x84310284, %edx
	mov	$0x83624173, %edi
	mov	$0xfaebfaeb, %esi
	mov	$0x84610123, %ebp

#ifdef __x86_64__
	movq	$0x123456789abcdef0, %r8
	movq	$0xaaaabbbbccccdddd, %r9
	movq	$0x1010101010101010, %r10
	movq	$0x1111111111111111, %r11
	movq	$0x1212121212121212, %r12
	movq	$0x1313131313131313, %r13
	movq	$0x1414141414141414, %r14
	movq	$0x1515151515151515, %r15
#endif

/* do compare */
	ud1	%eax, %eax

/* exit test */
	ud1	%ecx, %eax
