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
#include <ucontext.h>
#include <string.h>

#include "risu.h"
#include "risu_reginfo_i386.h"

struct reginfo master_ri, apprentice_ri;

static int insn_is_ud2(uint32_t insn)
{
    return ((insn & 0xffff) == 0x0b0f);
}

void advance_pc(void *vuc)
{
    ucontext_t *uc = (ucontext_t *) vuc;

    /* We assume that this is either UD1 or UD2.
     * This would need tweaking if we want to test
     * expected undefs on x86.
     */
    uc->uc_mcontext.gregs[REG_EIP] += 2;
}

void set_ucontext_paramreg(void *vuc, uint64_t value)
{
    ucontext_t *uc = (ucontext_t *) vuc;
    uc->uc_mcontext.gregs[REG_EAX] = (uint32_t) value;
}

uint64_t get_reginfo_paramreg(struct reginfo *ri)
{
    return ri->gregs[REG_EAX];
}

int get_risuop(struct reginfo *ri)
{
    switch (ri->faulting_insn & 0xffff) {
    case 0xb90f:                /* UD1 */
        return OP_COMPARE;
    case 0x0b0f:                /* UD2 */
        return OP_TESTEND;
    default:                    /* unexpected */
        return -1;
    }
}

uintptr_t get_pc(struct reginfo *ri)
{
    return ri->gregs[REG_EIP];
}

int send_register_info(int sock, void *uc)
{
    struct reginfo ri;
    fill_reginfo(&ri, uc);
    return send_data_pkt(sock, &ri, sizeof(ri));
}

/* Read register info from the socket and compare it with that from the
 * ucontext. Return 0 for match, 1 for end-of-test, 2 for mismatch.
 * NB: called from a signal handler.
 */
int recv_and_compare_register_info(int sock, void *uc)
{
    int resp;
    fill_reginfo(&master_ri, uc);
    recv_data_pkt(sock, &apprentice_ri, sizeof(apprentice_ri));
    if (memcmp(&master_ri, &apprentice_ri, sizeof(master_ri)) != 0) {
        /* mismatch */
        resp = 2;
    } else if (insn_is_ud2(master_ri.faulting_insn)) {
        /* end of test */
        resp = 1;
    } else {
        /* either successful match or expected undef */
        resp = 0;
    }
    send_response_byte(sock, resp);
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
    fprintf(stderr, "match status...\n");
    fprintf(stderr, "master reginfo:\n");
    dump_reginfo(&master_ri);
    fprintf(stderr, "apprentice reginfo:\n");
    dump_reginfo(&apprentice_ri);
    if (memcmp(&master_ri, &apprentice_ri, sizeof(master_ri)) == 0) {
        fprintf(stderr, "match!\n");
        return 0;
    }
    fprintf(stderr, "mismatch!\n");
    return 1;
}
