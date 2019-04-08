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

    /* We assume that this is either UD1 or UD2.
     * This would need tweaking if we want to test
     * expected undefs on x86.
     */
    uc->uc_mcontext.gregs[REG_EIP] += 2;
}

void set_ucontext_paramreg(void *vuc, uint64_t value)
{
    ucontext_t *uc = (ucontext_t *) vuc;
    uc->uc_mcontext.gregs[REG_EAX] = (uint32_t) value;
}

uint64_t get_reginfo_paramreg(struct reginfo *ri)
{
    return ri->gregs[REG_EAX];
}

int get_risuop(struct reginfo *ri)
{
    switch (ri->faulting_insn & 0xffff) {
    case 0xb90f:                /* UD1 */
        return OP_COMPARE;
    case 0x0b0f:                /* UD2 */
        return OP_TESTEND;
    default:                    /* unexpected */
        return -1;
    }
}

uintptr_t get_pc(struct reginfo *ri)
{
    return ri->gregs[REG_EIP];
}
