/*******************************************************************************
 * Copyright (c) 2016 Laurent Vivier
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 ******************************************************************************/

#include "risu.h"

void advance_pc(void *vuc)
{
    ucontext_t *uc = (ucontext_t *) vuc;
    uc->uc_mcontext.gregs[R_PC] += 4;
}

void set_ucontext_paramreg(void *vuc, uint64_t value)
{
    ucontext_t *uc = vuc;
    uc->uc_mcontext.gregs[R_A0] = value;
}

uint64_t get_reginfo_paramreg(struct reginfo *ri)
{
    return ri->gregs[R_A0];
}

RisuOp get_risuop(struct reginfo *ri)
{
    uint32_t insn = ri->faulting_insn;
    uint32_t op = insn & 0xf;
    uint32_t key = insn & ~0xf;
    uint32_t risukey = 0x4afc7000;
    return (key != risukey) ? OP_SIGILL : op;
}

uintptr_t get_pc(struct reginfo *ri)
{
    return ri->gregs[R_PC];
}
