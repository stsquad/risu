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
#include "risu_reginfo_i386.h"

void advance_pc(void *vuc)
{
    ucontext_t *uc = (ucontext_t *) vuc;

    /*
     * We assume that this is UD1 as per get_risuop below.
     * This would need tweaking if we want to test expected undefs.
     */
    uc->uc_mcontext.gregs[REG_E(IP)] += 3;
}

void set_ucontext_paramreg(void *vuc, uint64_t value)
{
    ucontext_t *uc = (ucontext_t *) vuc;
    uc->uc_mcontext.gregs[REG_E(AX)] = value;
}

uint64_t get_reginfo_paramreg(struct reginfo *ri)
{
    return ri->gregs[REG_E(AX)];
}

RisuOp get_risuop(struct reginfo *ri)
{
    if ((ri->faulting_insn & 0xf8ffff) == 0xc0b90f) { /* UD1 %xxx,%eax */
        return (ri->faulting_insn >> 16) & 7;
    }
    return OP_SIGILL;
}

uintptr_t get_pc(struct reginfo *ri)
{
    return ri->gregs[REG_E(IP)];
}
