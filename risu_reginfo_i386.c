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
#include <assert.h>

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

    memset(ri, 0, sizeof(*ri));

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
}

/* reginfo_is_eq: compare the reginfo structs, returns nonzero if equal */
int reginfo_is_eq(struct reginfo *m, struct reginfo *a)
{
    return 0 == memcmp(m, a, sizeof(*m));
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

/* reginfo_dump: print state to a stream, returns nonzero on success */
int reginfo_dump(struct reginfo *ri, FILE *f)
{
    int i;
    fprintf(f, "  faulting insn %x\n", ri->faulting_insn);
    for (i = 0; i < NGREG; i++) {
        if (regname[i]) {
            fprintf(f, "  %-6s: " PRIxREG "\n", regname[i], ri->gregs[i]);
        }
    }
    return !ferror(f);
}

int reginfo_dump_mismatch(struct reginfo *m, struct reginfo *a, FILE *f)
{
    int i;
    for (i = 0; i < NGREG; i++) {
        if (m->gregs[i] != a->gregs[i]) {
            assert(regname[i]);
            fprintf(f, "Mismatch: %s: " PRIxREG " v " PRIxREG "\n",
                    regname[i], m->gregs[i], a->gregs[i]);
        }
    }
    return !ferror(f);
}
