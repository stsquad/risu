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

struct reginfo master_ri, apprentice_ri;

uint8_t apprentice_memblock[MEMBLOCKLEN];

static int mem_used;
static int packet_mismatch;

int send_register_info(write_fn write_fn, void *uc)
{
    struct reginfo ri;
    trace_header_t header;
    RisuOp op;

    reginfo_init(&ri, uc);
    op = get_risuop(&ri);

    /* Write a header with PC/op to keep in sync */
    header.pc = get_pc(&ri);
    header.risu_op = op;
    if (write_fn(&header, sizeof(header)) != 0) {
        return -1;
    }

    switch (op) {
    case OP_COMPARE:
    case OP_TESTEND:
    case OP_SIGILL:
        /*
         * Do a simple register compare on (a) explicit request
         * (b) end of test (c) a non-risuop UNDEF
         */
        if (write_fn(&ri, reginfo_size()) != 0) {
            return -1;
        }
        /* For OP_TEST_END, force return 1 to exit. */
        return op == OP_TESTEND;
    case OP_SETMEMBLOCK:
        memblock = (void *)(uintptr_t)get_reginfo_paramreg(&ri);
        break;
    case OP_GETMEMBLOCK:
        set_ucontext_paramreg(uc,
                              get_reginfo_paramreg(&ri) + (uintptr_t)memblock);
        break;
    case OP_COMPAREMEM:
        return write_fn(memblock, MEMBLOCKLEN);
        break;
    default:
        abort();
    }
    return 0;
}

/* Read register info from the socket and compare it with that from the
 * ucontext. Return 0 for match, 1 for end-of-test, 2 for mismatch.
 * NB: called from a signal handler.
 *
 * We don't have any kind of identifying info in the incoming data
 * that says whether it is register or memory data, so if the two
 * sides get out of sync then we will fail obscurely.
 */
int recv_and_compare_register_info(read_fn read_fn,
                                   respond_fn resp_fn, void *uc)
{
    int resp = 0;
    trace_header_t header;
    RisuOp op;

    reginfo_init(&master_ri, uc);
    op = get_risuop(&master_ri);

    if (read_fn(&header, sizeof(header)) != 0) {
        return -1;
    }

    if (header.risu_op != op) {
        /* We are out of sync */
        resp = 2;
        resp_fn(resp);
        return resp;
    }

    /* send OK for the header */
    resp_fn(0);

    switch (op) {
    case OP_COMPARE:
    case OP_TESTEND:
    case OP_SIGILL:
        /* Do a simple register compare on (a) explicit request
         * (b) end of test (c) a non-risuop UNDEF
         */
        if (read_fn(&apprentice_ri, reginfo_size())) {
            packet_mismatch = 1;
            resp = 2;
        } else if (!reginfo_is_eq(&master_ri, &apprentice_ri)) {
            /* register mismatch */
            resp = 2;
        } else if (op == OP_TESTEND) {
            resp = 1;
        }
        resp_fn(resp);
        break;
    case OP_SETMEMBLOCK:
        memblock = (void *)(uintptr_t)get_reginfo_paramreg(&master_ri);
        break;
    case OP_GETMEMBLOCK:
        set_ucontext_paramreg(uc, get_reginfo_paramreg(&master_ri) +
                              (uintptr_t)memblock);
        break;
    case OP_COMPAREMEM:
        mem_used = 1;
        if (read_fn(apprentice_memblock, MEMBLOCKLEN)) {
            packet_mismatch = 1;
            resp = 2;
        } else if (memcmp(memblock, apprentice_memblock, MEMBLOCKLEN) != 0) {
            /* memory mismatch */
            resp = 2;
        }
        resp_fn(resp);
        break;
    default:
        abort();
    }

    return resp;
}

/* Print a useful report on the status of the last comparison
 * done in recv_and_compare_register_info(). This is called on
 * exit, so need not restrict itself to signal-safe functions.
 * Should return 0 if it was a good match (ie end of test)
 * and 1 for a mismatch.
 */
int report_match_status(bool trace)
{
    int resp = 0;
    fprintf(stderr, "match status...\n");
    if (packet_mismatch) {
        fprintf(stderr, "packet mismatch (probably disagreement "
                "about UNDEF on load/store)\n");
        /* We don't have valid reginfo from the apprentice side
         * so stop now rather than printing anything about it.
         */
        fprintf(stderr, "%s reginfo:\n", trace ? "this" : "master");
        reginfo_dump(&master_ri, stderr);
        return 1;
    }
    if (!reginfo_is_eq(&master_ri, &apprentice_ri)) {
        fprintf(stderr, "mismatch on regs!\n");
        resp = 1;
    }
    if (mem_used
        && memcmp(memblock, &apprentice_memblock, MEMBLOCKLEN) != 0) {
        fprintf(stderr, "mismatch on memory!\n");
        resp = 1;
    }
    if (!resp) {
        fprintf(stderr, "match!\n");
        return 0;
    }

    fprintf(stderr, "%s reginfo:\n", trace ? "this" : "master");
    reginfo_dump(&master_ri, stderr);
    fprintf(stderr, "%s reginfo:\n", trace ? "trace" : "apprentice");
    reginfo_dump(&apprentice_ri, stderr);

    if (trace) {
        reginfo_dump_mismatch(&apprentice_ri, &master_ri, stderr);
    } else {
        reginfo_dump_mismatch(&master_ri, &apprentice_ri, stderr);
    }
    return resp;
}
