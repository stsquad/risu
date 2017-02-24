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

#include "risu.h"

int send_register_info(int sock, void *uc)
{
    struct reginfo ri;
    int op;
    reginfo_init(&ri, uc);
    op = get_risuop(&ri);

    switch (op) {
    case OP_COMPARE:
    case OP_TESTEND:
    default:
        /* Do a simple register compare on (a) explicit request
         * (b) end of test (c) a non-risuop UNDEF
         */
        return send_data_pkt(sock, &ri, sizeof(ri));
    case OP_SETMEMBLOCK:
        memblock = (void *)(uintptr_t)get_reginfo_paramreg(&ri);
       break;
    case OP_GETMEMBLOCK:
        set_ucontext_paramreg(uc,
                              get_reginfo_paramreg(&ri) + (uintptr_t)memblock);
        break;
    case OP_COMPAREMEM:
        return send_data_pkt(sock, memblock, MEMBLOCKLEN);
        break;
    }
    return 0;
}
