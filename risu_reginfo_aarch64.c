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
#include <getopt.h>
#include <stdlib.h>
#include <stdbool.h>

#include "risu.h"
#include "risu_reginfo_aarch64.h"

#ifndef SVE_MAGIC
void *arch_long_opts;
char *arch_extra_help;
#else
/* Should we test SVE register state */
static int test_sve;
static struct option extra_opts[] = {
    {"test-sve", no_argument, &test_sve, 1},
    {0, 0, 0, 0}
};

void *arch_long_opts = &extra_opts[0];
char *arch_extra_help = "  --test-sve        Compare SVE registers\n";

/* Extra SVE copy function, only called with --test-sve */
static void reginfo_copy_sve(struct reginfo *ri, struct _aarch64_ctx *ctx)
{
    struct sve_context *sve;
    int r, vq;
    bool found = false;

    while (!found) {
       switch (ctx->magic)
       {
          case SVE_MAGIC:
             found = true;
             break;
          case EXTRA_MAGIC:
             fprintf(stderr, "%s: found EXTRA_MAGIC\n", __func__);
             abort();
          case 0:
             /* We might not have an SVE context */
             fprintf(stderr, "%s: reached end of ctx, no joy (%d)\n", __func__, ctx->size);
             return;
          default:
             ctx = (struct _aarch64_ctx *)((void *)ctx + ctx->size);
             break;
       }

    }

    sve = (struct sve_context *) ctx;
    ri->vl = sve->vl;
    vq = sve_vq_from_vl(sve->vl); /* number of quads for whole vl */

    /* Copy ZREG's one at a time */
    for (r = 0; r < SVE_NUM_ZREGS; r++) {
        memcpy(&ri->zregs[r],
               (char *)sve + SVE_SIG_ZREG_OFFSET(vq, r),
               SVE_SIG_ZREG_SIZE(vq));
    }

    /* Copy PREG's one at a time */
    for (r = 0; r < SVE_NUM_PREGS; r++) {
        memcpy(&ri->pregs[r],
               (char *)sve + SVE_SIG_PREG_OFFSET(vq, r),
               SVE_SIG_PREG_SIZE(vq));
    }

    /* Finally the FFR */
    memcpy(&ri->ffr,(char *)sve + SVE_SIG_FFR_OFFSET(vq),
               SVE_SIG_FFR_SIZE(vq));

}
#endif

/* reginfo_init: initialize with a ucontext */
void reginfo_init(struct reginfo *ri, ucontext_t *uc)
{
    int i;
    struct _aarch64_ctx *ctx;
    struct fpsimd_context *fp;

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

    while (ctx->magic != FPSIMD_MAGIC && ctx->size != 0) {
        ctx += (ctx->size + sizeof(*ctx) - 1) / sizeof(*ctx);
    }

    if (ctx->magic != FPSIMD_MAGIC || ctx->size != sizeof(*fp)) {
        fprintf(stderr,
                "risu_reginfo_aarch64: failed to get FP/SIMD state\n");
        return;
    }

    fp = (struct fpsimd_context *) ctx;
    ri->fpsr = fp->fpsr;
    ri->fpcr = fp->fpcr;

    for (i = 0; i < 32; i++) {
        ri->vregs[i] = fp->vregs[i];
    }

#ifdef SVE_MAGIC
    if (test_sve) {
        ctx = (struct _aarch64_ctx *) &uc->uc_mcontext.__reserved[0];
        reginfo_copy_sve(ri, ctx);
    }
#endif
};

/* reginfo_is_eq: compare the reginfo structs, returns nonzero if equal */
int reginfo_is_eq(struct reginfo *r1, struct reginfo *r2)
{
    return memcmp(r1, r2, sizeof(*r1)) == 0;
}

#ifdef SVE_MAGIC
static int sve_zreg_is_eq(struct reginfo *r1, struct reginfo *r2, int z)
{
    return memcmp(r1->zregs[z], r2->zregs[z], sizeof(*r1->zregs[z])) == 0;
}

static int sve_preg_is_eq(struct reginfo *r1, struct reginfo *r2, int p)
{
    return memcmp(r1->pregs[p], r2->pregs[p], sizeof(*r1->pregs[p])) == 0;
}
#endif

/* reginfo_dump: print state to a stream, returns nonzero on success */
int reginfo_dump(struct reginfo *ri, FILE * f)
{
    int i;
    fprintf(f, "  faulting insn %08x\n", ri->faulting_insn);

    for (i = 0; i < 31; i++) {
        fprintf(f, "  X%2d   : %016" PRIx64 "\n", i, ri->regs[i]);
    }

    fprintf(f, "  sp    : %016" PRIx64 "\n", ri->sp);
    fprintf(f, "  pc    : %016" PRIx64 "\n", ri->pc);
    fprintf(f, "  flags : %08x\n", ri->flags);
    fprintf(f, "  fpsr  : %08x\n", ri->fpsr);
    fprintf(f, "  fpcr  : %08x\n", ri->fpcr);

    for (i = 0; i < 32; i++) {
        fprintf(f, "  V%2d   : %016" PRIx64 "%016" PRIx64 "\n", i,
                (uint64_t) (ri->vregs[i] >> 64),
                (uint64_t) (ri->vregs[i] & 0xffffffffffffffff));
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
            fprintf(f, "  X%2d   : %016" PRIx64 " vs %016" PRIx64 "\n",
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

    for (i = 0; i < 32; i++) {
        if (m->vregs[i] != a->vregs[i]) {
            fprintf(f, "  V%2d   : "
                    "%016" PRIx64 "%016" PRIx64 " vs "
                    "%016" PRIx64 "%016" PRIx64 "\n", i,
                    (uint64_t) (m->vregs[i] >> 64),
                    (uint64_t) (m->vregs[i] & 0xffffffffffffffff),
                    (uint64_t) (a->vregs[i] >> 64),
                    (uint64_t) (a->vregs[i] & 0xffffffffffffffff));
        }
    }

#ifdef SVE_MAGIC
    if (test_sve) {
        if (m->vl != a->vl) {
            fprintf(f, "  SVE VL  : %d vs %d\n", m->vl, a->vl);
        }
        for (i = 0; i < SVE_NUM_PREGS; i++) {
           if (!sve_preg_is_eq(m, a, i)) {
              int q;
              fprintf(f, "  P%2d   : ", i);
              for (q = 0; q < sve_vq_from_vl(m->vl); q++) {
                 fprintf(f, "%04x", m->pregs[i][q]);
              }
              fprintf(f, " vs ");
              for (q = 0; q < sve_vq_from_vl(m->vl); q++) {
                 fprintf(f, "%04x", a->pregs[i][q]);
              }
              fprintf(f, "\n");
            }
        }
        for (i = 0; i < SVE_NUM_ZREGS; i++) {
           if (!sve_zreg_is_eq(m, a, i)) {
              int q;
              char *pad="";
              fprintf(f, "  Z%2d   : ", i);
              for (q = 0; q < sve_vq_from_vl(m->vl); q++) {
                 if (m->zregs[i][q] != a->zregs[i][q]) {
                    fprintf(f, "%sq%02d: %016" PRIx64 "%016" PRIx64 " vs %016" PRIx64 "%016" PRIx64"\n", pad, q,
                            (uint64_t) (m->zregs[i][q] >> 64), (uint64_t) m->zregs[i][q],
                            (uint64_t) (a->zregs[i][q] >> 64), (uint64_t) a->zregs[i][q]);
                    pad = "          ";
                 }
              }
           }
        }
    }
#endif

    return !ferror(f);
}
