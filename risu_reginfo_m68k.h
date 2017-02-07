/*****************************************************************************
 * Copyright (c) 2016 Laurent Vivier
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *****************************************************************************/

#ifndef RISU_REGINFO_M68K_H
#define RISU_REGINFO_M68K_H

struct reginfo
{
    uint32_t faulting_insn;
    uint32_t pc;
    gregset_t gregs;
    fpregset_t fpregs;
};

/* initialize structure from a ucontext */
void reginfo_init(struct reginfo *ri, ucontext_t *uc);

/* return 1 if structs are equal, 0 otherwise. */
int reginfo_is_eq(struct reginfo *r1, struct reginfo *r2, ucontext_t *uc);

/* print reginfo state to a stream */
void reginfo_dump(struct reginfo *ri, int is_master);

/* reginfo_dump_mismatch: print mismatch details to a stream, ret nonzero=ok */
int reginfo_dump_mismatch(struct reginfo *m, struct reginfo *a, FILE *f);

#endif /* RISU_REGINFO_M68K_H */
