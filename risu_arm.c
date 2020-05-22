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

#include <stdio.h>
#include <ucontext.h>
#include <string.h>

#include "risu.h"
#include "risu_reginfo_arm.h"

int insnsize(ucontext_t *uc)
{
    /* Return instruction size in bytes of the
     * instruction at PC
     */
    if (uc->uc_mcontext.arm_cpsr & 0x20) {
        uint16_t faulting_insn = *((uint16_t *) uc->uc_mcontext.arm_pc);
        switch (faulting_insn & 0xF800) {
        case 0xE800:
        case 0xF000:
        case 0xF800:
            /* 32 bit Thumb2 instruction */
            return 4;
        default:
            /* 16 bit Thumb instruction */
            return 2;
        }
    }
    /* ARM instruction */
    return 4;
}

void advance_pc(void *vuc)
{
    ucontext_t *uc = vuc;
    uc->uc_mcontext.arm_pc += insnsize(uc);
}


void set_ucontext_paramreg(void *vuc, uint64_t value)
{
    ucontext_t *uc = vuc;
    uc->uc_mcontext.arm_r0 = value;
}

uint64_t get_reginfo_paramreg(struct reginfo *ri)
{
    return ri->gpreg[0];
}

RisuOp get_risuop(struct reginfo *ri)
{
    /* Return the risuop we have been asked to do
     * (or OP_SIGILL if this was a SIGILL for a non-risuop insn)
     */
    uint32_t insn = ri->faulting_insn;
    int isz = ri->faulting_insn_size;
    uint32_t op = insn & 0xf;
    uint32_t key = insn & ~0xf;
    uint32_t risukey = (isz == 2) ? 0xdee0 : 0xe7fe5af0;
    return (key != risukey) ? OP_SIGILL : op;
}

uintptr_t get_pc(struct reginfo *ri)
{
   return ri->gpreg[15];
}
