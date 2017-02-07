/*****************************************************************************
 * Copyright (c) 2016 Laurent Vivier
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *****************************************************************************/

#include <stdio.h>
#include <ucontext.h>
#include <string.h>
#include <math.h>

#include "risu.h"
#include "risu_reginfo_m68k.h"

/* reginfo_init: initialize with a ucontext */
void reginfo_init(struct reginfo *ri, ucontext_t *uc)
{
    int i;
    memset(ri, 0, sizeof(*ri));

    ri->faulting_insn = *((uint32_t *)uc->uc_mcontext.gregs[R_PC]);
    ri->pc = uc->uc_mcontext.gregs[R_PC] - image_start_address;

    for (i = 0; i < NGREG; i++) {
        ri->gregs[i] = uc->uc_mcontext.gregs[i];
    }

    ri->fpregs.f_pcr = uc->uc_mcontext.fpregs.f_pcr;
    ri->fpregs.f_psr = uc->uc_mcontext.fpregs.f_psr;
    ri->fpregs.f_fpiaddr = uc->uc_mcontext.fpregs.f_fpiaddr;
    for (i = 0; i < 8; i++) {
        memcpy(&ri->fpregs.f_fpregs[i * 3],
               &uc->uc_mcontext.fpregs.f_fpregs[i * 3],
               3 * sizeof(int));
    }
}

/* reginfo_is_eq: compare the reginfo structs, returns nonzero if equal */
int reginfo_is_eq(struct reginfo *m, struct reginfo *a, ucontext_t *uc)
{
    int i;

    if (m->gregs[R_PS] != a->gregs[R_PS]) {
        return 0;
    }

    for (i = 0; i < 16; i++) {
        if (i == R_SP || i == R_A6) {
            continue;
        }
        if (m->gregs[i] != a->gregs[i]) {
            return 0;
        }
    }

    if (m->fpregs.f_pcr != a->fpregs.f_pcr) {
        return 0;
    }

    if (m->fpregs.f_psr != a->fpregs.f_psr) {
        return 0;
    }

    for (i = 0; i < 8; i++) {
        if (m->fpregs.f_fpregs[i * 3] != a->fpregs.f_fpregs[i * 3] ||
            m->fpregs.f_fpregs[i * 3 + 1] != a->fpregs.f_fpregs[i * 3 + 1] ||
            m->fpregs.f_fpregs[i * 3 + 2] != a->fpregs.f_fpregs[i * 3 + 2]) {
            return 0;
        }
    }

    return 1;
}

/* reginfo_dump: print state to a stream, returns nonzero on success */
void reginfo_dump(struct reginfo *ri, int is_master)
{
    int i;
    if (is_master) {
        fprintf(stderr, "  pc            \e[1;101;37m0x%08x\e[0m\n",
                ri->pc);
    }
    fprintf(stderr, "\tPC: %08x\n", ri->gregs[R_PC]);
    fprintf(stderr, "\tPS: %04x\n", ri->gregs[R_PS]);

    for (i = 0; i < 8; i++) {
        fprintf(stderr, "\tD%d: %8x\tA%d: %8x\n", i, ri->gregs[i],
                i, ri->gregs[i + 8]);
    }


    for (i = 0; i < 8; i++) {
        fprintf(stderr, "\tFP%d: %08x %08x %08x\n", i,
                ri->fpregs.f_fpregs[i * 3], ri->fpregs.f_fpregs[i * 3 + 1],
                ri->fpregs.f_fpregs[i * 3 + 2]);
    }

    fprintf(stderr, "\n");
}

int reginfo_dump_mismatch(struct reginfo *m, struct reginfo *a, FILE *f)
{
    int i;

    if (m->gregs[R_PS] != a->gregs[R_PS]) {
            fprintf(f, "Mismatch: Register PS\n");
            fprintf(f, "master: [%x] - apprentice: [%x]\n",
                    m->gregs[R_PS], a->gregs[R_PS]);
    }

    for (i = 0; i < 16; i++) {
        if (i == R_SP || i == R_A6) {
            continue;
        }
        if (m->gregs[i] != a->gregs[i]) {
            fprintf(f, "Mismatch: Register %c%d\n", i < 8 ? 'D' : 'A', i % 8);
            fprintf(f, "master: [%x] - apprentice: [%x]\n",
                    m->gregs[i], a->gregs[i]);
        }
    }

    if (m->fpregs.f_pcr != a->fpregs.f_pcr) {
        fprintf(f, "Mismatch: Register FPCR\n");
        fprintf(f, "m: [%04x] != a: [%04x]\n",
                m->fpregs.f_pcr, a->fpregs.f_pcr);
    }

    if (m->fpregs.f_psr != a->fpregs.f_psr) {
        fprintf(f, "Mismatch: Register FPSR\n");
        fprintf(f, "m: [%08x] != a: [%08x]\n",
                m->fpregs.f_psr, a->fpregs.f_psr);
    }

    for (i = 0; i < 8; i++) {
        if (m->fpregs.f_fpregs[i * 3] != a->fpregs.f_fpregs[i * 3] ||
            m->fpregs.f_fpregs[i * 3 + 1] != a->fpregs.f_fpregs[i * 3 + 1] ||
            m->fpregs.f_fpregs[i * 3 + 2] != a->fpregs.f_fpregs[i * 3 + 2]) {
            fprintf(f, "Mismatch: Register FP%d\n", i);
            fprintf(f, "m: [%08x %08x %08x] != a: [%08x %08x %08x]\n",
                    m->fpregs.f_fpregs[i * 3], m->fpregs.f_fpregs[i * 3 + 1],
                    m->fpregs.f_fpregs[i * 3 + 2], a->fpregs.f_fpregs[i * 3],
                    a->fpregs.f_fpregs[i * 3 + 1],
                    a->fpregs.f_fpregs[i * 3 + 2]);
        }
    }


    return !ferror(f);
}
