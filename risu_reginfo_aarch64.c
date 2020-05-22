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
#include <assert.h>
#include <sys/prctl.h>

#include "risu.h"
#include "risu_reginfo_aarch64.h"

#ifndef SVE_MAGIC
const struct option * const arch_long_opts;
const char * const arch_extra_help;
#else

/* Should we test SVE register state */
static int test_sve;
static const struct option extra_opts[] = {
    {"test-sve", required_argument, NULL, FIRST_ARCH_OPT },
    {0, 0, 0, 0}
};

const struct option * const arch_long_opts = &extra_opts[0];
const char * const arch_extra_help
    = "  --test-sve=<vq>        Compare SVE registers with VQ\n";
#endif

void process_arch_opt(int opt, const char *arg)
{
#ifdef SVE_MAGIC
    assert(opt == FIRST_ARCH_OPT);
    test_sve = strtol(arg, 0, 10);

    if (test_sve <= 0 || test_sve > SVE_VQ_MAX) {
        fprintf(stderr, "Invalid value for VQ (1-%d)\n", SVE_VQ_MAX);
        exit(EXIT_FAILURE);
    }
#else
    abort();
#endif
}

void arch_init(void)
{
#ifdef SVE_MAGIC
    long want, got1, got2;

    if (test_sve == 0) {
        return;
    }

    want = sve_vl_from_vq(test_sve);
    asm(".arch_extension sve\n\trdvl %0, #1" : "=r"(got1));
    if (want != got1) {
        got2 = prctl(PR_SVE_SET_VL, want);
        if (want != got2) {
            if (got2 < 0) {
                perror("prctl PR_SVE_SET_VL");
                got2 = got1;
            }
            fprintf(stderr, "Unsupported value for VQ (%d != %d)\n",
                    test_sve, (int)sve_vq_from_vl(got1));
            exit(EXIT_FAILURE);
        }
    }
#endif
}

int reginfo_size(struct reginfo *ri)
{
#ifdef SVE_MAGIC
    if (ri->sve_vl) {
        int vq = sve_vq_from_vl(ri->sve_vl);
        return (offsetof(struct reginfo, sve) +
                SVE_SIG_CONTEXT_SIZE(vq) - SVE_SIG_REGS_OFFSET);
    }
#endif
    return offsetof(struct reginfo, simd) + sizeof(ri->simd);
}

#ifdef SVE_MAGIC
static uint64_t *reginfo_zreg(struct reginfo *ri, int vq, int i)
{
    return (uint64_t *)(ri->sve + SVE_SIG_ZREG_OFFSET(vq, i) -
                        SVE_SIG_REGS_OFFSET);
}

static uint16_t *reginfo_preg(struct reginfo *ri, int vq, int i)
{
    return (uint16_t *)(ri->sve + SVE_SIG_PREG_OFFSET(vq, i) -
                        SVE_SIG_REGS_OFFSET);
}
#endif

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
        int vq = test_sve;

        if (sve == NULL) {
            fprintf(stderr, "risu_reginfo_aarch64: failed to get SVE state\n");
            return;
        }
        if (sve->vl != sve_vl_from_vq(vq)) {
            fprintf(stderr, "risu_reginfo_aarch64: "
                    "unexpected SVE state: %d != %d\n",
                    sve->vl, sve_vl_from_vq(vq));
            return;
        }

        if (sve->head.size < SVE_SIG_CONTEXT_SIZE(vq)) {
            if (sve->head.size == sizeof(*sve)) {
                /* SVE state is empty -- not an error.  */
                goto do_simd;
            } else {
                fprintf(stderr, "risu_reginfo_aarch64: "
                        "failed to get complete SVE state\n");
            }
            return;
        }

        ri->sve_vl = sve->vl;
        memcpy(ri->sve, (char *)sve + SVE_SIG_REGS_OFFSET,
               SVE_SIG_CONTEXT_SIZE(vq) - SVE_SIG_REGS_OFFSET);
        return;
    }
 do_simd:
#endif /* SVE_MAGIC */

    for (i = 0; i < 32; i++) {
        ri->simd.vregs[i] = fp->vregs[i];
    }
}

/* reginfo_is_eq: compare the reginfo structs, returns nonzero if equal */
int reginfo_is_eq(struct reginfo *r1, struct reginfo *r2)
{
    return memcmp(r1, r2, reginfo_size(r1)) == 0;
}

#ifdef SVE_MAGIC
static int sve_zreg_is_eq(int vq, const void *z1, const void *z2)
{
    return memcmp(z1, z2, vq * 16) == 0;
}

static int sve_preg_is_eq(int vq, const void *p1, const void *p2)
{
    return memcmp(p1, p2, vq * 2) == 0;
}

static void sve_dump_preg(FILE *f, int vq, const uint16_t *p)
{
    int q;
    for (q = vq - 1; q >= 0; q--) {
        fprintf(f, "%04x", p[q]);
    }
}

static void sve_dump_preg_diff(FILE *f, int vq, const uint16_t *p1,
                               const uint16_t *p2)
{
    sve_dump_preg(f, vq, p1);
    fprintf(f, " vs ");
    sve_dump_preg(f, vq, p2);
    fprintf(f, "\n");
}

static void sve_dump_zreg_diff(FILE *f, int vq, const uint64_t *za,
                               const uint64_t *zb)
{
    const char *pad = "";
    int q;

    for (q = 0; q < vq; ++q) {
        uint64_t za0 = za[2 * q], za1 = za[2 * q + 1];
        uint64_t zb0 = zb[2 * q], zb1 = zb[2 * q + 1];

        if (za0 != zb0 || za1 != zb1) {
            fprintf(f, "%sq%-2d: %016" PRIx64 "%016" PRIx64
                    " vs %016" PRIx64 "%016" PRIx64"\n",
                    pad, q, za1, za0, zb1, zb0);
            pad = "      ";
        }
    }
}
#endif

/* reginfo_dump: print state to a stream */
void reginfo_dump(struct reginfo *ri, FILE * f)
{
    int i;
    fprintf(f, "  faulting insn %08x\n", ri->faulting_insn);

    for (i = 0; i < 31; i++) {
        fprintf(f, "  X%-2d    : %016" PRIx64 "\n", i, ri->regs[i]);
    }

    fprintf(f, "  sp     : %016" PRIx64 "\n", ri->sp);
    fprintf(f, "  pc     : %016" PRIx64 "\n", ri->pc);
    fprintf(f, "  flags  : %08x\n", ri->flags);
    fprintf(f, "  fpsr   : %08x\n", ri->fpsr);
    fprintf(f, "  fpcr   : %08x\n", ri->fpcr);

#ifdef SVE_MAGIC
    if (ri->sve_vl) {
        int vq = sve_vq_from_vl(ri->sve_vl);
        int q;

        fprintf(f, "  vl     : %d\n", ri->sve_vl);

        for (i = 0; i < SVE_NUM_ZREGS; i++) {
            uint64_t *z = reginfo_zreg(ri, vq, i);

            fprintf(f, "  Z%-2d q%-2d: %016" PRIx64 "%016" PRIx64 "\n",
                    i, 0, z[1], z[0]);
            for (q = 1; q < vq; ++q) {
                fprintf(f, "      q%-2d: %016" PRIx64 "%016" PRIx64 "\n",
                        q, z[q * 2 + 1], z[q * 2]);
            }
        }

        for (i = 0; i < SVE_NUM_PREGS + 1; i++) {
            uint16_t *p = reginfo_preg(ri, vq, i);

            if (i == SVE_NUM_PREGS) {
                fprintf(f, "  FFR    : ");
            } else {
                fprintf(f, "  P%-2d    : ", i);
            }
            sve_dump_preg(f, vq, p);
            fprintf(f, "\n");
        }
        return;
    }
#endif

    for (i = 0; i < 32; i++) {
        fprintf(f, "  V%-2d    : %016" PRIx64 "%016" PRIx64 "\n", i,
                (uint64_t) (ri->simd.vregs[i] >> 64),
                (uint64_t) (ri->simd.vregs[i]));
    }
}

void reginfo_dump_mismatch(struct reginfo *m, struct reginfo *a, FILE * f)
{
    int i;

    if (m->faulting_insn != a->faulting_insn) {
        fprintf(f, "  faulting insn: %08x vs %08x\n",
                m->faulting_insn, a->faulting_insn);
    }

    for (i = 0; i < 31; i++) {
        if (m->regs[i] != a->regs[i]) {
            fprintf(f, "  X%-2d    : %016" PRIx64 " vs %016" PRIx64 "\n",
                    i, m->regs[i], a->regs[i]);
        }
    }

    if (m->sp != a->sp) {
        fprintf(f, "  sp     : %016" PRIx64 " vs %016" PRIx64 "\n",
                m->sp, a->sp);
    }

    if (m->pc != a->pc) {
        fprintf(f, "  pc     : %016" PRIx64 " vs %016" PRIx64 "\n",
                m->pc, a->pc);
    }

    if (m->flags != a->flags) {
        fprintf(f, "  flags  : %08x vs %08x\n", m->flags, a->flags);
    }

    if (m->fpsr != a->fpsr) {
        fprintf(f, "  fpsr   : %08x vs %08x\n", m->fpsr, a->fpsr);
    }

    if (m->fpcr != a->fpcr) {
        fprintf(f, "  fpcr   : %08x vs %08x\n", m->fpcr, a->fpcr);
    }

#ifdef SVE_MAGIC
    if (m->sve_vl != a->sve_vl) {
        fprintf(f, "  vl    : %d vs %d\n", m->sve_vl, a->sve_vl);
    }
    if (m->sve_vl) {
        int vq = sve_vq_from_vl(m->sve_vl);

        for (i = 0; i < SVE_NUM_ZREGS; i++) {
            uint64_t *zm = reginfo_zreg(m, vq, i);
            uint64_t *za = reginfo_zreg(a, vq, i);

            if (!sve_zreg_is_eq(vq, zm, za)) {
                fprintf(f, "  Z%-2d ", i);
                sve_dump_zreg_diff(f, vq, zm, za);
            }
        }
        for (i = 0; i < SVE_NUM_PREGS + 1; i++) {
            uint16_t *pm = reginfo_preg(m, vq, i);
            uint16_t *pa = reginfo_preg(a, vq, i);

            if (!sve_preg_is_eq(vq, pm, pa)) {
                if (i == SVE_NUM_PREGS) {
                    fprintf(f, "  FFR   : ");
                } else {
                    fprintf(f, "  P%-2d    : ", i);
                }
                sve_dump_preg_diff(f, vq, pm, pa);
            }
        }
        return;
    }
#endif

    for (i = 0; i < 32; i++) {
        if (m->simd.vregs[i] != a->simd.vregs[i]) {
            fprintf(f, "  V%-2d    : "
                    "%016" PRIx64 "%016" PRIx64 " vs "
                    "%016" PRIx64 "%016" PRIx64 "\n", i,
                    (uint64_t) (m->simd.vregs[i] >> 64),
                    (uint64_t) m->simd.vregs[i],
                    (uint64_t) (a->simd.vregs[i] >> 64),
                    (uint64_t) a->simd.vregs[i]);
        }
    }
}
