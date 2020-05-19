/******************************************************************************
 * Copyright (c) 2017 Linaro Limited
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors:
 *     Peter Maydell (Linaro) - initial implementation
 *****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "risu.h"

static struct reginfo master_ri, apprentice_ri;
static uint8_t master_memblock[MEMBLOCKLEN];
static trace_header_t master_header;

RisuResult send_register_info(void *uc)
{
    struct reginfo ri;
    trace_header_t header;
    RisuResult res;
    RisuOp op;
    void *extra;

    reginfo_init(&ri, uc);
    op = get_risuop(&ri);

    /* Write a header with PC/op to keep in sync */
    header.magic = RISU_MAGIC;
    header.pc = get_pc(&ri);
    header.risu_op = op;

    switch (op) {
    case OP_TESTEND:
    case OP_COMPARE:
    case OP_SIGILL:
        header.size = reginfo_size(&ri);
        extra = &ri;
        break;

    case OP_SETMEMBLOCK:
    case OP_GETMEMBLOCK:
        header.size = 0;
        extra = NULL;
        break;

    case OP_COMPAREMEM:
        header.size = MEMBLOCKLEN;
        extra = memblock;
        break;

    default:
        abort();
    }

    res = write_buffer(&header, sizeof(header));
    if (res != RES_OK) {
        return res;
    }
    if (extra) {
        res = write_buffer(extra, header.size);
        if (res != RES_OK) {
            return res;
        }
    }

    switch (op) {
    case OP_COMPARE:
    case OP_SIGILL:
    case OP_COMPAREMEM:
        break;
    case OP_TESTEND:
        return RES_END;
    case OP_SETMEMBLOCK:
        memblock = (void *)(uintptr_t)get_reginfo_paramreg(&ri);
        break;
    case OP_GETMEMBLOCK:
        set_ucontext_paramreg(uc,
                              get_reginfo_paramreg(&ri) + (uintptr_t)memblock);
        break;
    default:
        abort();
    }
    return RES_OK;
}

/* Read register info from the socket and compare it with that from the
 * ucontext. Return 0 for match, 1 for end-of-test, 2 for mismatch.
 * NB: called from a signal handler.
 *
 * We don't have any kind of identifying info in the incoming data
 * that says whether it is register or memory data, so if the two
 * sides get out of sync then we will fail obscurely.
 */
RisuResult recv_and_compare_register_info(void *uc)
{
    RisuResult res;
    size_t extra_size;
    RisuOp op;

    reginfo_init(&apprentice_ri, uc);
    op = get_risuop(&apprentice_ri);

    switch (op) {
    case OP_TESTEND:
    case OP_COMPARE:
    case OP_SIGILL:
        extra_size = reginfo_size(&master_ri);
        break;
    case OP_SETMEMBLOCK:
    case OP_GETMEMBLOCK:
        extra_size = 0;
        break;
    case OP_COMPAREMEM:
        extra_size = MEMBLOCKLEN;
        break;
    default:
        abort();
    }

    res = read_buffer(&master_header, sizeof(master_header));
    if (res != RES_OK) {
        goto fail_header;
    }
    if (master_header.magic != RISU_MAGIC ||
        master_header.risu_op != op ||
        master_header.size != extra_size) {
        res = RES_MISMATCH_HEAD;
        goto fail_header;
    }

    /* send OK for the header */
    respond(RES_OK);

    switch (op) {
    case OP_TESTEND:
    case OP_COMPARE:
    case OP_SIGILL:
        res = read_buffer(&master_ri, extra_size);
        if (res != RES_OK) {
            /* fail */
        } else if (!reginfo_is_eq(&master_ri, &apprentice_ri)) {
            /* register mismatch */
            res = RES_MISMATCH_REG;
        } else if (op == OP_TESTEND) {
            res = RES_END;
        }
        break;

    case OP_SETMEMBLOCK:
        memblock = (void *)(uintptr_t)get_reginfo_paramreg(&apprentice_ri);
        return RES_OK;

    case OP_GETMEMBLOCK:
        set_ucontext_paramreg(uc, get_reginfo_paramreg(&apprentice_ri) +
                              (uintptr_t)memblock);
        return RES_OK;

    case OP_COMPAREMEM:
        res = read_buffer(master_memblock, MEMBLOCKLEN);
        if (res != RES_OK) {
            /* fail */
        } else if (memcmp(memblock, master_memblock, MEMBLOCKLEN) != 0) {
            /* memory mismatch */
            res = RES_MISMATCH_MEM;
        }
        break;

    default:
        abort();
    }

 fail_header:
    respond(res == RES_OK ? RES_OK : RES_END);
    return res;
}

/*
 * Print a useful report on the status of the last reg comparison
 * done in recv_and_compare_register_info().
 */
void report_mismatch_reg(void)
{
    fprintf(stderr, "master reginfo:\n");
    reginfo_dump(&master_ri, stderr);
    fprintf(stderr, "apprentice reginfo:\n");
    reginfo_dump(&apprentice_ri, stderr);
    reginfo_dump_mismatch(&master_ri, &apprentice_ri, stderr);
}

void report_mismatch_header(void)
{
    fprintf(stderr, "header mismatch detail (master : apprentice):\n");
    if (master_header.magic != RISU_MAGIC) {
        fprintf(stderr, "  magic: %08x vs %08x\n",
                master_header.magic, RISU_MAGIC);
        /* If the magic number is wrong, everything else is garbage too. */
        return;
    }

    RisuOp a_op = get_risuop(&apprentice_ri);
    RisuOp m_op = master_header.risu_op;
    if (a_op != m_op) {
        fprintf(stderr, "  op   : %d != %d\n", m_op, a_op);
        /* If the opcode is mismatched, we can't compute size. */
    } else {
        const char *kind;
        size_t m_sz = master_header.size;
        size_t a_sz;

        switch (a_op) {
        case OP_TESTEND:
        case OP_COMPARE:
        case OP_SIGILL:
            kind = "reginfo";
            a_sz = reginfo_size(&apprentice_ri);
            break;
        case OP_SETMEMBLOCK:
        case OP_GETMEMBLOCK:
            kind = "unexpected";
            a_sz = 0;
            break;
        case OP_COMPAREMEM:
            kind = "memblock";
            a_sz = MEMBLOCKLEN;
            break;
        default:
            abort();
        }
        if (a_sz != m_sz) {
            fprintf(stderr, " size : %zd != %zd (%s)\n",
                    m_sz, a_sz, kind);
        } else {
            /* If magic, op, and size are the same, how did we get here? */
            abort();
        }
    }

    uint64_t a_pc = get_pc(&apprentice_ri);
    uint64_t m_pc = master_header.pc;
    if (a_pc != m_pc) {
        fprintf(stderr, "  pc   : %016" PRIx64 " vs %016" PRIx64 "\n",
                m_pc, a_pc);
    } else {
        fprintf(stderr, "  pc   : %016" PRIx64 "\n", a_pc);
    }
}
