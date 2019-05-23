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

#ifdef __x86_64__
#define BASE	%rax
	lea	2f(%rip), BASE
#else
#define BASE	%eax
	call	1f
1:	pop	BASE
	add	$2f-1b, BASE
#endif

	movdqa	0(BASE), %xmm0
	movdqa	1*16(BASE), %xmm1
	movdqa	2*16(BASE), %xmm2
	movdqa	3*16(BASE), %xmm3
	movdqa	4*16(BASE), %xmm4
	movdqa	5*16(BASE), %xmm5
	movdqa	6*16(BASE), %xmm6
	movdqa	7*16(BASE), %xmm7

#ifdef __x86_64__
	movdqa	8*16(BASE), %xmm8
	movdqa	9*16(BASE), %xmm9
	movdqa	10*16(BASE), %xmm10
	movdqa	11*16(BASE), %xmm11
	movdqa	12*16(BASE), %xmm12
	movdqa	13*16(BASE), %xmm13
	movdqa	14*16(BASE), %xmm14
	movdqa	15*16(BASE), %xmm15
#endif

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

	.p2align 16
2:
	.set	i, 0
	.rept	256
	.byte	i
	.set	i, i + 1
	.endr
