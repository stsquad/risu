/******************************************************************************
 * Copyright (c) IBM Corp, 2016
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors:
 *     Jose Ricardo Ziviani - initial implementation
 *     based on Claudio Fontana's risu_reginfo_aarch64
 *     based on Peter Maydell's risu_arm.c
 *****************************************************************************/

#ifndef RISU_REGINFO_PPC64LE_H
#define RISU_REGINFO_PPC64LE_H

struct reginfo {
    uint32_t faulting_insn;
    uint32_t prev_insn;
    uint64_t nip;
    uint64_t prev_addr;
    gregset_t gregs;
    uint64_t fpregs[32];
    uint64_t fpscr;
    vrregset_t vrregs;
};

#endif /* RISU_REGINFO_PPC64LE_H */
