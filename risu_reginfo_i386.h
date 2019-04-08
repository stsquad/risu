/*******************************************************************************
 * Copyright (c) 2010 Linaro Limited
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors:
 *     Peter Maydell (Linaro) - initial implementation
 ******************************************************************************/

#ifndef RISU_REGINFO_I386_H
#define RISU_REGINFO_I386_H

/* This is the data structure we pass over the socket.
 * It is a simplified and reduced subset of what can
 * be obtained with a ucontext_t*
 */
struct reginfo {
    uint32_t faulting_insn;
    gregset_t gregs;
};

#ifndef REG_GS
/* Assume that either we get all these defines or none */
#   define REG_GS      0
#   define REG_FS      1
#   define REG_ES      2
#   define REG_DS      3
#   define REG_ESP     7
#   define REG_EAX    11
#   define REG_TRAPNO 12
#   define REG_EIP    14
#   define REG_EFL    16
#   define REG_UESP   17
#endif /* !defined(REG_GS) */

#endif /* RISU_REGINFO_I386_H */
