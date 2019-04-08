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
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

#include "risu.h"
#include "risu_reginfo_i386.h"

const struct option * const arch_long_opts;
const char * const arch_extra_help;

void process_arch_opt(int opt, const char *arg)
{
    abort();
}

const int reginfo_size(void)
{
    return sizeof(struct reginfo);
}

/* reginfo_init: initialize with a ucontext */
void reginfo_init(struct reginfo *ri, ucontext_t *uc)
{
    int i;
    for (i = 0; i < NGREG; i++) {
        switch (i) {
        case REG_ESP:
        case REG_UESP:
        case REG_GS:
        case REG_FS:
        case REG_ES:
        case REG_DS:
        case REG_TRAPNO:
        case REG_EFL:
            /* Don't store these registers as it results in mismatches.
             * In particular valgrind has different values for some
             * segment registers, and they're boring anyway.
             * We really shouldn't be ignoring EFL but valgrind doesn't
             * seem to set it right and I don't care to investigate.
             */
            ri->gregs[i] = 0xDEADBEEF;
            break;
        case REG_EIP:
            /* Store the offset from the start of the test image */
            ri->gregs[i] = uc->uc_mcontext.gregs[i] - image_start_address;
            break;
        default:
            ri->gregs[i] = uc->uc_mcontext.gregs[i];
            break;
        }
    }
    /* x86 insns aren't 32 bit but we're not really testing x86 so
     * this is just to distinguish 'do compare' from 'stop'
     */
    ri->faulting_insn = *((uint32_t *) uc->uc_mcontext.gregs[REG_EIP]);
}

/* reginfo_is_eq: compare the reginfo structs, returns nonzero if equal */
int reginfo_is_eq(struct reginfo *m, struct reginfo *a)
{
    return 0 == memcmp(m, a, sizeof(*m));
}

static const char *const regname[NGREG] = {
    "GS", "FS", "ES", "DS", "EDI", "ESI", "EBP", "ESP",
    "EBX", "EDX", "ECX", "EAX", "TRAPNO", "ERR", "EIP",
    "CS", "EFL", "UESP", "SS"
};

/* reginfo_dump: print state to a stream, returns nonzero on success */
int reginfo_dump(struct reginfo *ri, FILE *f)
{
    int i;
    fprintf(f, "  faulting insn %x\n", ri->faulting_insn);
    for (i = 0; i < NGREG; i++) {
        fprintf(f, "  %s: %x\n", regname[i] ? regname[i] : "???",
                ri->gregs[i]);
    }
    return !ferror(f);
}

int reginfo_dump_mismatch(struct reginfo *m, struct reginfo *a, FILE *f)
{
    int i;
    for (i = 0; i < NGREG; i++) {
        if (m->gregs[i] != a->gregs[i]) {
            fprintf(f, "Mismatch: Register %s\n", regname[i] ? regname[i] : "???");
            fprintf(f, "m: [%x] != a: [%x]\n", m->gregs[i], a->gregs[i]);
        }
    }
    return !ferror(f);
}
