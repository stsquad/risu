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
#include <stddef.h>
#include <string.h>
#include <ucontext.h>
#include <assert.h>
#include <cpuid.h>

#include "risu.h"
#include "risu_reginfo_i386.h"

#include <asm/sigcontext.h>

/*
 * Refer to "Intel(R) 64 and IA-32 Architectures Software Developer's
 * Manual", Volume 1, Section 13.1 "XSAVE-Supported Features and
 * State-Component Bitmaps" for detailed discussion of these constants
 * and their meaning.
 */
enum {
    XFEAT_X87              = 1 << 0,
    XFEAT_SSE              = 1 << 1,
    XFEAT_AVX              = 1 << 2,
    XFEAT_AVX512_OPMASK    = 1 << 5,
    XFEAT_AVX512_ZMM_HI256 = 1 << 6,
    XFEAT_AVX512_HI16_ZMM  = 1 << 7,
    XFEAT_AVX512           = XFEAT_AVX512_OPMASK
                           | XFEAT_AVX512_ZMM_HI256
                           | XFEAT_AVX512_HI16_ZMM
};

static uint64_t xfeatures = XFEAT_X87 | XFEAT_SSE;

static const struct option extra_ops[] = {
    {"xfeatures", required_argument, NULL, FIRST_ARCH_OPT },
    {0, 0, 0, 0}
};

const struct option * const arch_long_opts = extra_ops;
const char * const arch_extra_help
    = "  --xfeatures=<mask>  Use features in mask for XSAVE\n";

void process_arch_opt(int opt, const char *arg)
{
    char *endptr;

    assert(opt == FIRST_ARCH_OPT);

    if (!strcmp(arg, "sse")) {
        xfeatures = XFEAT_X87 | XFEAT_SSE;
    } else if (!strcmp(arg, "avx")) {
        xfeatures = XFEAT_X87 | XFEAT_SSE | XFEAT_AVX;
    } else if (!strcmp(arg, "avx512")) {
        xfeatures = XFEAT_X87 | XFEAT_SSE | XFEAT_AVX | XFEAT_AVX512;
    } else {
        xfeatures = strtoull(arg, &endptr, 0);
        if (*endptr) {
            fprintf(stderr,
                    "Unable to parse '%s' in '%s' into an xfeatures integer mask\n",
                    endptr, arg);
            exit(EXIT_FAILURE);
        }
    }
}

const int reginfo_size(void)
{
    return sizeof(struct reginfo);
}

static void *xsave_feature_buf(struct _xstate *xs, int feature)
{
    unsigned int eax, ebx, ecx, edx;
    int ok;

    /*
     * Get the location of the XSAVE feature from the cpuid leaf.
     * Given that we know the xfeature bit is set, this must succeed.
     */
    ok = __get_cpuid_count(0xd, feature, &eax, &ebx, &ecx, &edx);
    assert(ok);

    /* Sanity check that the frame stored by the kernel contains the data. */
    assert(xs->fpstate.sw_reserved.extended_size >= eax + ebx);

    return (void *)xs + ebx;
}

/* reginfo_init: initialize with a ucontext */
void reginfo_init(struct reginfo *ri, ucontext_t *uc)
{
    int i, nvecregs;
    struct _fpstate *fp;
    struct _xstate *xs;
    uint64_t features;

    memset(ri, 0, sizeof(*ri));

    /* Require master and apprentice to be given the same arguments.  */
    ri->xfeatures = xfeatures;

    for (i = 0; i < NGREG; i++) {
        switch (i) {
        case REG_E(IP):
            /* Store the offset from the start of the test image.  */
            ri->gregs[i] = uc->uc_mcontext.gregs[i] - image_start_address;
            break;
        case REG_EFL:
            /* Store only the "flaggy" bits: SF, ZF, AF, PF, CF.  */
            ri->gregs[i] = uc->uc_mcontext.gregs[i] & 0xd5;
            break;
        case REG_E(SP):
            /* Ignore the stack.  */
            ri->gregs[i] = 0xdeadbeef;
            break;
        case REG_E(AX):
        case REG_E(BX):
        case REG_E(CX):
        case REG_E(DX):
        case REG_E(DI):
        case REG_E(SI):
        case REG_E(BP):
#ifdef __x86_64__
        case REG_R8:
        case REG_R9:
        case REG_R10:
        case REG_R11:
        case REG_R12:
        case REG_R13:
        case REG_R14:
        case REG_R15:
#endif
            ri->gregs[i] = uc->uc_mcontext.gregs[i];
            break;
        }
    }

    /*
     * x86 insns aren't 32 bit but 3 bytes are sufficient to
     * distinguish 'do compare' from 'stop'.
     */
    ri->faulting_insn = *(uint32_t *)uc->uc_mcontext.gregs[REG_E(IP)];

    /*
     * FP state is omitted if unused (aka in init state).
     * Use the <asm/sigcontext.h> struct for access to AVX state.
     */

    fp = (struct _fpstate *)uc->uc_mcontext.fpregs;
    if (fp == NULL) {
        return;
    }

#ifdef __x86_64__
    nvecregs = 16;
#else
    /* We don't (currently) care about the 80387 state, only SSE+.  */
    if (fp->magic != X86_FXSR_MAGIC) {
        return;
    }
    nvecregs = 8;
#endif

    /*
     * Now we know that _fpstate contains FXSAVE data.
     */
    ri->mxcsr = fp->mxcsr;

    for (i = 0; i < nvecregs; ++i) {
#ifdef __x86_64__
        memcpy(&ri->vregs[i], &fp->xmm_space[i * 4], 16);
#else
        memcpy(&ri->vregs[i], &fp->_xmm[i], 16);
#endif
    }

    if (fp->sw_reserved.magic1 != FP_XSTATE_MAGIC1) {
        return;
    }
    xs = (struct _xstate *)fp;
    features = xfeatures & xs->xstate_hdr.xfeatures;

    /*
     * Now we know that _fpstate contains XSAVE data.
     */

    if (features & XFEAT_AVX) {
        /* YMM_Hi128 state */
        void *buf = xsave_feature_buf(xs, XFEAT_AVX);
        for (i = 0; i < nvecregs; ++i) {
            memcpy(&ri->vregs[i].q[2], buf + 16 * i, 16);
        }
    }

    if (features & XFEAT_AVX512_OPMASK) {
        /* Opmask state */
        uint64_t *buf = xsave_feature_buf(xs, XFEAT_AVX512_OPMASK);
        for (i = 0; i < 8; ++i) {
            ri->kregs[i] = buf[i];
        }
    }

    if (features & XFEAT_AVX512_ZMM_HI256) {
        /* ZMM_Hi256 state */
        void *buf = xsave_feature_buf(xs, XFEAT_AVX512_ZMM_HI256);
        for (i = 0; i < nvecregs; ++i) {
            memcpy(&ri->vregs[i].q[4], buf + 32 * i, 32);
        }
    }

#ifdef __x86_64__
    if (features & XFEAT_AVX512_HI16_ZMM) {
        /* Hi16_ZMM state */
        void *buf = xsave_feature_buf(xs, XFEAT_AVX512_HI16_ZMM);
        for (i = 0; i < 16; ++i) {
            memcpy(&ri->vregs[i + 16], buf + 64 * i, 64);
        }
    }
#endif
}

/* reginfo_is_eq: compare the reginfo structs, returns nonzero if equal */
int reginfo_is_eq(struct reginfo *m, struct reginfo *a)
{
    return !memcmp(m, a, sizeof(*m));
}

static const char *const regname[NGREG] = {
    [REG_EFL] = "eflags",
#ifdef __x86_64__
    [REG_RIP] = "rip",
    [REG_RAX] = "rax",
    [REG_RBX] = "rbx",
    [REG_RCX] = "rcx",
    [REG_RDX] = "rdx",
    [REG_RDI] = "rdi",
    [REG_RSI] = "rsi",
    [REG_RBP] = "rbp",
    [REG_RSP] = "rsp",
    [REG_R8]  = "r8",
    [REG_R9]  = "r9",
    [REG_R10] = "r10",
    [REG_R11] = "r11",
    [REG_R12] = "r12",
    [REG_R13] = "r13",
    [REG_R14] = "r14",
    [REG_R15] = "r15",
#else
    [REG_EIP] = "eip",
    [REG_EAX] = "eax",
    [REG_EBX] = "ebx",
    [REG_ECX] = "ecx",
    [REG_EDX] = "edx",
    [REG_EDI] = "edi",
    [REG_ESI] = "esi",
    [REG_EBP] = "ebp",
    [REG_ESP] = "esp",
#endif
};

#ifdef __x86_64__
# define PRIxREG   "%016llx"
#else
# define PRIxREG   "%08x"
#endif

static int get_nvecregs(uint64_t features)
{
#ifdef __x86_64__
    return features & XFEAT_AVX512_HI16_ZMM ? 32 : 16;
#else
    return 8;
#endif
}

static int get_nvecquads(uint64_t features)
{
    if (features & XFEAT_AVX512_ZMM_HI256) {
        return 8;
    } else if (features & XFEAT_AVX) {
        return 4;
    } else {
        return 2;
    }
}

static char get_vecletter(uint64_t features)
{
    if (features & (XFEAT_AVX512_ZMM_HI256 | XFEAT_AVX512_HI16_ZMM)) {
        return 'z';
    } else if (features & XFEAT_AVX) {
        return 'y';
    } else {
        return 'x';
    }
}

/* reginfo_dump: print state to a stream, returns nonzero on success */
int reginfo_dump(struct reginfo *ri, FILE *f)
{
    uint64_t features;
    int i, j, n, w;
    char r;

    fprintf(f, "  faulting insn %x\n", ri->faulting_insn);
    for (i = 0; i < NGREG; i++) {
        if (regname[i]) {
            fprintf(f, "  %-6s: " PRIxREG "\n", regname[i], ri->gregs[i]);
        }
    }

    fprintf(f, "  mxcsr : %x\n", ri->mxcsr);
    fprintf(f, "  xfeat : %" PRIx64 "\n", ri->xfeatures);

    features = ri->xfeatures;
    n = get_nvecregs(features);
    w = get_nvecquads(features);
    r = get_vecletter(features);

    for (i = 0; i < n; i++) {
        fprintf(f, "  %cmm%-3d: ", r, i);
        for (j = w - 1; j >= 0; j--) {
            fprintf(f, "%016" PRIx64 "%c",
                    ri->vregs[i].q[j], j == 0 ? '\n' : ' ');
        }
    }

    if (features & XFEAT_AVX512_OPMASK) {
        for (i = 0; i < 8; i++) {
            fprintf(f, "  k%-5d: %016" PRIx64 "\n", i, ri->kregs[i]);
        }
    }

    return !ferror(f);
}

int reginfo_dump_mismatch(struct reginfo *m, struct reginfo *a, FILE *f)
{
    int i, j, n, w;
    uint64_t features;
    char r;

    fprintf(f, "Mismatch (master v apprentice):\n");

    for (i = 0; i < NGREG; i++) {
        if (m->gregs[i] != a->gregs[i]) {
            assert(regname[i]);
            fprintf(f, "  %-6s: " PRIxREG " v " PRIxREG "\n",
                    regname[i], m->gregs[i], a->gregs[i]);
        }
    }

    if (m->mxcsr != a->mxcsr) {
        fprintf(f, "  mxcsr : %x v %x\n", m->mxcsr, a->mxcsr);
    }
    if (m->xfeatures != a->xfeatures) {
        fprintf(f, "  xfeat : %" PRIx64 " v %" PRIx64 "\n",
                m->xfeatures, a->xfeatures);
    }

    features = m->xfeatures;
    n = get_nvecregs(features);
    w = get_nvecquads(features);
    r = get_vecletter(features);

    for (i = 0; i < n; i++) {
        if (memcmp(&m->vregs[i], &a->vregs[i], w * 8)) {
            fprintf(f, "  %cmm%-3d: ", r, i);
            for (j = w - 1; j >= 0; j--) {
                fprintf(f, "%016" PRIx64 "%c",
                        m->vregs[i].q[j], j == 0 ? '\n' : ' ');
            }
            fprintf(f, "       v: ");
            for (j = w - 1; j >= 0; j--) {
                fprintf(f, "%016" PRIx64 "%c",
                        a->vregs[i].q[j], j == 0 ? '\n' : ' ');
            }
        }
    }

    for (i = 0; i < 8; i++) {
        if (m->kregs[i] != a->kregs[i]) {
            fprintf(f, "  k%-5d: %016" PRIx64 " v %016" PRIx64 "\n",
                    i, m->kregs[i], a->kregs[i]);
        }
    }

    return !ferror(f);
}
