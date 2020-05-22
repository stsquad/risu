/******************************************************************************
 * Copyright (c) IBM Corp, 2016
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors:
 *     Jose Ricardo Ziviani - initial implementation
 *     based on Claudio Fontana's risu_aarch64.c
 *     based on Peter Maydell's risu_arm.c
 *****************************************************************************/

#include <stdio.h>
#include <ucontext.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <sys/user.h>

#include "risu.h"
#include "risu_reginfo_ppc64.h"

/* Names for indexes within gregset_t, ignoring those irrelevant here */
enum {
    NIP = 32,
    MSR = 33,
    CTR = 35,
    LNK = 36,
    XER = 37,
    CCR = 38,
};

const struct option * const arch_long_opts;
const char * const arch_extra_help;

static const char * const greg_names[NGREG] = {
     "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
     "r8",  "r9", "r10", "r11", "r12", "r13", "r14", "r15",
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
    "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
    [NIP] = "nip",
    [MSR] = "msr",
    [CTR] = "ctr",
    [LNK] = "lnk",
    [XER] = "xer",
    [CCR] = "ccr",
};

void process_arch_opt(int opt, const char *arg)
{
    abort();
}

void arch_init(void)
{
}

int reginfo_size(struct reginfo *ri)
{
    return sizeof(*ri);
}

/* reginfo_init: initialize with a ucontext */
void reginfo_init(struct reginfo *ri, ucontext_t *uc)
{
    int i;

    memset(ri, 0, sizeof(*ri));

    ri->faulting_insn = *((uint32_t *) uc->uc_mcontext.regs->nip);
    ri->nip = uc->uc_mcontext.regs->nip - image_start_address;

    for (i = 0; i < NGREG; i++) {
        /* Do not copy gp_reg entries not relevant to the context. */
        if (greg_names[i]) {
            ri->gregs[i] = uc->uc_mcontext.gp_regs[i];
        }
    }
    ri->gregs[1] = 0xdeadbeef;   /* sp */
    ri->gregs[13] = 0xdeadbeef;  /* gp */

    memcpy(ri->fpregs, uc->uc_mcontext.fp_regs, 32 * sizeof(double));
    ri->fpscr = uc->uc_mcontext.fp_regs[32];

    memcpy(ri->vrregs.vrregs, uc->uc_mcontext.v_regs->vrregs,
           sizeof(ri->vrregs.vrregs[0]) * 32);
    ri->vrregs.vscr = uc->uc_mcontext.v_regs->vscr;
    ri->vrregs.vrsave = uc->uc_mcontext.v_regs->vrsave;
}

/* reginfo_is_eq: compare the reginfo structs, returns nonzero if equal */
int reginfo_is_eq(struct reginfo *m, struct reginfo *a)
{
    return memcmp(m, a, sizeof(*m));
}

/* reginfo_dump: print state to a stream */
void reginfo_dump(struct reginfo *ri, FILE * f)
{
    const char *sep;
    int i, j;

    fprintf(f, "%6s: %08x\n", "insn", ri->faulting_insn);
    fprintf(f, "%6s: %016lx\n", "pc", ri->nip);

    sep = "";
    for (i = j = 0; i < NGREG; i++) {
        if (greg_names[i] != NULL) {
            fprintf(f, "%s%6s: %016lx", sep, greg_names[i], ri->gregs[i]);
            sep = (++j & 1 ? "  " : "\n");
        }
    }

    sep = "\n";
    for (i = j = 0; i < 32; i++) {
        fprintf(f, "%s%*s%d: %016lx",
                sep, 6 - (i < 10 ? 1 : 2), "f", i, ri->fpregs[i]);
        sep = (++j & 1 ? "  " : "\n");
    }
    fprintf(f, "\n%6s: %016lx\n", "fpscr", ri->fpscr);

    for (i = 0; i < 32; i++) {
        fprintf(f, "%*s%d: %08x %08x %08x %08x\n",
                6 - (i < 10 ? 1 : 2), "vr", i,
                ri->vrregs.vrregs[i][0], ri->vrregs.vrregs[i][1],
                ri->vrregs.vrregs[i][2], ri->vrregs.vrregs[i][3]);
    }
}

void reginfo_dump_mismatch(struct reginfo *m, struct reginfo *a, FILE *f)
{
    int i;

    for (i = 0; i < NGREG; i++) {
        if (greg_names[i] != NULL && m->gregs[i] != a->gregs[i]) {
            fprintf(f, "%6s: %016lx vs %016lx\n",
                    greg_names[i], m->gregs[i], a->gregs[i]);
        }
    }

    for (i = 0; i < 32; i++) {
        if (m->fpregs[i] != a->fpregs[i]) {
            fprintf(f, "%*s%d: %016lx vs %016lx\n",
                    6 - (i < 10 ? 1 : 2), "f", i,
                    m->fpregs[i], a->fpregs[i]);
        }
    }

    for (i = 0; i < 32; i++) {
        if (m->vrregs.vrregs[i][0] != a->vrregs.vrregs[i][0] ||
            m->vrregs.vrregs[i][1] != a->vrregs.vrregs[i][1] ||
            m->vrregs.vrregs[i][2] != a->vrregs.vrregs[i][2] ||
            m->vrregs.vrregs[i][3] != a->vrregs.vrregs[i][3]) {

            fprintf(f, "%*s%d: %08x%08x%08x%08x vs %08x%08x%08x%08x\n",
                    6 - (i < 10 ? 1 : 2), "vr", i,
                    m->vrregs.vrregs[i][0], m->vrregs.vrregs[i][1],
                    m->vrregs.vrregs[i][2], m->vrregs.vrregs[i][3],
                    a->vrregs.vrregs[i][0], a->vrregs.vrregs[i][1],
                    a->vrregs.vrregs[i][2], a->vrregs.vrregs[i][3]);
        }
    }
}
