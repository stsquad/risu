/*****************************************************************************
 * Copyright (c) 2013 Linaro Limited
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors:
 *     Claudio Fontana (Linaro) - initial implementation
 *     based on test_arm.s by Peter Maydell
 *****************************************************************************/

/* Initialise the gp regs */
mov w0, 0
mov w1, 1
mov w2, 2
mov w3, 3
mov w4, 4
mov w5, 5
mov w6, 6
mov w7, 7
mov w8, 8
mov w9, 9
mov w10, 10
mov w11, 11
mov w12, 12
mov w13, 13
mov w14, 14
mov w15, 15
mov w16, 16
mov w17, 17
mov w18, 18
mov w19, 19
mov w20, 20
mov w21, 21
mov w22, 22
mov w23, 23
mov w24, 24
mov w25, 25
mov w26, 26
mov w27, 27
mov w28, 28
mov w29, 29
mov w30, 30

/* Initialise the fp regs */
movi d0, #0
fmov d1, #1.0
fmov d2, #2.0
fmov d3, #3.0
fmov d4, #4.0
fmov d5, #5.0
fmov d6, #6.0
fmov d7, #7.0
fmov d8, #8.0
fmov d9, #9.0
fmov d10, #10.0
fmov d11, #11.0
fmov d12, #12.0
fmov d13, #13.0
fmov d14, #14.0
fmov d15, #15.0
fmov d16, #16.0
fmov d17, #17.0
fmov d18, #18.0
fmov d19, #19.0
fmov d20, #20.0
fmov d21, #21.0
fmov d22, #22.0
fmov d23, #23.0
fmov d24, #24.0
fmov d25, #25.0
fmov d26, #26.0
fmov d27, #27.0
fmov d28, #28.0
fmov d29, #29.0
fmov d30, #30.0
fmov d31, #31.0

/* do compare.
 * The manual says instr with bits (28,27) == 0 0 are UNALLOCATED
 */
.int 0x00005af0
/* exit test */
.int 0x00005af1
