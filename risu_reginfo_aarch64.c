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

#include <stdio.h>
#include <ucontext.h>
#include <string.h>
#include <signal.h> /* for FPSIMD_MAGIC */
#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>

#include "risu.h"
#include "risu_reginfo_aarch64.h"

#ifndef SVE_MAGIC
const struct option * const arch_long_opts;
const char * const arch_extra_help;
#else

/* Should we test SVE register state */
static int test_sve;
static const struct option extra_opts[] = {
    {"test-sve", no_argument, &test_sve, 1},
    {0, 0, 0, 0}
};

const struct option * const arch_long_opts = &extra_opts[0];
const char * const arch_extra_help = "  --test-sve        Compare SVE registers\n";
#endif

void process_arch_opt(int opt, const char *arg)
{
    abort();
}

const int reginfo_size(void)
{
    int size = offsetof(struct reginfo, simd.end);
#ifdef SVE_MAGIC
    if (test_sve) {
        size = offsetof(struct reginfo, sve.end);
    }
#endif
    return size;
}

/* reginfo_init: initialize with a ucontext */
void reginfo_init(struct reginfo *ri, ucontext_t *uc)
{
    int i;
    struct _aarch64_ctx *ctx, *extra = NULL;
    struct fpsimd_context *fp = NULL;
#ifdef SVE_MAGIC
    struct sve_context *sve = NULL;
#endif

    /* necessary to be able to compare with memcmp later */
    memset(ri, 0, sizeof(*ri));

    for (i = 0; i < 31; i++) {
        ri->regs[i] = uc->uc_mcontext.regs[i];
    }

    ri->sp = 0xdeadbeefdeadbeef;
    ri->pc = uc->uc_mcontext.pc - image_start_address;
    ri->flags = uc->uc_mcontext.pstate & 0xf0000000;    /* get only flags */

    ri->fault_address = uc->uc_mcontext.fault_address;
    ri->faulting_insn = *((uint32_t *) uc->uc_mcontext.pc);

    ctx = (struct _aarch64_ctx *) &uc->uc_mcontext.__reserved[0];
    while (ctx) {
        switch (ctx->magic) {
        case FPSIMD_MAGIC:
            fp = (void *)ctx;
            break;
#ifdef SVE_MAGIC
        case SVE_MAGIC:
            sve = (void *)ctx;
            break;
        case EXTRA_MAGIC:
            extra = (void *)((struct extra_context *)(ctx))->datap;
            break;
#endif
        case 0:
            /* End of list.  */
            ctx = extra;
            extra = NULL;
            continue;
        default:
            /* Unknown record -- skip it.  */
            break;
        }
        ctx = (void *)ctx + ctx->size;
    }

    if (!fp || fp->head.size != sizeof(*fp)) {
        fprintf(stderr, "risu_reginfo_aarch64: failed to get FP/SIMD state\n");
        return;
    }
    ri->fpsr = fp->fpsr;
    ri->fpcr = fp->fpcr;

#ifdef SVE_MAGIC
    if (test_sve) {
        int vq = sve_vq_from_vl(sve->vl); /* number of quads for whole vl */

        if (sve == NULL) {
            fprintf(stderr, "risu_reginfo_aarch64: failed to get SVE state\n");
            return;
        }

        ri->sve.vl = sve->vl;

        if (sve->head.size < SVE_SIG_CONTEXT_SIZE(vq)) {
            if (sve->head.size == sizeof(*sve)) {
                /* SVE state is empty -- not an error.  */
            } else {
                fprintf(stderr, "risu_reginfo_aarch64: "
                        "failed to get complete SVE state\n");
            }
            return;
        }

        /* Copy ZREG's one at a time */
        for (i = 0; i < SVE_NUM_ZREGS; i++) {
            memcpy(&ri->sve.zregs[i],
                   (void *)sve + SVE_SIG_ZREG_OFFSET(vq, i),
                   SVE_SIG_ZREG_SIZE(vq));
        }

        /* Copy PREG's one at a time */
        for (i = 0; i < SVE_NUM_PREGS; i++) {
            memcpy(&ri->sve.pregs[i],
                   (void *)sve + SVE_SIG_PREG_OFFSET(vq, i),
                   SVE_SIG_PREG_SIZE(vq));
        }

        /* Finally the FFR */
        memcpy(&ri->sve.ffr,(void *)sve + SVE_SIG_FFR_OFFSET(vq),
               SVE_SIG_FFR_SIZE(vq));

        return;
    }
#endif

    for (i = 0; i < 32; i++) {
        ri->simd.vregs[i] = fp->vregs[i];
    }
}

/* reginfo_is_eq: compare the reginfo structs, returns nonzero if equal */
int reginfo_is_eq(struct reginfo *r1, struct reginfo *r2)
{
    return memcmp(r1, r2, reginfo_size()) == 0;
}

#ifdef SVE_MAGIC
static int sve_zreg_is_eq(struct reginfo *r1, struct reginfo *r2, int z)
{
    return memcmp(r1->sve.zregs[z], r2->sve.zregs[z], sizeof(*r1->sve.zregs[z])) == 0;
}

static int sve_preg_is_eq(uint16_t const (*p1)[SVE_VQ_MAX],
                          uint16_t const (*p2)[SVE_VQ_MAX])
{
    return memcmp(p1, p2, sizeof *p1) == 0;
}

static void sve_dump_preg_diff(FILE *f, int vq,
                               uint16_t const (*p1)[SVE_VQ_MAX],
                               uint16_t const (*p2)[SVE_VQ_MAX])
{
    int q;

    for (q = 0; q < vq; q++) {
       fprintf(f, "%#04x", *p1[q]);
    }
    fprintf(f, " vs ");
    for (q = 0; q < vq; q++) {
       fprintf(f, "%#04x", *p2[q]);
    }
    fprintf(f, "\n");
}
#endif

/* reginfo_dump: print state to a stream, returns nonzero on success */
int reginfo_dump(struct reginfo *ri, FILE * f)
{
    int i;
    fprintf(f, "  faulting insn %08x\n", ri->faulting_insn);

    for (i = 0; i < 31; i++) {
        fprintf(f, "  X%-2d   : %016" PRIx64 "\n", i, ri->regs[i]);
    }

    fprintf(f, "  sp    : %016" PRIx64 "\n", ri->sp);
    fprintf(f, "  pc    : %016" PRIx64 "\n", ri->pc);
    fprintf(f, "  flags : %08x\n", ri->flags);
    fprintf(f, "  fpsr  : %08x\n", ri->fpsr);
    fprintf(f, "  fpcr  : %08x\n", ri->fpcr);

    for (i = 0; i < 32; i++) {
        fprintf(f, "  V%-2d   : %016" PRIx64 "%016" PRIx64 "\n", i,
                (uint64_t) (ri->simd.vregs[i] >> 64),
                (uint64_t) (ri->simd.vregs[i]));
    }

    return !ferror(f);
}

/* reginfo_dump_mismatch: print mismatch details to a stream, ret nonzero=ok */
int reginfo_dump_mismatch(struct reginfo *m, struct reginfo *a, FILE * f)
{
    int i;
    fprintf(f, "mismatch detail (master : apprentice):\n");
    if (m->faulting_insn != a->faulting_insn) {
        fprintf(f, "  faulting insn mismatch %08x vs %08x\n",
                m->faulting_insn, a->faulting_insn);
    }
    for (i = 0; i < 31; i++) {
        if (m->regs[i] != a->regs[i]) {
            fprintf(f, "  X%-2d   : %016" PRIx64 " vs %016" PRIx64 "\n",
                    i, m->regs[i], a->regs[i]);
        }
    }

    if (m->sp != a->sp) {
        fprintf(f, "  sp    : %016" PRIx64 " vs %016" PRIx64 "\n",
                m->sp, a->sp);
    }

    if (m->pc != a->pc) {
        fprintf(f, "  pc    : %016" PRIx64 " vs %016" PRIx64 "\n",
                m->pc, a->pc);
    }

    if (m->flags != a->flags) {
        fprintf(f, "  flags : %08x vs %08x\n", m->flags, a->flags);
    }

    if (m->fpsr != a->fpsr) {
        fprintf(f, "  fpsr  : %08x vs %08x\n", m->fpsr, a->fpsr);
    }

    if (m->fpcr != a->fpcr) {
        fprintf(f, "  fpcr  : %08x vs %08x\n", m->fpcr, a->fpcr);
    }

#ifdef SVE_MAGIC
    if (test_sve) {
        struct sve_reginfo *ms = &m->sve;
        struct sve_reginfo *as = &a->sve;

        if (ms->vl != as->vl) {
            fprintf(f, "  SVE VL  : %d vs %d\n", ms->vl, as->vl);
        }

        if (!sve_preg_is_eq(&ms->ffr, &as->ffr)) {
           fprintf(f, "  FFR   : ");
           sve_dump_preg_diff(f, sve_vq_from_vl(ms->vl),
                              &ms->pregs[i], &as->pregs[i]);
        }
        for (i = 0; i < SVE_NUM_PREGS; i++) {
           if (!sve_preg_is_eq(&ms->pregs[i], &as->pregs[i])) {
              fprintf(f, "  P%2d   : ", i);
              sve_dump_preg_diff(f, sve_vq_from_vl(ms->vl),
                                 &ms->pregs[i], &as->pregs[i]);
           }
        }
        for (i = 0; i < SVE_NUM_ZREGS; i++) {
           if (!sve_zreg_is_eq(m, a, i)) {
              int q;
              char *pad="";
              fprintf(f, "  Z%2d   : ", i);
              for (q = 0; q < sve_vq_from_vl(ms->vl); q++) {
                 if (ms->zregs[i][q] != as->zregs[i][q]) {
                    fprintf(f, "%sq%02d: %016" PRIx64 "%016" PRIx64
                            " vs %016" PRIx64 "%016" PRIx64"\n", pad, q,
                            (uint64_t) (ms->zregs[i][q] >> 64),
                            (uint64_t) ms->zregs[i][q],
                            (uint64_t) (as->zregs[i][q] >> 64),
                            (uint64_t) as->zregs[i][q]);
                    pad = "          ";
                 }
              }
           }
        }

        return !ferror(f);
    }
#endif

    for (i = 0; i < 32; i++) {
        if (m->simd.vregs[i] != a->simd.vregs[i]) {
            fprintf(f, "  V%-2d   : "
                    "%016" PRIx64 "%016" PRIx64 " vs "
                    "%016" PRIx64 "%016" PRIx64 "\n", i,
                    (uint64_t) (m->simd.vregs[i] >> 64),
                    (uint64_t) m->simd.vregs[i],
                    (uint64_t) (a->simd.vregs[i] >> 64),
                    (uint64_t) a->simd.vregs[i]);
        }
    }

    return !ferror(f);
}
