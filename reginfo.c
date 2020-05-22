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

RisuResult send_register_info(void *uc)
{
    struct reginfo ri;
    trace_header_t header;
    RisuResult res;
    RisuOp op;

    reginfo_init(&ri, uc);
    op = get_risuop(&ri);

    /* Write a header with PC/op to keep in sync */
    header.pc = get_pc(&ri);
    header.risu_op = op;
    res = write_buffer(&header, sizeof(header));
    if (res != RES_OK) {
        return res;
    }

    switch (op) {
    case OP_COMPARE:
    case OP_TESTEND:
    case OP_SIGILL:
        /*
         * Do a simple register compare on (a) explicit request
         * (b) end of test (c) a non-risuop UNDEF
         */
        res = write_buffer(&ri, reginfo_size());
        /* For OP_TEST_END, force exit. */
        if (res == RES_OK && op == OP_TESTEND) {
            res = RES_END;
        }
        break;
    case OP_SETMEMBLOCK:
        memblock = (void *)(uintptr_t)get_reginfo_paramreg(&ri);
        break;
    case OP_GETMEMBLOCK:
        set_ucontext_paramreg(uc,
                              get_reginfo_paramreg(&ri) + (uintptr_t)memblock);
        break;
    case OP_COMPAREMEM:
        return write_buffer(memblock, MEMBLOCKLEN);
    default:
        abort();
    }
    return res;
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
    trace_header_t header;
    RisuOp op;

    reginfo_init(&apprentice_ri, uc);
    op = get_risuop(&apprentice_ri);

    res = read_buffer(&header, sizeof(header));
    if (res != RES_OK) {
        return res;
    }

    if (header.risu_op != op) {
        /* We are out of sync.  Tell master to exit. */
        respond(RES_END);
        return RES_BAD_IO;
    }

    /* send OK for the header */
    respond(RES_OK);

    switch (op) {
    case OP_COMPARE:
    case OP_TESTEND:
    case OP_SIGILL:
        /* Do a simple register compare on (a) explicit request
         * (b) end of test (c) a non-risuop UNDEF
         */
        res = read_buffer(&master_ri, reginfo_size());
        if (res != RES_OK) {
            /* fail */
        } else if (!reginfo_is_eq(&master_ri, &apprentice_ri)) {
            /* register mismatch */
            res = RES_MISMATCH_REG;
        } else if (op == OP_TESTEND) {
            res = RES_END;
        }
        respond(res == RES_OK ? RES_OK : RES_END);
        break;
    case OP_SETMEMBLOCK:
        memblock = (void *)(uintptr_t)get_reginfo_paramreg(&apprentice_ri);
        break;
    case OP_GETMEMBLOCK:
        set_ucontext_paramreg(uc, get_reginfo_paramreg(&apprentice_ri) +
                              (uintptr_t)memblock);
        break;
    case OP_COMPAREMEM:
        res = read_buffer(master_memblock, MEMBLOCKLEN);
        if (res != RES_OK) {
            /* fail */
        } else if (memcmp(memblock, master_memblock, MEMBLOCKLEN) != 0) {
            /* memory mismatch */
            res = RES_MISMATCH_MEM;
        }
        respond(res == RES_OK ? RES_OK : RES_END);
        break;
    default:
        abort();
    }

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
