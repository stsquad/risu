/*******************************************************************************
 * Copyright (c) 2016 Laurent Vivier
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 ******************************************************************************/

#include <stdio.h>
#include <ucontext.h>
#include <string.h>

#include "risu.h"
#include "risu_reginfo_m68k.h"

struct reginfo master_ri, apprentice_ri;
static int mem_used = 0;
static int packet_mismatch = 0;

uint8_t apprentice_memblock[MEMBLOCKLEN];

void advance_pc(void *vuc)
{
    ucontext_t *uc = (ucontext_t*)vuc;
    uc->uc_mcontext.gregs[R_PC] += 4;
}

void set_a0(void *vuc, uint32_t a0)
{
    ucontext_t *uc = vuc;
    uc->uc_mcontext.gregs[R_A0] = a0;
}

static int get_risuop(uint32_t insn)
{
    uint32_t op = insn & 0xf;
    uint32_t key = insn & ~0xf;
    uint32_t risukey = 0x4afc7000;
    return (key != risukey) ? -1 : op;
}

int send_register_info(int sock, void *uc)
{
    struct reginfo ri;
    int op;

    reginfo_init(&ri, uc);
    op = get_risuop(ri.faulting_insn);

    switch (op) {
    case OP_COMPARE:
    case OP_TESTEND:
    default:
        return send_data_pkt(sock, &ri, sizeof(ri));
    case OP_SETMEMBLOCK:
        memblock = (void*)ri.gregs[R_A0];
        break;
    case OP_GETMEMBLOCK:
        set_a0(uc, ri.gregs[R_A0] + (uintptr_t)memblock);
        break;
    case OP_COMPAREMEM:
        return send_data_pkt(sock, memblock, MEMBLOCKLEN);
        break;
    }
    return 0;
}

/* Read register info from the socket and compare it with that from the
 * ucontext. Return 0 for match, 1 for end-of-test, 2 for mismatch.
 * NB: called from a signal handler.
 */
int recv_and_compare_register_info(int sock, void *uc)
{
    int resp = 0;
    int op;

    reginfo_init(&master_ri, uc);
    op = get_risuop(master_ri.faulting_insn);

    switch (op) {
    case OP_COMPARE:
    case OP_TESTEND:
    default:
        if (recv_data_pkt(sock, &apprentice_ri, sizeof(apprentice_ri))) {
            packet_mismatch = 1;
            resp = 2;
        } else if (!reginfo_is_eq(&master_ri, &apprentice_ri, uc)) {
            resp = 2;
        }
        else if (op == OP_TESTEND) {
            resp = 1;
        }
        send_response_byte(sock, resp);
        break;
    case OP_SETMEMBLOCK:
        memblock = (void*)master_ri.gregs[R_A0];
        break;
    case OP_GETMEMBLOCK:
        set_a0(uc, master_ri.gregs[R_A0] + (uintptr_t)memblock);
        break;
    case OP_COMPAREMEM:
        mem_used = 1;
        if (recv_data_pkt(sock, apprentice_memblock, MEMBLOCKLEN)) {
            packet_mismatch = 1;
            resp = 2;
        } else if (memcmp(memblock, apprentice_memblock, MEMBLOCKLEN) != 0) {
            resp = 2;
        }
        send_response_byte(sock, resp);
        break;
    }
    return resp;
}

/* Print a useful report on the status of the last comparison
 * done in recv_and_compare_register_info(). This is called on
 * exit, so need not restrict itself to signal-safe functions.
 * Should return 0 if it was a good match (ie end of test)
 * and 1 for a mismatch.
 */
int report_match_status(void)
{
    int resp = 0;
    fprintf(stderr, "match status...\n");

    if (packet_mismatch) {
        fprintf(stderr, "packet mismatch (probably disagreement "
                "about UNDEF on load/store)\n");
        fprintf(stderr, "master reginfo:\n");
        reginfo_dump(&master_ri, 0);
    }
    if (!reginfo_is_eq(&master_ri, &apprentice_ri, NULL)) {
        fprintf(stderr, "mismatch on regs!\n");
        resp = 1;
    }
    if (mem_used && memcmp(memblock, &apprentice_memblock, MEMBLOCKLEN) != 0) {
        fprintf(stderr, "mismatch on memory!\n");
        resp = 1;
    }
    if (!resp) {
        fprintf(stderr, "match!\n");
        return 0;
    }

    fprintf(stderr, "master reginfo:\n");
    reginfo_dump(&master_ri, 1);

    fprintf(stderr, "apprentice reginfo:\n");
    reginfo_dump(&apprentice_ri, 0);

    reginfo_dump_mismatch(&master_ri, &apprentice_ri, stderr);
    return resp;
}
