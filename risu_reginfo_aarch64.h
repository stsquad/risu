/******************************************************************************
 * Copyright (c) 2013 Linaro Limited
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors:
 *     Claudio Fontana (Linaro) - initial implementation
 *     based on Peter Maydell's risu_arm.c
 *****************************************************************************/

#ifndef RISU_REGINFO_AARCH64_H
#define RISU_REGINFO_AARCH64_H

struct reginfo {
    uint64_t fault_address;
    uint64_t regs[31];
    uint64_t sp;
    uint64_t pc;
    uint32_t flags;
    uint32_t faulting_insn;

    /* FP/SIMD */
    uint32_t fpsr;
    uint32_t fpcr;
    __uint128_t vregs[32];

#ifdef SVE_MAGIC
    /* SVE */
    uint16_t    vl; /* current VL */
    __uint128_t zregs[SVE_NUM_ZREGS][SVE_VQ_MAX];
    uint16_t    pregs[SVE_NUM_PREGS][SVE_VQ_MAX];
    uint16_t    ffr[SVE_VQ_MAX];
#endif
};

#endif /* RISU_REGINFO_AARCH64_H */
