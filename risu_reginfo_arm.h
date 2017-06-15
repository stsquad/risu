/*****************************************************************************
 * Copyright (c) 2013 Linaro Limited
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors:
 *     Peter Maydell (Linaro) - initial implementation
 *     Claudio Fontana (Linaro) - minor refactoring
 *****************************************************************************/

#ifndef RISU_REGINFO_ARM_H
#define RISU_REGINFO_ARM_H

struct reginfo {
    uint64_t fpregs[32];
    uint32_t faulting_insn;
    uint32_t faulting_insn_size;
    uint32_t gpreg[16];
    uint32_t cpsr;
    uint32_t fpscr;
};

#endif /* RISU_REGINFO_ARM_H */
